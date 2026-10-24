#include "debt_ledger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
const QString K_WATER_TASK	   = QStringLiteral("water");
const QString K_BREAK_TASK	   = QStringLiteral("eye_break");
constexpr int K_SCHEMA_VERSION = 2;

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

int defaultWeightFor(const QString& taskId) {
	if(taskId == K_BREAK_TASK || taskId == QStringLiteral("break"))
		return 2;
	return 1;
}

QString backupPathFor(const QString& path) {
	const auto info		 = QFileInfo(path);
	auto	   candidate = info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
					 + QStringLiteral(".corrupt-")
					 + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"))
					 + QStringLiteral(".json");
	int suffix = 1;
	while(QFile::exists(candidate))
		candidate = info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
					+ QStringLiteral(".corrupt-")
					+ QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"))
					+ QStringLiteral("-") + QString::number(suffix++) + QStringLiteral(".json");
	return candidate;
}
}  // namespace

DebtLedger::DebtLedger(QString filePath)
	: _mFilePath(std::move(filePath)) {}

DebtLedger::LoadResult DebtLedger::load() {
	LoadResult result;
	_mEvents.clear();
	_mWritable = true;
	if(_mFilePath.isEmpty()) {
		result._ok = true;
		return result;
	}

	QFile file(_mFilePath);
	if(!file.exists())
		return result;
	if(!file.open(QIODevice::ReadOnly)) {
		_mWritable	  = false;
		result._ok	  = false;
		result._error = QStringLiteral("无法读取兄弟账本：%1").arg(_mFilePath);
		return result;
	}

	const auto contents = file.readAll();
	file.close();

	QJsonParseError parseError;
	const auto		document = QJsonDocument::fromJson(contents, &parseError);
	if(parseError.error != QJsonParseError::NoError || !document.isObject()) {
		result._error = QStringLiteral("兄弟账本不是有效的 JSON：%1").arg(parseError.errorString());
	} else {
		const auto root		   = document.object();
		const int  version	   = root.value(QStringLiteral("version")).toInt(1);
		const auto eventValues = root.value(QStringLiteral("events"));
		if(version > K_SCHEMA_VERSION) {
			// A newer schema is not corrupt; it belongs to a newer program. Do
			// not back it up or rewrite it, and refuse all writes.
			_mWritable = false;
			result._ok = false;
			result._error = QStringLiteral("兄弟账本版本 %1 高于当前程序支持的版本 %2；账本保持只读。")
								.arg(version)
								.arg(K_SCHEMA_VERSION);
			return result;
		}
		if(!eventValues.isArray()) {
			result._error = QStringLiteral("兄弟账本缺少 events 数组。");
		} else {
			QList<Event> loaded;
			bool		 valid = true;
			for(const auto& value : eventValues.toArray()) {
				if(!value.isObject()) {
					valid = false;
					break;
				}
				const auto object = value.toObject();
				Event	   event;
				event._id	  = object.value(QStringLiteral("id")).toString();
				event._taskId = object.value(QStringLiteral("task")).toString();
				// The first implementation called this task "break"; keep old
				// ledgers working after the task was renamed to "eye_break".
				if(event._taskId == QStringLiteral("break"))
					event._taskId = K_BREAK_TASK;
				event._units	  = object.value(QStringLiteral("units")).toInt();
				event._instanceId = object.value(QStringLiteral("instance")).toString();
				event._createdAt  = QDateTime::fromString(
					 object.value(QStringLiteral("created_at")).toString(), Qt::ISODateWithMs);
				if(event._id.isEmpty())
					event._id = newId();
				if(version <= 1) {
					event._kind	  = EventKind::Skipped;
					event._weight = defaultWeightFor(event._taskId);
				} else {
					bool kindOk	  = false;
					event._kind	  = kindFromName(object.value(QStringLiteral("kind")).toString(), &kindOk);
					event._weight = object.value(QStringLiteral("weight")).toInt(0);
					event._repaidEventId = object.value(QStringLiteral("repaid_event_id")).toString();
					if(!kindOk) {
						valid = false;
						break;
					}
				}
				if(event._taskId.isEmpty() || event._units <= 0 || !event._createdAt.isValid()) {
					valid = false;
					break;
				}
				if(event._kind == EventKind::Repayment) {
					if(event._repaidEventId.isEmpty()) {
						valid = false;
						break;
					}
				} else if(event._kind == EventKind::Completed) {
					event._weight = 0;
				} else if(event._weight <= 0) {
					valid = false;
					break;
				}
				loaded.append(event);
			}
			if(valid) {
				_mEvents   = std::move(loaded);
				_mWritable = true;
				return result;
			}
			result._error = QStringLiteral("兄弟账本中存在无法识别的事件。");
		}
	}

	// Back up the damaged bytes before allowing the application to continue with
	// an empty ledger. If the backup cannot be made, refuse to operate so the
	// next save cannot destroy the only copy.
	const auto backup = backupPathFor(_mFilePath);
	if(QFile::copy(_mFilePath, backup)) {
		_mWritable		   = true;
		result._ok		   = true;
		result._recovered  = true;
		result._backupPath = backup;
		return result;
	}
	_mWritable = false;
	result._ok = false;
	result._error += QStringLiteral(" 无法创建备份，账本保持只读。");
	return result;
}

bool DebtLedger::recordDebt(const QString& taskId,
							EventKind	   kind,
							int			   units,
							int			   weight,
							const QString& instanceId) {
	if(!_mWritable || taskId.isEmpty() || (kind != EventKind::Skipped && kind != EventKind::Missed)
	   || units <= 0 || weight <= 0)
		return false;

	Event event;
	event._id		  = newId();
	event._taskId	  = taskId;
	event._kind		  = kind;
	event._units	  = units;
	event._weight	  = weight;
	event._instanceId = instanceId;
	event._createdAt  = QDateTime::currentDateTimeUtc();
	_mEvents.append(event);
	if(save())
		return true;
	_mEvents.removeLast();
	return false;
}

bool DebtLedger::recordCompletion(const QString& taskId, int units, const QString& instanceId) {
	if(!_mWritable || taskId.isEmpty() || units <= 0)
		return false;

	Event event;
	event._id		  = newId();
	event._taskId	  = taskId;
	event._kind		  = EventKind::Completed;
	event._units	  = units;
	event._weight	  = 0;
	event._instanceId = instanceId;
	event._createdAt  = QDateTime::currentDateTimeUtc();
	_mEvents.append(event);
	if(save())
		return true;
	_mEvents.removeLast();
	return false;
}

bool DebtLedger::repayOne(const QString& taskId) {
	if(!_mWritable || taskId.isEmpty())
		return false;

	QSet<QString> repaid;
	for(const auto& event : _mEvents) {
		if(event._kind == EventKind::Repayment && !event._repaidEventId.isEmpty())
			repaid.insert(event._repaidEventId);
	}

	for(const auto& debt : _mEvents) {
		if(debt._taskId != taskId || debt._kind == EventKind::Completed || debt._kind == EventKind::Repayment
		   || repaid.contains(debt._id))
			continue;

		Event repayment;
		repayment._id			 = newId();
		repayment._taskId		 = taskId;
		repayment._kind			 = EventKind::Repayment;
		repayment._units		 = debt._units;
		repayment._weight		 = debt._weight;
		repayment._repaidEventId = debt._id;
		repayment._createdAt	 = QDateTime::currentDateTimeUtc();
		_mEvents.append(repayment);
		if(save())
			return true;
		_mEvents.removeLast();
		return false;
	}
	return false;
}

bool DebtLedger::hasEventForInstance(const QString& instanceId) const {
	if(instanceId.isEmpty())
		return false;
	for(const auto& event : _mEvents) {
		if(event._instanceId == instanceId)
			return true;
	}
	return false;
}

int DebtLedger::outstandingCount(const QString& taskId) const {
	QSet<QString> repaid;
	for(const auto& event : _mEvents) {
		if(event._kind == EventKind::Repayment)
			repaid.insert(event._repaidEventId);
	}
	return static_cast<int>(std::count_if(_mEvents.cbegin(), _mEvents.cend(), [&](const Event& event) {
		return event._taskId == taskId
			   && (event._kind == EventKind::Skipped || event._kind == EventKind::Missed)
			   && !repaid.contains(event._id);
	}));
}

qint64 DebtLedger::outstandingUnits(const QString& taskId) const {
	QSet<QString> repaid;
	for(const auto& event : _mEvents) {
		if(event._kind == EventKind::Repayment)
			repaid.insert(event._repaidEventId);
	}
	qint64 total = 0;
	for(const auto& event : _mEvents) {
		if(event._taskId == taskId && (event._kind == EventKind::Skipped || event._kind == EventKind::Missed)
		   && !repaid.contains(event._id))
			total += event._units;
	}
	return total;
}

int DebtLedger::lifetimeDebtCount(const QString& taskId) const {
	return static_cast<int>(std::count_if(_mEvents.cbegin(), _mEvents.cend(), [&](const Event& event) {
		return event._taskId == taskId
			   && (event._kind == EventKind::Skipped || event._kind == EventKind::Missed);
	}));
}

qint64 DebtLedger::lifetimeDebtUnits(const QString& taskId) const {
	qint64 total = 0;
	for(const auto& event : _mEvents) {
		if(event._taskId == taskId && (event._kind == EventKind::Skipped || event._kind == EventKind::Missed))
			total += event._units;
	}
	return total;
}

int DebtLedger::repaymentCount(const QString& taskId) const {
	return static_cast<int>(std::count_if(_mEvents.cbegin(), _mEvents.cend(), [&](const Event& event) {
		return event._taskId == taskId && event._kind == EventKind::Repayment;
	}));
}

QStringList DebtLedger::outstandingTaskIds() const {
	QSet<QString> repaid;
	for(const auto& event : _mEvents) {
		if(event._kind == EventKind::Repayment)
			repaid.insert(event._repaidEventId);
	}
	QStringList result;
	for(const auto& event : _mEvents) {
		if((event._kind == EventKind::Skipped || event._kind == EventKind::Missed)
		   && !repaid.contains(event._id) && !result.contains(event._taskId))
			result.append(event._taskId);
	}
	return result;
}

int DebtLedger::score() const {
	QSet<QString> repaid;
	for(const auto& event : _mEvents) {
		if(event._kind == EventKind::Repayment)
			repaid.insert(event._repaidEventId);
	}
	qint64 total = 0;
	for(const auto& event : _mEvents) {
		if((event._kind == EventKind::Skipped || event._kind == EventKind::Missed)
		   && !repaid.contains(event._id))
			total += event._weight;
	}
	return static_cast<int>(std::min<qint64>(total, std::numeric_limits<int>::max()));
}

QString DebtLedger::levelForScore(int score) {
	if(score <= 0)
		return QStringLiteral("no_debt");
	if(score < 4)
		return QStringLiteral("mild");
	if(score < 10)
		return QStringLiteral("moderate");
	if(score < 20)
		return QStringLiteral("severe");
	return QStringLiteral("critical");
}

QString DebtLedger::kindName(EventKind kind) {
	switch(kind) {
	case EventKind::Skipped:
		return QStringLiteral("skipped");
	case EventKind::Missed:
		return QStringLiteral("missed");
	case EventKind::Completed:
		return QStringLiteral("completed");
	case EventKind::Repayment:
		return QStringLiteral("repayment");
	}
	return QStringLiteral("skipped");
}

DebtLedger::EventKind DebtLedger::kindFromName(const QString& name, bool* ok) {
	if(ok)
		*ok = true;
	if(name == QStringLiteral("skipped"))
		return EventKind::Skipped;
	if(name == QStringLiteral("missed"))
		return EventKind::Missed;
	if(name == QStringLiteral("completed"))
		return EventKind::Completed;
	if(name == QStringLiteral("repayment"))
		return EventKind::Repayment;
	if(ok)
		*ok = false;
	return EventKind::Skipped;
}

bool DebtLedger::save() const {
	if(!_mWritable)
		return false;
	const QFileInfo info(_mFilePath);
	if(!QDir().mkpath(info.absolutePath()))
		return false;

	QJsonArray events;
	for(const auto& event : _mEvents) {
		QJsonObject object;
		object[QStringLiteral("id")]	 = event._id;
		object[QStringLiteral("task")]	 = event._taskId;
		object[QStringLiteral("kind")]	 = kindName(event._kind);
		object[QStringLiteral("units")]	 = event._units;
		object[QStringLiteral("weight")] = event._weight;
		if(!event._repaidEventId.isEmpty())
			object[QStringLiteral("repaid_event_id")] = event._repaidEventId;
		if(!event._instanceId.isEmpty())
			object[QStringLiteral("instance")] = event._instanceId;
		object[QStringLiteral("created_at")] = event._createdAt.toString(Qt::ISODateWithMs);
		events.append(object);
	}

	const auto contents = QJsonDocument(QJsonObject{
											{QStringLiteral("version"), K_SCHEMA_VERSION},
											{QStringLiteral("events"),  events			 },
	})
							  .toJson(QJsonDocument::Indented);
	QSaveFile file(_mFilePath);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.commit();
}
