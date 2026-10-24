#include "phrase_catalog.h"

#include "debt_ledger.h"
#include "settings_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>

#include <utility>

namespace {
const QStringList K_LEVELS{
	QStringLiteral("no_debt"),
	QStringLiteral("mild"),
	QStringLiteral("moderate"),
	QStringLiteral("severe"),
	QStringLiteral("critical"),
};

const QStringList K_EVENTS{
	QStringLiteral("due"),
	QStringLiteral("skipped"),
	QStringLiteral("missed"),
	QStringLiteral("completed"),
	QStringLiteral("snoozed"),
};

QString stripBom(const QString& text) {
	if(!text.isEmpty() && text.front() == QChar(0xFEFF))
		return text.mid(1);
	return text;
}

bool isKnownLevel(const QString& name) { return K_LEVELS.contains(name); }

bool isKnownEvent(const QString& name) { return K_EVENTS.contains(name); }

QString emergencyAction() { return QStringLiteral("真的？"); }

bool parseFileImpl(const QString&								path,
				   QHash<QString, QStringList>&					levels,
				   QHash<QString, QHash<QString, QStringList>>& tasks,
				   QStringList&									actions,
				   QString&										error,
				   int&											errorLine);
}  // namespace

PhraseCatalog::PhraseCatalog(QObject* parent)
	: QObject(parent) {
	_mReloadTimer.setSingleShot(true);
	_mReloadTimer.setInterval(300);
	connect(&_mReloadTimer, &QTimer::timeout, this, &PhraseCatalog::reloadNow);
	connect(&_mWatcher, &QFileSystemWatcher::fileChanged, this, &PhraseCatalog::onWatchTriggered);
	connect(&_mWatcher, &QFileSystemWatcher::directoryChanged, this, &PhraseCatalog::onWatchTriggered);
}

PhraseCatalog::~PhraseCatalog() = default;

QString PhraseCatalog::userConfigPath() {
	return QDir(SettingsManager::configDir()).filePath(QStringLiteral("phrases.conf"));
}

QString PhraseCatalog::installedConfigPath() {
	const auto env = qEnvironmentVariable("BROHEALTH_PHRASES_CONF");
	if(!env.isEmpty())
		return env;

	const auto appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
#if defined(BROHEALTH_BUILD_BINARY_DIR) && defined(BROHEALTH_BUNDLED_PHRASES_CONF)
	const auto buildDir = QDir::cleanPath(QStringLiteral(BROHEALTH_BUILD_BINARY_DIR));
	if(appDir == buildDir || appDir.startsWith(buildDir + QDir::separator()))
		return QStringLiteral(BROHEALTH_BUNDLED_PHRASES_CONF);
#endif

	// Prefer a location relative to the executable so a custom install prefix
	// (or an AppImage-like tree) does not silently read /usr/share.
	const auto relative = QDir::cleanPath(appDir + QStringLiteral("/../share/brohealth/phrases.conf"));
	if(QFile::exists(relative))
		return relative;

#ifdef BROHEALTH_INSTALLED_PHRASES_CONF
	return QStringLiteral(BROHEALTH_INSTALLED_PHRASES_CONF);
#else
#	ifdef BROHEALTH_BUNDLED_PHRASES_CONF
	return QStringLiteral(BROHEALTH_BUNDLED_PHRASES_CONF);
#	else
	return QStringLiteral("/usr/share/brohealth/phrases.conf");
#	endif
#endif
}

QString PhraseCatalog::missingConfigMessage() { return QStringLiteral("文案配置文件不存在或格式不正确"); }

QString PhraseCatalog::levelForScore(int score) { return DebtLedger::levelForScore(score); }

bool PhraseCatalog::ensureUserCopy(QString* error) {
	const auto env = qEnvironmentVariable("BROHEALTH_PHRASES_CONF");
	if(!env.isEmpty()) {
		if(QFile::exists(env))
			return true;
		if(error)
			*error = QStringLiteral("环境变量指定的文案文件不存在：%1").arg(env);
		return false;
	}

	const auto target = userConfigPath();
	if(QFile::exists(target))
		return true;

	const auto source = installedConfigPath();
	QString	   validationError;
	int		   validationLine = 0;
	if(!QFile::exists(source)) {
		if(error)
			*error = missingConfigMessage() + QStringLiteral("：%1").arg(source);
		return false;
	}
	if(!QFileInfo(source).isReadable()) {
		if(error)
			*error = QStringLiteral("无法读取安装目录的文案模板：%1").arg(source);
		return false;
	}
	if(!validateFile(source, nullptr, &validationError, &validationLine)) {
		if(error) {
			*error = QStringLiteral("安装目录的文案模板无效：%1（第 %2 行：%3）")
						 .arg(source)
						 .arg(validationLine)
						 .arg(validationError);
		}
		return false;
	}

	if(!QDir().mkpath(QFileInfo(target).absolutePath())) {
		if(error)
			*error = QStringLiteral("无法创建用户配置目录：%1").arg(QFileInfo(target).absolutePath());
		return false;
	}
	if(!QFile::copy(source, target)) {
		if(error)
			*error = QStringLiteral("无法创建用户文案副本：%1").arg(target);
		return false;
	}
	return true;
}

bool PhraseCatalog::loadActive(QString* error) {
	const auto envPath = qEnvironmentVariable("BROHEALTH_PHRASES_CONF");
	if(!envPath.isEmpty()) {
		QString	   envError;
		int		   envLine = 0;
		const bool ok	   = load(envPath, &envError, &envLine);
		if(error)
			*error = envError;
		if(!ok)
			emit reloadFailed(envError);
		return ok;
	}

	QString userError;
	if(!ensureUserCopy(&userError)) {
		// A missing or broken user copy is not fatal: the installed template may
		// still be usable.
	}

	const auto userPath = userConfigPath();
	if(!QFile::exists(userPath)) {
		// No user copy (read-only install, unusual permissions, or environment
		// override); attempt the template directly without discarding a last
		// known good catalog.
		Data	installedData;
		QString installedError;
		int		installedLine = 0;
		if(parseFile(installedConfigPath(), installedData, installedError, installedLine)) {
			applyData(std::move(installedData));
			_mConfigPath   = userPath;
			_mSourcePath   = installedConfigPath();
			_mUsedFallback = true;
			_mValid		   = true;
			_mError		   = userError;
			_mErrorLine	   = 0;
			watchCurrent();
			if(error)
				*error = userError;
			return true;
		}
		if(_mValid) {
			_mConfigPath = userPath;
			_mError		 = installedError;
			_mErrorLine	 = installedLine;
			watchCurrent();
			if(error)
				*error = installedError;
			emit reloadFailed(installedError);
			return true;
		}
		_mConfigPath = userPath;
		_mSourcePath = installedConfigPath();
		_mValid		 = false;
		_mError		 = installedError;
		_mErrorLine	 = installedLine;
		watchCurrent();
		if(error)
			*error = installedError;
		emit reloadFailed(installedError);
		return false;
	}

	Data	data;
	int		line = 0;
	QString parseError;
	if(parseFile(userPath, data, parseError, line)) {
		applyData(std::move(data));
		_mConfigPath   = userPath;
		_mSourcePath   = userPath;
		_mValid		   = true;
		_mUsedFallback = false;
		_mError.clear();
		_mErrorLine = 0;
		watchCurrent();
		if(error)
			error->clear();
		return true;
	}

	// Keep last-known-good data first. Only a completely fresh start (no usable
	// catalog yet) should fall back to the installed template.
	if(_mValid) {
		_mError		= parseError;
		_mErrorLine = line;
		watchCurrent();
		if(error)
			*error = parseError;
		emit reloadFailed(parseError);
		return true;
	}

	QString installedError;
	int		installedLine = 0;
	Data	fallbackData;
	if(parseFile(installedConfigPath(), fallbackData, installedError, installedLine)) {
		applyData(std::move(fallbackData));
		_mConfigPath   = userPath;
		_mSourcePath   = installedConfigPath();
		_mUsedFallback = true;
		_mValid		   = true;
		_mError		   = parseError;
		_mErrorLine	   = line;
		watchCurrent();
		if(error)
			*error = parseError;
		emit reloadFailed(parseError);
		return true;
	}

	_mConfigPath = userPath;
	_mSourcePath = installedConfigPath();
	_mValid		 = false;
	_mError		 = parseError;
	_mErrorLine	 = line;
	watchCurrent();
	if(error)
		*error = parseError;
	emit reloadFailed(parseError);
	return false;
}

bool PhraseCatalog::load(const QString& path, QString* error, int* errorLine) {
	Data	data;
	int		line = 0;
	QString parseError;
	if(!parseFile(path, data, parseError, line)) {
		resetData();
		_mConfigPath = path;
		_mSourcePath = path;
		_mValid		 = false;
		_mError		 = parseError;
		_mErrorLine	 = line;
		if(error)
			*error = parseError;
		if(errorLine)
			*errorLine = line;
		return false;
	}

	applyData(std::move(data));
	_mConfigPath   = path;
	_mSourcePath   = path;
	_mValid		   = true;
	_mUsedFallback = false;
	_mError.clear();
	_mErrorLine = 0;
	watchCurrent();
	if(error)
		error->clear();
	if(errorLine)
		*errorLine = 0;
	return true;
}

bool PhraseCatalog::reload() {
	QString	   error;
	const bool ok = loadActive(&error);
	watchCurrent();
	if(ok)
		emit changed();
	return ok;
}

void PhraseCatalog::setAutoReloadEnabled(bool enabled) {
	_mAutoReload = enabled;
	if(!enabled) {
		_mReloadTimer.stop();
		if(!_mWatcher.files().isEmpty() || !_mWatcher.directories().isEmpty())
			_mWatcher.removePaths(_mWatcher.files() + _mWatcher.directories());
	} else {
		watchCurrent();
	}
}

int PhraseCatalog::phraseCount(const QString& level) const { return _mLevels.value(level).size(); }

int PhraseCatalog::taskPhraseCount(const QString& taskId, const QString& event) const {
	return _mTaskPhrases.value(taskId).value(event).size();
}

int PhraseCatalog::totalPhraseCount() const {
	int total = _mActions.size();
	for(const auto& level : _mLevels)
		total += level.size();
	for(const auto& task : _mTaskPhrases) {
		for(const auto& pool : task)
			total += pool.size();
	}
	return total;
}

QStringList PhraseCatalog::levels() const {
	QStringList result;
	for(const auto& level : K_LEVELS) {
		if(!_mLevels.value(level).isEmpty())
			result.append(level);
	}
	return result;
}

QString PhraseCatalog::phraseForScore(int score) {
	const auto level = DebtLedger::levelForScore(score);
	const auto key	 = QStringLiteral("level:") + level;
	const auto pool	 = _mLevels.value(level);
	if(pool.isEmpty())
		return emergencyPhrase(score);
	return pickFrom(key, pool);
}

QString PhraseCatalog::phraseForTask(const QString& taskId, const QString& event, int score) {
	auto pool = taskPool(taskId, event);
	if(pool.isEmpty() && event == QStringLiteral("missed"))
		pool = taskPool(taskId, QStringLiteral("skipped"));
	if(pool.isEmpty())
		return phraseForScore(score);
	return pickFrom(QStringLiteral("task:") + taskId + QLatin1Char('.') + event, pool);
}

QString PhraseCatalog::actionPrompt() {
	if(_mActions.isEmpty())
		return emergencyAction();
	return pickFrom(QStringLiteral("actions"), _mActions);
}

void PhraseCatalog::onWatchTriggered() {
	if(!_mAutoReload)
		return;
	// The config directory also contains settings.json, the ledger and pending
	// state. Only reparse when the phrase file itself actually changed.
	const bool changed = fileStampsChanged();
	_mInWatchHandler   = true;
	watchCurrent();
	_mInWatchHandler = false;
	if(!changed)
		return;
	_mReloadTimer.start();
}

void PhraseCatalog::reloadNow() {
	QString	   error;
	const bool ok = loadActive(&error);
	watchCurrent();
	// A successful reload must notify the UI even when the path did not change;
	// the file contents are what the user edited.
	if(ok)
		emit changed();
}

void PhraseCatalog::watchCurrent() {
	if(!_mAutoReload)
		return;

	QStringList paths = _mWatcher.files() + _mWatcher.directories();
	if(!paths.isEmpty())
		_mWatcher.removePaths(paths);

	const auto watchedPath = _mConfigPath.isEmpty() ? userConfigPath() : _mConfigPath;
	if(QFile::exists(watchedPath))
		_mWatcher.addPath(watchedPath);
	const auto userDir = QFileInfo(watchedPath).absolutePath();
	if(QDir(userDir).exists())
		_mWatcher.addPath(userDir);

	if(_mUsedFallback && QFile::exists(installedConfigPath()))
		_mWatcher.addPath(installedConfigPath());

	if(!_mInWatchHandler)
		rememberStamps();
}

void PhraseCatalog::rememberStamps() {
	auto remember = [](const QString& path, QDateTime& stamp, qint64& size) {
		const QFileInfo info(path);
		if(info.exists()) {
			stamp = info.lastModified();
			size  = info.size();
		} else {
			stamp = QDateTime();
			size  = -1;
		}
	};
	remember(_mConfigPath, _mUserStamp, _mUserSize);
	if(_mSourcePath != _mConfigPath)
		remember(_mSourcePath, _mSourceStamp, _mSourceSize);
	else {
		_mSourceStamp = _mUserStamp;
		_mSourceSize  = _mUserSize;
	}
}

bool PhraseCatalog::fileStampsChanged() const {
	auto changed = [](const QString& path, const QDateTime& stamp, qint64 size) {
		const QFileInfo info(path);
		if(!info.exists())
			return stamp.isValid() || size >= 0;
		return info.lastModified() != stamp || info.size() != size;
	};
	if(changed(_mConfigPath, _mUserStamp, _mUserSize))
		return true;
	if(_mSourcePath != _mConfigPath && changed(_mSourcePath, _mSourceStamp, _mSourceSize))
		return true;
	return false;
}

void PhraseCatalog::applyData(Data&& data) {
	_mLevels	  = std::move(data.levels);
	_mTaskPhrases = std::move(data.tasks);
	_mActions	  = std::move(data.actions);
	_mLastPicked.clear();
}

void PhraseCatalog::resetData() {
	_mLevels.clear();
	_mTaskPhrases.clear();
	_mActions.clear();
	_mLastPicked.clear();
}

QString PhraseCatalog::pickFrom(const QString& key, const QStringList& pool) {
	if(pool.isEmpty())
		return {};
	int index = QRandomGenerator::global()->bounded(pool.size());
	if(pool.size() > 1 && _mLastPicked.value(key) == pool.at(index))
		index = (index + 1 + QRandomGenerator::global()->bounded(pool.size() - 1)) % pool.size();
	const auto selected = pool.at(index);
	_mLastPicked.insert(key, selected);
	return selected;
}

QString PhraseCatalog::emergencyPhrase(int score) const {
	if(score >= 20)
		return QStringLiteral("兄弟只盼着你好，先喝口水吧。");
	if(score >= 10)
		return QStringLiteral("账本已经不好看了，兄弟递来一杯水。");
	if(score > 0)
		return QStringLiteral("欠得不多，喝一口就能还。");
	return QStringLiteral("账本清白，兄弟很满意。");
}

QStringList PhraseCatalog::taskPool(const QString& taskId, const QString& event) const {
	return _mTaskPhrases.value(taskId).value(event);
}

bool PhraseCatalog::validateFile(const QString& path, QString* report, QString* error, int* errorLine) {
	Data	data;
	int		line = 0;
	QString parseError;
	if(!parseFileImpl(path, data.levels, data.tasks, data.actions, parseError, line)) {
		if(error)
			*error = parseError;
		if(errorLine)
			*errorLine = line;
		return false;
	}
	if(error)
		error->clear();
	if(errorLine)
		*errorLine = 0;

	if(!report)
		return true;

	QStringList lines;
	lines.append(QStringLiteral("文件：%1").arg(path));
	int total = 0;
	for(const auto& level : K_LEVELS) {
		const int count = data.levels.value(level).size();
		total += count;
		lines.append(QStringLiteral("%1：%2 条").arg(level).arg(count));
	}
	lines.append(QStringLiteral("actions：%1 条").arg(data.actions.size()));
	total += data.actions.size();
	for(auto it = data.tasks.cbegin(); it != data.tasks.cend(); ++it) {
		for(auto pool = it.value().cbegin(); pool != it.value().cend(); ++pool) {
			total += pool.value().size();
			lines.append(
				QStringLiteral("tasks.%1.%2：%3 条").arg(it.key(), pool.key()).arg(pool.value().size()));
		}
	}
	lines.append(QStringLiteral("合计：%1 条").arg(total));

	QHash<QString, int> seen;
	QSet<QString>		curly;
	int					longCount = 0;
	for(const auto& level : K_LEVELS) {
		for(const auto& phrase : data.levels.value(level)) {
			seen[phrase]++;
			if(phrase.contains(QChar(0x201C)) || phrase.contains(QChar(0x201D))
			   || phrase.contains(QChar(0x2018)) || phrase.contains(QChar(0x2019)))
				curly.insert(phrase);
			if(phrase.size() > 120)
				longCount++;
		}
	}
	for(const auto& phrase : data.actions) {
		seen[phrase]++;
		if(phrase.contains(QChar(0x201C)) || phrase.contains(QChar(0x201D)))
			curly.insert(phrase);
		if(phrase.size() > 120)
			longCount++;
	}
	for(const auto& task : data.tasks) {
		for(const auto& pool : task) {
			for(const auto& phrase : pool) {
				seen[phrase]++;
				if(phrase.contains(QChar(0x201C)) || phrase.contains(QChar(0x201D)))
					curly.insert(phrase);
				if(phrase.size() > 120)
					longCount++;
			}
		}
	}
	int duplicateCount = 0;
	for(auto it = seen.cbegin(); it != seen.cend(); ++it) {
		if(it.value() > 1)
			duplicateCount += it.value() - 1;
	}
	lines.append(QStringLiteral("重复行：%1").arg(duplicateCount));
	lines.append(QStringLiteral("中文弯引号行：%1").arg(curly.size()));
	lines.append(QStringLiteral("超长行（>120）：%1").arg(longCount));

	if(report)
		*report = lines.join(QLatin1Char('\n'));
	return true;
}

namespace {
bool parseFileImpl(const QString&								path,
				   QHash<QString, QStringList>&					levels,
				   QHash<QString, QHash<QString, QStringList>>& tasks,
				   QStringList&									actions,
				   QString&										error,
				   int&											errorLine) {
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly)) {
		error	  = QStringLiteral("无法读取文案文件：%1").arg(path);
		errorLine = 0;
		return false;
	}
	const auto	   bytes = file.readAll();
	QStringDecoder decoder(QStringDecoder::Utf8);
	const QString  text = stripBom(decoder.decode(bytes));
	if(decoder.hasError()) {
		error	  = QStringLiteral("文案文件不是合法的 UTF-8");
		errorLine = 0;
		return false;
	}

	levels.clear();
	tasks.clear();
	actions.clear();

	enum class Section {
		None,
		Level,
		Actions,
		Task
	};
	Section section = Section::None;
	QString levelName;
	QString taskId;
	QString taskEvent;

	const auto lines = text.split(QLatin1Char('\n'));
	for(int index = 0; index < lines.size(); ++index) {
		QString line = lines.at(index);
		if(line.endsWith(QLatin1Char('\r')))
			line.chop(1);
		const auto trimmed = line.trimmed();
		if(trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
			continue;

		if(trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
			const auto						name = trimmed.mid(1, trimmed.size() - 2).trimmed();
			static const QRegularExpression taskPattern(
				QStringLiteral("^tasks\\.([A-Za-z0-9_]+)\\.([A-Za-z0-9_]+)$"));
			if(name == QStringLiteral("actions")) {
				section = Section::Actions;
				continue;
			}
			if(isKnownLevel(name)) {
				section	  = Section::Level;
				levelName = name;
				continue;
			}
			const auto match = taskPattern.match(name);
			if(match.hasMatch()) {
				taskId	  = match.captured(1);
				taskEvent = match.captured(2);
				if(!isKnownEvent(taskEvent)) {
					error	  = QStringLiteral("未知的任务事件：%1").arg(name);
					errorLine = index + 1;
					return false;
				}
				section = Section::Task;
				continue;
			}
			error	  = QStringLiteral("未知的文案分组：%1").arg(name);
			errorLine = index + 1;
			return false;
		}

		if(trimmed.startsWith(QLatin1Char('['))) {
			error	  = QStringLiteral("分组行必须以 ] 结束：%1").arg(trimmed);
			errorLine = index + 1;
			return false;
		}

		if(section == Section::None) {
			error	  = QStringLiteral("文案内容必须位于 [section] 之下");
			errorLine = index + 1;
			return false;
		}
		QString phrase = trimmed;
		if(section == Section::Level)
			levels[levelName].append(phrase);
		else if(section == Section::Actions)
			actions.append(phrase);
		else if(section == Section::Task)
			tasks[taskId][taskEvent].append(phrase);
	}

	for(const auto& level : K_LEVELS) {
		if(levels.value(level).isEmpty()) {
			error	  = QStringLiteral("文案分组 [%1] 缺失或为空").arg(level);
			errorLine = 0;
			return false;
		}
	}
	if(actions.isEmpty()) {
		error	  = QStringLiteral("文案分组 [actions] 缺失或为空");
		errorLine = 0;
		return false;
	}
	for(auto it = tasks.cbegin(); it != tasks.cend(); ++it) {
		for(auto pool = it.value().cbegin(); pool != it.value().cend(); ++pool) {
			if(pool.value().isEmpty()) {
				error	  = QStringLiteral("任务分组 tasks.%1.%2 为空").arg(it.key(), pool.key());
				errorLine = 0;
				return false;
			}
		}
	}
	return true;
}
}  // namespace

bool PhraseCatalog::parseFile(const QString& path, Data& data, QString& error, int& errorLine) {
	return parseFileImpl(path, data.levels, data.tasks, data.actions, error, errorLine);
}
