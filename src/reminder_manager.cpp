#include "reminder_manager.h"

#include "break_reminder.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSystemTrayIcon>
#include <QTime>
#include <QUuid>

#include <algorithm>

namespace {
constexpr int K_PENDING_GRACE_SECONDS = 300;  // five minutes before a missed reminder becomes debt
constexpr int K_SNOOZE_MINUTES		  = 5;
constexpr int K_MAX_FREE_SNOOZES	  = 2;
constexpr int K_BREAK_WARNING_SECONDS = 60;
constexpr int K_MISSED_RETRY_SECONDS  = 60;

QString ledgerFilePath() {
	return QDir(SettingsManager::configDir()).filePath(QStringLiteral("debt-ledger.json"));
}

QString pendingFilePath() {
	return QDir(SettingsManager::configDir()).filePath(QStringLiteral("pending.json"));
}

QString pendingBackupPath() {
	const auto info	  = QFileInfo(pendingFilePath());
	auto	   backup = info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
				  + QStringLiteral(".corrupt-")
				  + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"))
				  + QStringLiteral(".json");
	int suffix = 1;
	while(QFile::exists(backup))
		backup = info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
				 + QStringLiteral(".corrupt-")
				 + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"))
				 + QStringLiteral("-") + QString::number(suffix++) + QStringLiteral(".json");
	return backup;
}

QString newInstanceId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString levelDisplay(const QString& level) {
	if(level == QStringLiteral("no_debt"))
		return QStringLiteral("无欠账");
	if(level == QStringLiteral("mild"))
		return QStringLiteral("轻微");
	if(level == QStringLiteral("moderate"))
		return QStringLiteral("中等");
	if(level == QStringLiteral("severe"))
		return QStringLiteral("严重");
	return QStringLiteral("危急");
}

QDateTime parseDateTime(const QJsonValue& value) {
	return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}

QString formatDateTime(const QDateTime& value) {
	return value.isValid() ? value.toString(Qt::ISODateWithMs) : QString();
}

QTime remainingTime(const QDateTime& now, const QDateTime& deadline) {
	if(!deadline.isValid())
		return QTime(0, 0, 0);
	const qint64 seconds = std::clamp<qint64>(now.secsTo(deadline), 0, 23 * 3600 + 59 * 60 + 59);
	return QTime(static_cast<int>(seconds / 3600),
				 static_cast<int>((seconds % 3600) / 60),
				 static_cast<int>(seconds % 60));
}
}  // namespace

ReminderManager::ReminderManager(QSystemTrayIcon* trayIcon, QObject* parent, bool enableNotifications)
	: QObject(parent)
	, _mNotifier(trayIcon)
	, _mLedger(ledgerFilePath())
	, _mEnableNotifications(enableNotifications)
	, _mPendingStorePath(pendingFilePath()) {
	_mTick.setInterval(1000);
	connect(&_mTick, &QTimer::timeout, this, &ReminderManager::onTick);

	connect(&_mNotifier, &DesktopNotifier::actionInvoked, this, &ReminderManager::onNotificationAction);
	connect(&_mNotifier, &DesktopNotifier::notificationClosed, this, &ReminderManager::onNotificationClosed);
	connect(&_mPhrases, &PhraseCatalog::changed, this, &ReminderManager::onPhrasesChanged);
	connect(&_mPhrases, &PhraseCatalog::reloadFailed, this, &ReminderManager::onPhrasesReloadFailed);

	_mBreakReminder = new BreakReminder(this);
	connect(_mBreakReminder, &BreakReminder::skipConfirmed, this, &ReminderManager::onBreakSkipConfirmed);

	const auto loadResult = _mLedger.load();
	_mLedgerWritable	  = loadResult._ok;
	if(loadResult._recovered) {
		_mStartupError = QStringLiteral("兄弟账本损坏，原文件已备份到：%1").arg(loadResult._backupPath);
	} else if(!loadResult._ok) {
		_mStartupError = loadResult._error;
	}

	QString phraseError;
	_mPhrases.loadActive(&phraseError);
	if(!phraseError.isEmpty() && _mStartupError.isEmpty())
		_mStartupError = phraseError;
}

ReminderManager::~ReminderManager() {
	_mShuttingDown = true;
	if(_mStarted)
		stop();
}

void ReminderManager::configure(const QVector<ReminderConfig>& tasks) {
	const auto				current = now();
	const auto				old		= _mTasks;
	QHash<QString, Runtime> next;
	QStringList				order;
	QString					breakTaskId;

	for(const auto& config : tasks) {
		if(config._id.isEmpty() || next.contains(config._id))
			continue;

		Runtime	   runtime;
		const auto oldIt = old.constFind(config._id);
		if(oldIt != old.constEnd()) {
			const auto oldConfig  = oldIt.value()._config;
			const bool wasEnabled = oldConfig._enabled;
			runtime				  = oldIt.value();
			runtime._config		  = config;
			if(config._enabled && (!wasEnabled || oldConfig._intervalMinutes != config._intervalMinutes)
			   && !runtime._pending)
				runtime._nextDue = current.addSecs(std::max(1, config._intervalMinutes) * 60);
			if(!config._enabled) {
				closeNotificationsForInstance(runtime._instanceId);
				runtime._pending = false;
				runtime._instanceId.clear();
				runtime._pendingUnits  = 0;
				runtime._pendingWeight = 0;
				runtime._snoozeCount   = 0;
				runtime._warningSent   = false;
				runtime._nextDue	   = current.addSecs(std::max(1, config._intervalMinutes) * 60);
			}
		} else {
			runtime._config	 = config;
			runtime._nextDue = current.addSecs(std::max(1, config._intervalMinutes) * 60);
		}
		if(!runtime._nextDue.isValid())
			runtime._nextDue = current.addSecs(std::max(1, config._intervalMinutes) * 60);
		if(config.isBreakTask() && breakTaskId.isEmpty())
			breakTaskId = config._id;
		next.insert(config._id, runtime);
		order.append(config._id);
	}

	for(auto it = old.cbegin(); it != old.cend(); ++it) {
		if(it->_pending && !next.contains(it.key()))
			closeNotificationsForInstance(it->_instanceId);
	}

	_mTasks		  = next;
	_mTaskOrder	  = order;
	_mBreakTaskId = breakTaskId;

	if(_mBreaking) {
		const auto it = _mTasks.constFind(_mBreakingTaskId);
		if(it == _mTasks.constEnd() || !it.value()._config._enabled) {
			_mBreakReminder->closeBreak();
			_mBreaking = false;
			emit breakFinished();
		}
	}

	if(!_mPendingLoaded) {
		_mPendingLoaded = true;
		loadPending();
	} else {
		savePending();
	}

	if(_mStarted && !_mTick.isActive())
		_mTick.start();
	emit stateChanged();
}

void ReminderManager::start() {
	if(_mStarted)
		return;
	_mStarted = true;
	if(!_mStartupError.isEmpty()) {
		emitError(_mStartupError);
		_mStartupError.clear();
	}
	_mTick.start();
	poll(now());
	emit stateChanged();
}

void ReminderManager::stop() {
	_mStarted = false;
	_mTick.stop();
	if(_mBreaking) {
		_mBreakReminder->closeBreak();
		_mBreaking = false;
		emit breakFinished();
	}
	savePending();
}

void ReminderManager::setQuietHours(bool enabled, const QString& start, const QString& end) {
	_mQuietHoursEnabled	  = enabled;
	const auto validStart = QTime::fromString(start, QStringLiteral("HH:mm"));
	const auto validEnd	  = QTime::fromString(end, QStringLiteral("HH:mm"));
	_mQuietHoursStart =
		validStart.isValid() ? validStart.toString(QStringLiteral("HH:mm")) : QStringLiteral("22:00");
	_mQuietHoursEnd =
		validEnd.isValid() ? validEnd.toString(QStringLiteral("HH:mm")) : QStringLiteral("08:00");
	emit stateChanged();
}

void ReminderManager::setPausedUntil(const QDateTime& until) {
	_mPausedUntil = until;
	emit stateChanged();
}

void ReminderManager::setBreakMessage(const QString& message) { _mBreakMessage = message.trimmed(); }

void ReminderManager::setPhraseAutoReload(bool enabled) { _mPhrases.setAutoReloadEnabled(enabled); }

QDateTime ReminderManager::now() const {
	return _mNowOverride.isValid() ? _mNowOverride : QDateTime::currentDateTime();
}

void ReminderManager::setNowForTest(const QDateTime& now) { _mNowOverride = now; }

void ReminderManager::clearNowForTest() { _mNowOverride = QDateTime(); }

void ReminderManager::pollAt(const QDateTime& now) {
	_mNowOverride = now;
	poll(now);
}

void ReminderManager::onTick() { poll(now()); }

void ReminderManager::poll(const QDateTime& current) {
	if(!_mPendingLoaded) {
		_mPendingLoaded = true;
		loadPending();
	}

	if(_mPausedUntil.isValid() && current >= _mPausedUntil) {
		_mPausedUntil = QDateTime();
		emit stateChanged();
	}

	for(const auto& id : _mTaskOrder) {
		auto* runtime = runtimeFor(id);
		if(runtime)
			expirePending(*runtime, current);
	}

	if(_mBreaking) {
		emitCountdown(current);
		if(current >= _mBreakEndsAt)
			finishBreak(current, false);
		return;
	}

	for(const auto& id : _mTaskOrder) {
		auto* runtime = runtimeFor(id);
		if(!runtime)
			continue;
		if(id == _mBreakTaskId)
			pollBreak(*runtime, current);
		else
			pollTask(*runtime, current);
	}
	emitCountdown(current);
}

void ReminderManager::pollTask(Runtime& runtime, const QDateTime& current) {
	if(!runtime._config._enabled || runtime._pending)
		return;
	if(!runtime._nextDue.isValid()) {
		scheduleNext(runtime, current);
		return;
	}
	if(current < runtime._nextDue)
		return;
	if(isSuppressed(current)) {
		scheduleNext(runtime, current);
		return;
	}
	createPending(runtime, current);
}

void ReminderManager::pollBreak(Runtime& runtime, const QDateTime& current) {
	if(!runtime._config._enabled)
		return;
	if(!runtime._nextDue.isValid())
		scheduleNext(runtime, current);

	if(isSuppressed(current)) {
		if(current >= runtime._nextDue) {
			scheduleNext(runtime, current);
			runtime._warningSent = false;
		}
		return;
	}

	if(!runtime._warningSent && current < runtime._nextDue
	   && current >= runtime._nextDue.addSecs(-K_BREAK_WARNING_SECONDS)) {
		notifyBreakWarning(runtime);
		runtime._warningSent = true;
	}

	if(current >= runtime._nextDue)
		beginBreak(runtime, current);
}

void ReminderManager::expirePending(Runtime& runtime, const QDateTime& current) {
	if(!runtime._pending)
		return;

	// Pausing or entering quiet hours must never turn an outstanding reminder
	// into debt. Drop it without a ledger event and restart its cadence.
	if(isSuppressed(current)) {
		clearPending(runtime, false);
		scheduleNext(runtime, current);
		savePending();
		emit stateChanged();
		return;
	}

	if(current < runtime._pendingDeadline)
		return;

	if(!_mLedgerWritable) {
		// Retry once a minute instead of emitting an error on every poll tick.
		runtime._pendingDeadline = current.addSecs(K_MISSED_RETRY_SECONDS);
		savePending();
		emitError(QStringLiteral("账本不可写，欠账未能记录。"));
		return;
	}
	if(_mLedger.hasEventForInstance(runtime._instanceId)) {
		// A previous attempt committed the event but crashed before clearing
		// pending.json. This instance is already settled.
		const auto dueAt = runtime._pendingDueAt.isValid() ? runtime._pendingDueAt : current;
		clearPending(runtime, false);
		scheduleNext(runtime, dueAt);
		if(runtime._nextDue <= current)
			scheduleNext(runtime, current);
		savePending();
		emit stateChanged();
		return;
	}

	if(!_mLedger.recordDebt(runtime._config._id,
							DebtLedger::EventKind::Missed,
							runtime._pendingUnits,
							runtime._pendingWeight,
							runtime._instanceId)) {
		runtime._pendingDeadline = current.addSecs(K_MISSED_RETRY_SECONDS);
		emitError(QStringLiteral("无法记录漏掉的提醒，稍后重试。"));
		savePending();
		return;
	}

	const auto dueAt = runtime._pendingDueAt.isValid() ? runtime._pendingDueAt : current;
	clearPending(runtime, false);
	runtime._snoozeCount = 0;
	scheduleNext(runtime, dueAt);
	if(runtime._nextDue <= current)
		scheduleNext(runtime, current);
	savePending();
	setStatusPhrase(_mPhrases.phraseForTask(runtime._config._id, QStringLiteral("missed"), score()));
	emit stateChanged();
}

void ReminderManager::createPending(Runtime& runtime, const QDateTime& current) {
	if(runtime._pending || !runtime._config._enabled)
		return;

	runtime._pending		 = true;
	runtime._instanceId		 = newInstanceId();
	runtime._pendingUnits	 = std::max(1, runtime._config.unitsPerEvent());
	runtime._pendingWeight	 = std::max(1, runtime._config._weight);
	runtime._pendingDueAt	 = current;
	runtime._pendingDeadline = current.addSecs(K_PENDING_GRACE_SECONDS);
	// Preserve the snooze count when this pending instance was created by a
	// snooze. It is cleared when a reminder is completed, skipped or missed.
	scheduleNext(runtime, current);
	savePending();
	notifyPending(runtime);
	emit stateChanged();
}

void ReminderManager::notifyPending(Runtime& runtime) {
	if(!_mEnableNotifications || !runtime._pending)
		return;

	const auto task	 = runtime._config;
	const auto title = QStringLiteral("%1提醒：兄弟来了").arg(task._name);
	auto	   body	 = notificationBody(task._id, runtime._pendingUnits);
	if(!_mNotifier.actionsSupported())
		body += QStringLiteral("\n没有按钮时请打开主程序处理。");

	const auto completeLabel =
		task._id == QStringLiteral("water")
			? QStringLiteral("喝了（%1 %2）").arg(runtime._pendingUnits).arg(task._unit)
			: QStringLiteral("完成本次（%1 %2）").arg(runtime._pendingUnits).arg(task._unit);

	const QStringList actions{
		QStringLiteral("complete"),
		completeLabel,
		QStringLiteral("snooze"),
		QStringLiteral("5 分钟后再提醒"),
		QStringLiteral("skip"),
		QStringLiteral("跳过并记一笔"),
	};

	const auto result = _mNotifier.notify(title, body, actions, 10000);
	if(result._id != 0)
		_mNotificationInstances.insert(result._id, runtime._instanceId);
	if(!result._shown) {
		qWarning().noquote() << QStringLiteral("提醒未能显示：%1").arg(title);
		setStatusPhrase(QStringLiteral("系统通知不可用，请打开主程序处理本次提醒。"));
	}
}

void ReminderManager::notifyBreakWarning(const Runtime& runtime) {
	if(!_mEnableNotifications)
		return;
	const auto title	   = QStringLiteral("休息预告：%1 分钟后").arg(K_BREAK_WARNING_SECONDS / 60);
	const auto taskPhrase  = _mPhrases.phraseForTask(runtime._config._id, QStringLiteral("due"), score());
	const auto levelPhrase = _mPhrases.phraseForScore(score());
	auto body = QStringLiteral("兄弟还有 %1 秒就要来收走屏幕了，先把手上的事收个尾。\n%2")
					.arg(K_BREAK_WARNING_SECONDS)
					.arg(taskPhrase);
	if(levelPhrase != taskPhrase)
		body += QLatin1Char('\n') + levelPhrase;
	_mNotifier.notify(title,
					  body,
					  {QStringLiteral("ledger"), QStringLiteral("查看兄弟账本")},
					  K_BREAK_WARNING_SECONDS * 1000);
}

void ReminderManager::beginBreak(Runtime& runtime, const QDateTime& current) {
	if(_mBreaking)
		return;

	_mBreaking			 = true;
	_mBreakingTaskId	 = runtime._config._id;
	_mBreakEndsAt		 = current.addSecs(std::max(1, runtime._config._breakMinutes) * 60);
	runtime._warningSent = false;

	// Do not let a water reminder expire while the user is away from the screen
	// taking a break. Clear it without debt and restart its cadence after rest.
	for(const auto& id : _mTaskOrder) {
		auto* other = runtimeFor(id);
		if(!other || other == &runtime)
			continue;
		if(other->_pending) {
			clearPending(*other, false);
			other->_snoozeCount = 0;
		}
		const auto afterBreak = _mBreakEndsAt.addSecs(std::max(1, other->_config._intervalMinutes) * 60);
		if(other->_nextDue.isValid() && other->_nextDue > afterBreak)
			continue;
		other->_nextDue = afterBreak;
	}
	savePending();

	QString message = _mBreakMessage;
	if(message.isEmpty())
		message = _mPhrases.phraseForTask(runtime._config._id, QStringLiteral("due"), score());
	_mBreakReminder->setActionPrompt(actionPrompt());
	_mBreakReminder->showBreakWithMessage(runtime._config._breakMinutes, message);

	emit breakStarted(runtime._config._breakMinutes);
	emit stateChanged();
	emitCountdown(current);
}

void ReminderManager::finishBreak(const QDateTime& current, bool skipped) {
	if(!_mBreaking)
		return;

	const auto taskId = _mBreakingTaskId;
	_mBreakReminder->closeBreak();
	_mBreaking	  = false;

	auto* runtime = runtimeFor(taskId);
	if(skipped && runtime && runtime->_config._enabled) {
		if(!_mLedgerWritable) {
			emitError(QStringLiteral("账本不可写，本次休息欠账没有记录。"));
		} else if(!_mLedger.recordDebt(taskId,
									   DebtLedger::EventKind::Skipped,
									   std::max(1, runtime->_config.unitsPerEvent()),
									   std::max(1, runtime->_config._weight))) {
			emitError(QStringLiteral("无法记录休息欠账。"));
		} else {
			setStatusPhrase(_mPhrases.phraseForTask(taskId, QStringLiteral("skipped"), score()));
		}
	}

	for(const auto& id : _mTaskOrder) {
		auto* task = runtimeFor(id);
		if(!task)
			continue;
		task->_warningSent = false;
		task->_snoozeCount = 0;
		task->_nextDue	   = current.addSecs(std::max(1, task->_config._intervalMinutes) * 60);
	}
	_mBreakingTaskId.clear();
	savePending();
	emit breakFinished();
	if(skipped)
		emit breakSkipped();
	emit stateChanged();
	emitCountdown(current);
}

void ReminderManager::skipCurrentBreak() {
	if(!_mBreaking)
		return;
	finishBreak(now(), true);
}

void ReminderManager::onBreakSkipConfirmed() { skipCurrentBreak(); }

void ReminderManager::scheduleNext(Runtime& runtime, const QDateTime& from) {
	const int minutes	 = std::max(1, runtime._config._intervalMinutes);
	runtime._nextDue	 = from.addSecs(minutes * 60);
	runtime._warningSent = false;
}

void ReminderManager::closeNotificationsForInstance(const QString& instanceId) {
	if(instanceId.isEmpty())
		return;
	for(auto it = _mNotificationInstances.begin(); it != _mNotificationInstances.end();) {
		if(it.value() == instanceId) {
			_mNotifier.close(it.key());
			it = _mNotificationInstances.erase(it);
		} else {
			++it;
		}
	}
}

void ReminderManager::clearPending(Runtime& runtime, bool removeFile) {
	if(!runtime._pending)
		return;
	const auto instance = runtime._instanceId;
	runtime._pending	= false;
	runtime._instanceId.clear();
	runtime._pendingUnits	 = 0;
	runtime._pendingWeight	 = 0;
	runtime._pendingDueAt	 = QDateTime();
	runtime._pendingDeadline = QDateTime();
	closeNotificationsForInstance(instance);
	if(removeFile)
		savePending();
}

bool ReminderManager::recordDebtAndClear(Runtime&			   runtime,
										 DebtLedger::EventKind kind,
										 const QString&		   phraseEvent) {
	if(!runtime._pending)
		return false;
	if(!_mLedgerWritable) {
		emitError(QStringLiteral("账本不可写，欠账没有记录。"));
		return false;
	}
	if(_mLedger.hasEventForInstance(runtime._instanceId)) {
		clearPending(runtime, false);
		savePending();
		emit stateChanged();
		return true;
	}
	if(!_mLedger.recordDebt(
		   runtime._config._id, kind, runtime._pendingUnits, runtime._pendingWeight, runtime._instanceId)) {
		emitError(QStringLiteral("无法写入兄弟账本，本次欠账没有记录。"));
		return false;
	}
	clearPending(runtime, false);
	runtime._snoozeCount = 0;
	savePending();
	setStatusPhrase(_mPhrases.phraseForTask(runtime._config._id, phraseEvent, score()));
	emit stateChanged();
	return true;
}

bool ReminderManager::completeTask(const QString& taskId) {
	if(isBreakTask(taskId))
		return false;
	auto* runtime = runtimeFor(taskId);
	if(!runtime || !runtime->_pending)
		return false;

	if(_mLedger.hasEventForInstance(runtime->_instanceId)) {
		// Already committed before a crash; just clear the stale pending file.
	} else if(!_mLedgerWritable) {
		emitError(QStringLiteral("账本不可写，本次完成没有记录。"));
	} else if(!_mLedger.recordCompletion(taskId, runtime->_pendingUnits, runtime->_instanceId)) {
		emitError(QStringLiteral("无法记录本次完成，但提醒已结清。"));
	}
	clearPending(*runtime, false);
	runtime->_snoozeCount = 0;
	scheduleNext(*runtime, now());
	savePending();
	setStatusPhrase(_mPhrases.phraseForTask(taskId, QStringLiteral("completed"), score()));
	emit stateChanged();
	return true;
}

bool ReminderManager::skipTask(const QString& taskId) {
	if(isBreakTask(taskId))
		return false;
	auto* runtime = runtimeFor(taskId);
	if(!runtime || !runtime->_pending)
		return false;
	if(!recordDebtAndClear(*runtime, DebtLedger::EventKind::Skipped, QStringLiteral("skipped")))
		return false;
	scheduleNext(*runtime, now());
	savePending();
	emit stateChanged();
	return true;
}

bool ReminderManager::snoozeTask(const QString& taskId) {
	if(isBreakTask(taskId))
		return false;
	auto* runtime = runtimeFor(taskId);
	if(!runtime || !runtime->_pending)
		return false;
	if(runtime->_snoozeCount >= K_MAX_FREE_SNOOZES)
		return skipTask(taskId);

	const int snoozeCount = runtime->_snoozeCount + 1;
	clearPending(*runtime, false);
	runtime->_snoozeCount = snoozeCount;
	runtime->_nextDue	  = now().addSecs(K_SNOOZE_MINUTES * 60);
	savePending();
	setStatusPhrase(_mPhrases.phraseForTask(taskId, QStringLiteral("snoozed"), score()));
	emit stateChanged();
	return true;
}

bool ReminderManager::repayOne(const QString& taskId) {
	if(!_mLedgerWritable) {
		emitError(QStringLiteral("账本不可写，无法还账。"));
		return false;
	}
	if(!_mLedger.repayOne(taskId))
		return false;
	emit stateChanged();
	return true;
}

bool ReminderManager::triggerNow(const QString& taskId) {
	auto* runtime = runtimeFor(taskId);
	if(!runtime || !runtime->_config._enabled)
		return false;
	if(isBreakTask(taskId)) {
		if(_mBreaking)
			return false;
		beginBreak(*runtime, now());
		return true;
	}
	if(runtime->_pending)
		return false;
	createPending(*runtime, now());
	return true;
}

bool ReminderManager::hasPending(const QString& taskId) const {
	const auto* runtime = runtimeFor(taskId);
	return runtime && runtime->_pending;
}

int ReminderManager::pendingUnits(const QString& taskId) const {
	const auto* runtime = runtimeFor(taskId);
	return runtime && runtime->_pending ? runtime->_pendingUnits : 0;
}

bool ReminderManager::isBreaking() const { return _mBreaking; }

QTime ReminderManager::breakRemaining() const { return remainingTime(now(), _mBreakEndsAt); }

QDateTime ReminderManager::nextDue(const QString& taskId) const {
	const auto* runtime = runtimeFor(taskId);
	return runtime ? runtime->_nextDue : QDateTime();
}

int ReminderManager::score() const { return _mLedger.score(); }

QString ReminderManager::levelName() const { return displayLevel(); }

int ReminderManager::outstandingCount(const QString& taskId) const {
	return _mLedger.outstandingCount(taskId);
}

qint64 ReminderManager::outstandingUnits(const QString& taskId) const {
	return _mLedger.outstandingUnits(taskId);
}

int ReminderManager::lifetimeDebtCount(const QString& taskId) const {
	return _mLedger.lifetimeDebtCount(taskId);
}

qint64 ReminderManager::lifetimeDebtUnits(const QString& taskId) const {
	return _mLedger.lifetimeDebtUnits(taskId);
}

int ReminderManager::repaymentCount(const QString& taskId) const { return _mLedger.repaymentCount(taskId); }

QStringList ReminderManager::outstandingTaskIds() const { return _mLedger.outstandingTaskIds(); }

QVector<ReminderConfig> ReminderManager::tasks() const {
	QVector<ReminderConfig> result;
	for(const auto& id : _mTaskOrder) {
		const auto* runtime = runtimeFor(id);
		if(runtime)
			result.append(runtime->_config);
	}
	return result;
}

QString ReminderManager::taskName(const QString& taskId) const {
	const auto* runtime = runtimeFor(taskId);
	return runtime ? runtime->_config._name : taskId;
}

QString ReminderManager::taskUnit(const QString& taskId) const {
	const auto* runtime = runtimeFor(taskId);
	return runtime ? runtime->_config._unit : QString();
}

QString ReminderManager::notificationBody(const QString& taskId, int units) {
	const auto* runtime = runtimeFor(taskId);
	if(!runtime)
		return {};

	const auto task		   = runtime->_config;
	const auto scoreValue  = score();
	QString	   body		   = QStringLiteral("%1提醒：%2 %3。").arg(task._name).arg(units).arg(task._unit);
	const auto taskPhrase  = _mPhrases.phraseForTask(task._id, QStringLiteral("due"), scoreValue);
	const auto levelPhrase = _mPhrases.phraseForScore(scoreValue);
	body += QLatin1Char('\n') + taskPhrase;
	if(levelPhrase != taskPhrase)
		body += QLatin1Char('\n') + levelPhrase;
	body += QLatin1Char('\n') + shortDebtSummary();
	return body;
}

QString ReminderManager::notificationPreview(const QString& taskId) {
	const auto* runtime = runtimeFor(taskId);
	if(!runtime)
		return {};
	const int units = runtime->_pending ? runtime->_pendingUnits : runtime->_config.unitsPerEvent();
	return notificationBody(taskId, units);
}

QString ReminderManager::debtReport() {
	const auto sentence = buildDebtSentence();
	QString	   result	= sentence;
	if(score() > 0)
		result += QLatin1Char('\n') + QStringLiteral("兄弟债务分：%1（%2）").arg(score()).arg(displayLevel());
	result += QLatin1Char('\n') + _mPhrases.phraseForScore(score());
	return result;
}

QString ReminderManager::shortDebtSummary() const {
	if(score() <= 0)
		return QStringLiteral("当前无欠账。");

	QStringList clauses;
	for(const auto& id : _mTaskOrder) {
		const auto* runtime = runtimeFor(id);
		if(!runtime)
			continue;
		const auto count = _mLedger.outstandingCount(id);
		if(count <= 0)
			continue;
		clauses.append(QStringLiteral("%1 %2 次 / %3 %4")
						   .arg(runtime->_config._name)
						   .arg(count)
						   .arg(_mLedger.outstandingUnits(id))
						   .arg(runtime->_config._unit));
	}
	for(const auto& taskId : _mLedger.outstandingTaskIds()) {
		if(_mTaskOrder.contains(taskId))
			continue;
		const auto count = _mLedger.outstandingCount(taskId);
		if(count <= 0)
			continue;
		clauses.append(
			QStringLiteral("%1 %2 次 / %3 次").arg(taskId).arg(count).arg(_mLedger.outstandingUnits(taskId)));
	}
	if(clauses.isEmpty())
		return QStringLiteral("当前严重度：%1（%2 分）").arg(displayLevel()).arg(score());
	return QStringLiteral("当前欠：%1。严重度：%2（%3 分）")
		.arg(clauses.join(QStringLiteral("；")))
		.arg(displayLevel())
		.arg(score());
}

QString ReminderManager::buildDebtSentence() const {
	QStringList clauses;
	for(const auto& id : _mTaskOrder) {
		const auto* runtime = runtimeFor(id);
		if(!runtime)
			continue;
		const auto count = _mLedger.outstandingCount(id);
		if(count <= 0)
			continue;
		const auto units = _mLedger.outstandingUnits(id);
		if(id == QStringLiteral("water")) {
			clauses.append(QStringLiteral("欠下了 %1 次喝水，总计 %2 %3")
							   .arg(count)
							   .arg(units)
							   .arg(runtime->_config._unit));
		} else if(id == QStringLiteral("eye_break")) {
			clauses.append(QStringLiteral("欠下了 %1 次护眼休息，总计 %2 %3")
							   .arg(count)
							   .arg(units)
							   .arg(runtime->_config._unit));
		} else {
			clauses.append(QStringLiteral("欠下了 %1 次%2，总计 %3 %4")
							   .arg(count)
							   .arg(runtime->_config._name)
							   .arg(units)
							   .arg(runtime->_config._unit));
		}
	}

	for(const auto& taskId : _mLedger.outstandingTaskIds()) {
		if(_mTaskOrder.contains(taskId))
			continue;
		const auto count = _mLedger.outstandingCount(taskId);
		if(count <= 0)
			continue;
		clauses.append(QStringLiteral("欠下了 %1 次%2，总计 %3 次")
						   .arg(count)
						   .arg(taskId)
						   .arg(_mLedger.outstandingUnits(taskId)));
	}

	QString sentence;
	if(clauses.isEmpty())
		sentence = QStringLiteral("兄弟账本清白：当前没有欠账。");
	else
		sentence = QStringLiteral("你已经") + clauses.join(QStringLiteral("；")) + QStringLiteral("。");

	QStringList lifetime;
	for(const auto& id : _mTaskOrder) {
		const auto* runtime = runtimeFor(id);
		if(!runtime)
			continue;
		const auto lifetimeCount = _mLedger.lifetimeDebtCount(id);
		if(lifetimeCount <= _mLedger.outstandingCount(id))
			continue;
		lifetime.append(QStringLiteral("%1 累计 %2 次 / %3 %4，已还 %5 次")
							.arg(runtime->_config._name)
							.arg(lifetimeCount)
							.arg(_mLedger.lifetimeDebtUnits(id))
							.arg(runtime->_config._unit)
							.arg(_mLedger.repaymentCount(id)));
	}
	if(!lifetime.isEmpty())
		sentence += QLatin1Char('\n') + QStringLiteral("累计：") + lifetime.join(QStringLiteral("；"))
					+ QStringLiteral("。");
	return sentence;
}

QString ReminderManager::displayLevel() const { return levelDisplay(DebtLedger::levelForScore(score())); }

QString ReminderManager::actionPrompt() { return _mPhrases.actionPrompt(); }

QString ReminderManager::phraseConfigPath() const { return _mPhrases.configPath(); }

QString ReminderManager::phraseSourcePath() const { return _mPhrases.sourcePath(); }

QString ReminderManager::phraseLoadError() const {
	QString error = _mPhrases.errorString();
	if(!error.isEmpty()) {
		if(_mPhrases.errorLine() > 0)
			error += QStringLiteral("（第 %1 行）").arg(_mPhrases.errorLine());
		return error;
	}
	if(_mPhrases.usedFallback())
		return QStringLiteral("用户文案不可用，当前使用安装目录的只读模板。");
	return {};
}

bool ReminderManager::phrasesUsingFallback() const { return _mPhrases.usedFallback(); }

bool ReminderManager::reloadPhrases() {
	const bool ok = _mPhrases.reload();
	emit	   stateChanged();
	return ok;
}

bool ReminderManager::preparePhraseConfig(QString* error) {
	QString localError;
	if(!_mPhrases.ensureUserCopy(&localError)) {
		if(error)
			*error = localError;
		return false;
	}
	// Even when the existing user copy is invalid, the file must still be opened
	// so the user can repair it. Reload is best-effort here; reloadPhrases() is
	// the path that reports a hard failure.
	_mPhrases.reload();
	if(error)
		*error = _mPhrases.hasLoadError() ? phraseLoadError() : QString();
	emit stateChanged();
	return true;
}

void ReminderManager::onNotificationAction(uint notificationId, const QString& action) {
	const auto mapping = _mNotificationInstances.find(notificationId);
	if(mapping == _mNotificationInstances.end()) {
		if(action == QStringLiteral("ledger"))
			emit openLedgerRequested();
		return;
	}
	const auto instanceId = mapping.value();

	if(action == QStringLiteral("ledger")) {
		_mNotificationInstances.remove(notificationId);
		emit openLedgerRequested();
		return;
	}

	bool handled = false;
	for(const auto& id : _mTaskOrder) {
		auto* runtime = runtimeFor(id);
		if(!runtime || !runtime->_pending || runtime->_instanceId != instanceId)
			continue;
		if(action == QStringLiteral("complete") || action == QStringLiteral("drink"))
			handled = completeTask(id);
		else if(action == QStringLiteral("skip"))
			handled = skipTask(id);
		else if(action == QStringLiteral("snooze"))
			handled = snoozeTask(id);
		break;
	}

	// Keep the mapping on a failed write so the user can retry from the same
	// notification. Successful or stale actions drop it.
	if(handled)
		_mNotificationInstances.remove(notificationId);
}

void ReminderManager::onNotificationClosed(uint notificationId, uint reason) {
	Q_UNUSED(reason);
	_mNotificationInstances.remove(notificationId);
}

void ReminderManager::emitCountdown(const QDateTime& current) {
	if(_mBreaking) {
		emit countdownChanged(remainingTime(current, _mBreakEndsAt), false);
		return;
	}
	const auto* runtime = runtimeFor(_mBreakTaskId);
	if(runtime && runtime->_config._enabled) {
		emit countdownChanged(remainingTime(current, runtime->_nextDue), true);
		return;
	}
	emit countdownChanged(QTime(0, 0, 0), false);
}

bool ReminderManager::isSuppressed(const QDateTime& current) const {
	if(_mPausedUntil.isValid() && current < _mPausedUntil)
		return true;
	if(!_mQuietHoursEnabled)
		return false;

	const auto start = QTime::fromString(_mQuietHoursStart, QStringLiteral("HH:mm"));
	const auto end	 = QTime::fromString(_mQuietHoursEnd, QStringLiteral("HH:mm"));
	if(!start.isValid() || !end.isValid() || start == end)
		return false;

	const auto time = current.time();
	if(start < end)
		return time >= start && time < end;
	return time >= start || time < end;
}

bool ReminderManager::isBreakTask(const QString& taskId) const { return taskId == _mBreakTaskId; }

ReminderManager::Runtime* ReminderManager::runtimeFor(const QString& taskId) {
	auto it = _mTasks.find(taskId);
	return it == _mTasks.end() ? nullptr : &it.value();
}

const ReminderManager::Runtime* ReminderManager::runtimeFor(const QString& taskId) const {
	auto it = _mTasks.constFind(taskId);
	return it == _mTasks.constEnd() ? nullptr : &it.value();
}

void ReminderManager::emitError(const QString& message) {
	qWarning().noquote() << message;
	emit errorOccurred(message);
}

void ReminderManager::setStatusPhrase(const QString& phrase) {
	if(phrase.isEmpty())
		return;
	_mLastStatusPhrase = phrase;
	emit statusMessage(phrase);
}

void ReminderManager::onPhrasesChanged() {
	_mPhraseWarningEmitted = false;
	emit stateChanged();
}

void ReminderManager::onPhrasesReloadFailed(const QString& error) {
	if(!_mPhraseWarningEmitted) {
		emitError(error);
		_mPhraseWarningEmitted = true;
	}
	emit stateChanged();
}

bool ReminderManager::savePending() {
	auto fail = [this](const QString& message) {
		if(!_mPendingSaveErrorEmitted) {
			if(_mShuttingDown)
				qWarning().noquote() << message;
			else
				emitError(message);
			_mPendingSaveErrorEmitted = true;
		}
		return false;
	};

	QJsonArray pending;
	for(const auto& id : _mTaskOrder) {
		const auto* runtime = runtimeFor(id);
		if(!runtime || !runtime->_pending)
			continue;
		QJsonObject object;
		object[QStringLiteral("task")]	   = id;
		object[QStringLiteral("instance")] = runtime->_instanceId;
		object[QStringLiteral("units")]	   = runtime->_pendingUnits;
		object[QStringLiteral("weight")]   = runtime->_pendingWeight;
		object[QStringLiteral("due_at")]   = formatDateTime(runtime->_pendingDueAt);
		object[QStringLiteral("deadline")] = formatDateTime(runtime->_pendingDeadline);
		object[QStringLiteral("snooze")]   = runtime->_snoozeCount;
		pending.append(object);
	}

	const auto contents = QJsonDocument(QJsonObject{
											{QStringLiteral("version"), 1		 },
											{QStringLiteral("pending"), pending},
	})
							  .toJson(QJsonDocument::Indented);
	const auto info = QFileInfo(_mPendingStorePath);
	if(!QDir().mkpath(info.absolutePath()))
		return fail(QStringLiteral("无法创建待处理提醒目录：%1").arg(info.absolutePath()));
	QSaveFile file(_mPendingStorePath);
	if(!file.open(QIODevice::WriteOnly))
		return fail(QStringLiteral("无法写入待处理提醒：%1").arg(_mPendingStorePath));
	if(file.write(contents) != contents.size())
		return fail(QStringLiteral("待处理提醒写入不完整：%1").arg(_mPendingStorePath));
	if(!file.commit())
		return fail(QStringLiteral("待处理提醒提交失败：%1").arg(_mPendingStorePath));
	_mPendingSaveErrorEmitted = false;
	return true;
}

void ReminderManager::loadPending() {
	QFile file(_mPendingStorePath);
	if(!file.exists())
		return;
	if(!file.open(QIODevice::ReadOnly)) {
		emitError(QStringLiteral("无法读取待处理提醒：%1").arg(_mPendingStorePath));
		return;
	}
	const auto		bytes = file.readAll();
	QJsonParseError parseError;
	const auto		document = QJsonDocument::fromJson(bytes, &parseError);
	if(parseError.error != QJsonParseError::NoError || !document.isObject()) {
		const auto backup = pendingBackupPath();
		if(QFile::copy(_mPendingStorePath, backup))
			emitError(QStringLiteral("待处理提醒文件损坏，已备份到：%1").arg(backup));
		else
			emitError(QStringLiteral("待处理提醒文件损坏，备份失败：%1").arg(_mPendingStorePath));
		return;
	}

	const auto root = document.object();
	if(root.contains(QStringLiteral("pending")) && !root.value(QStringLiteral("pending")).isArray()) {
		const auto backup = pendingBackupPath();
		if(QFile::copy(_mPendingStorePath, backup))
			emitError(QStringLiteral("待处理提醒格式损坏，已备份到：%1").arg(backup));
		return;
	}

	const auto current = now();
	for(const auto& value : root.value(QStringLiteral("pending")).toArray()) {
		if(!value.isObject())
			continue;
		const auto object  = value.toObject();
		const auto taskId  = object.value(QStringLiteral("task")).toString();
		auto*	   runtime = runtimeFor(taskId);
		if(!runtime || !runtime->_config._enabled || runtime->_pending)
			continue;

		const auto dueAt	  = parseDateTime(object.value(QStringLiteral("due_at")));
		auto	   deadline	  = parseDateTime(object.value(QStringLiteral("deadline")));
		const auto units	  = object.value(QStringLiteral("units")).toInt(runtime->_config.unitsPerEvent());
		const auto weight	  = object.value(QStringLiteral("weight")).toInt(runtime->_config._weight);
		const auto instanceId = object.value(QStringLiteral("instance")).toString();
		if(!dueAt.isValid() || !deadline.isValid() || instanceId.isEmpty())
			continue;

		if(_mLedger.hasEventForInstance(instanceId)) {
			// The event is already committed; drop the stale pending entry.
			runtime->_nextDue = dueAt.addSecs(std::max(1, runtime->_config._intervalMinutes) * 60);
			if(runtime->_nextDue <= current)
				runtime->_nextDue = current.addSecs(std::max(1, runtime->_config._intervalMinutes) * 60);
			continue;
		}

		if(isSuppressed(current)) {
			// Never notify or charge for a pending reminder while the user has
			// paused reminders or is inside quiet hours.
			runtime->_nextDue = current.addSecs(std::max(1, runtime->_config._intervalMinutes) * 60);
			continue;
		}

		if(deadline <= current) {
			if(_mLedgerWritable
			   && _mLedger.recordDebt(taskId,
									  DebtLedger::EventKind::Missed,
									  std::max(1, units),
									  std::max(1, weight),
									  instanceId)) {
				runtime->_nextDue = dueAt.addSecs(std::max(1, runtime->_config._intervalMinutes) * 60);
				if(runtime->_nextDue <= current)
					runtime->_nextDue = current.addSecs(std::max(1, runtime->_config._intervalMinutes) * 60);
				continue;
			}
			// Keep the pending reminder rather than losing it when the ledger is
			// temporarily unwritable. It will be retried on the next poll.
			runtime->_pending		  = true;
			runtime->_instanceId	  = instanceId;
			runtime->_pendingUnits	  = std::max(1, units);
			runtime->_pendingWeight	  = std::max(1, weight);
			runtime->_pendingDueAt	  = dueAt;
			runtime->_pendingDeadline = current.addSecs(K_MISSED_RETRY_SECONDS);
			runtime->_snoozeCount	  = object.value(QStringLiteral("snooze")).toInt(0);
			continue;
		}

		runtime->_pending		  = true;
		runtime->_instanceId	  = instanceId;
		runtime->_pendingUnits	  = std::max(1, units);
		runtime->_pendingWeight	  = std::max(1, weight);
		runtime->_pendingDueAt	  = dueAt;
		runtime->_pendingDeadline = deadline;
		runtime->_snoozeCount	  = object.value(QStringLiteral("snooze")).toInt(0);
		notifyPending(*runtime);
	}
	savePending();
}
