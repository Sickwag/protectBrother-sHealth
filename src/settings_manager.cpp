#include "settings_manager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTime>

#include <algorithm>

namespace {
constexpr int K_MINIMUM_MINUTES = 1;
constexpr int K_MAXIMUM_MINUTES = 1440;
constexpr int K_MINIMUM_BREAK	= 1;
constexpr int K_MAXIMUM_BREAK	= 600;
constexpr int K_MINIMUM_UNITS	= 1;
constexpr int K_MAXIMUM_UNITS	= 10000;
constexpr int K_MINIMUM_WEIGHT	= 1;
constexpr int K_MAXIMUM_WEIGHT	= 100;

QString backupPathFor(const QString& path) {
	const auto info	  = QFileInfo(path);
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

QString sanitizedTime(const QString& value, const QString& fallback) {
	const auto time = QTime::fromString(value, QStringLiteral("HH:mm"));
	return time.isValid() ? time.toString(QStringLiteral("HH:mm")) : fallback;
}

ReminderConfig sanitized(ReminderConfig config) {
	config._intervalMinutes = std::clamp(config._intervalMinutes, K_MINIMUM_MINUTES, K_MAXIMUM_MINUTES);
	config._breakMinutes	= std::clamp(config._breakMinutes, K_MINIMUM_BREAK, K_MAXIMUM_BREAK);
	config._unitsPerEvent	= std::clamp(config._unitsPerEvent, K_MINIMUM_UNITS, K_MAXIMUM_UNITS);
	config._weight			= std::clamp(config._weight, K_MINIMUM_WEIGHT, K_MAXIMUM_WEIGHT);
	if(config._kind != QStringLiteral("periodic") && config._kind != QStringLiteral("work_break"))
		config._kind = QStringLiteral("periodic");
	if(config._name.isEmpty())
		config._name = config._id;
	if(config._unit.isEmpty())
		config._unit = QStringLiteral("次");
	if(config._id.isEmpty())
		config._id = QStringLiteral("task");
	return config;
}

QJsonObject reminderToJson(const ReminderConfig& config) {
	QJsonObject object;
	object[QStringLiteral("id")]			   = config._id;
	object[QStringLiteral("name")]			   = config._name;
	object[QStringLiteral("unit")]			   = config._unit;
	object[QStringLiteral("kind")]			   = config._kind;
	object[QStringLiteral("message")]		   = config._message;
	object[QStringLiteral("enabled")]		   = config._enabled;
	object[QStringLiteral("interval_minutes")] = config._intervalMinutes;
	object[QStringLiteral("break_minutes")]	   = config._breakMinutes;
	object[QStringLiteral("units_per_event")]  = config._unitsPerEvent;
	object[QStringLiteral("weight")]		   = config._weight;
	return object;
}

ReminderConfig reminderFromJson(const QJsonObject& object) {
	ReminderConfig config;
	config._id				= object.value(QStringLiteral("id")).toString();
	config._name			= object.value(QStringLiteral("name")).toString();
	config._unit			= object.value(QStringLiteral("unit")).toString();
	config._kind			= object.value(QStringLiteral("kind")).toString(QStringLiteral("periodic"));
	config._message			= object.value(QStringLiteral("message")).toString();
	config._enabled			= object.value(QStringLiteral("enabled")).toBool(true);
	config._intervalMinutes = object.value(QStringLiteral("interval_minutes")).toInt(50);
	config._breakMinutes	= object.value(QStringLiteral("break_minutes")).toInt(20);
	config._unitsPerEvent	= object.value(QStringLiteral("units_per_event")).toInt(250);
	config._weight			= object.value(QStringLiteral("weight")).toInt(1);
	return sanitized(config);
}
}  // namespace

SettingsManager::SettingsManager() {
	migrateLegacyConfig();

	auto dir = configDir();
	QDir().mkpath(dir);
	_mFilePath	= dir + "/settings.json";
	_mReminders = defaultReminders();
}

QVector<ReminderConfig> SettingsManager::defaultReminders() {
	ReminderConfig water;
	water._id			   = QStringLiteral("water");
	water._name			   = QStringLiteral("喝水");
	water._unit			   = QStringLiteral("ml");
	water._kind			   = QStringLiteral("periodic");
	water._intervalMinutes = 50;
	water._unitsPerEvent   = 250;
	water._weight		   = 1;

	ReminderConfig eyeBreak;
	eyeBreak._id			  = QStringLiteral("eye_break");
	eyeBreak._name			  = QStringLiteral("护眼休息");
	eyeBreak._unit			  = QStringLiteral("分钟");
	eyeBreak._kind			  = QStringLiteral("work_break");
	eyeBreak._intervalMinutes = 120;
	eyeBreak._breakMinutes	  = 20;
	eyeBreak._unitsPerEvent	  = 20;
	eyeBreak._weight		  = 2;

	return {water, eyeBreak};
}

QString SettingsManager::configDir() {
	return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/brohealth";
}

QVector<MonitorSettings> SettingsManager::mergeMonitors(QVector<MonitorSettings>		stored,
														const QVector<MonitorSettings>& current) {
	for(const auto& monitor : current) {
		auto existing = std::find_if(stored.begin(), stored.end(), [&](const MonitorSettings& candidate) {
			return candidate._name == monitor._name;
		});
		if(existing == stored.end())
			stored.append(monitor);
		else
			*existing = monitor;
	}
	return stored;
}

void SettingsManager::migrateLegacyConfig() {
	const auto root = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
	QDir	   rootDir(root);
	if(rootDir.exists(QStringLiteral("screen-light-adjust")) && !rootDir.exists(QStringLiteral("brohealth")))
		rootDir.rename(QStringLiteral("screen-light-adjust"), QStringLiteral("brohealth"));
}

QVector<MonitorSettings>& SettingsManager::monitors() { return _mMonitors; }

const QVector<MonitorSettings>& SettingsManager::monitors() const { return _mMonitors; }

QVector<ReminderConfig>& SettingsManager::reminders() { return _mReminders; }

const QVector<ReminderConfig>& SettingsManager::reminders() const { return _mReminders; }

ReminderConfig* SettingsManager::reminder(const QString& id) {
	for(auto& config : _mReminders) {
		if(config._id == id)
			return &config;
	}
	return nullptr;
}

const ReminderConfig* SettingsManager::reminder(const QString& id) const {
	for(const auto& config : _mReminders) {
		if(config._id == id)
			return &config;
	}
	return nullptr;
}

void SettingsManager::setReminder(const ReminderConfig& config) {
	const auto sanitizedConfig = sanitized(config);
	for(auto& existing : _mReminders) {
		if(existing._id == sanitizedConfig._id) {
			existing = sanitizedConfig;
			return;
		}
	}
	_mReminders.append(sanitizedConfig);
}

bool SettingsManager::removeReminder(const QString& id) {
	for(auto it = _mReminders.begin(); it != _mReminders.end(); ++it) {
		if(it->_id == id) {
			_mReminders.erase(it);
			return true;
		}
	}
	return false;
}

void SettingsManager::setQuietHours(const QString& start, const QString& end) {
	_mQuietHoursStart = sanitizedTime(start, QStringLiteral("22:00"));
	_mQuietHoursEnd	  = sanitizedTime(end, QStringLiteral("08:00"));
}

void SettingsManager::load() {
	_mReadOnly = false;
	_mLoadWarning.clear();

	QFile file(_mFilePath);
	if(!file.exists())
		return;
	if(!file.open(QIODevice::ReadOnly)) {
		_mReadOnly	  = true;
		_mLoadWarning = QStringLiteral("无法读取设置文件：%1").arg(_mFilePath);
		qWarning().noquote() << _mLoadWarning;
		return;
	}

	QJsonParseError parseError;
	const auto		document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if(parseError.error != QJsonParseError::NoError || !document.isObject()) {
		const auto backup = backupPathFor(_mFilePath);
		if(QFile::copy(_mFilePath, backup)) {
			_mLoadWarning = QStringLiteral("设置文件损坏，已备份到：%1").arg(backup);
			qWarning().noquote() << _mLoadWarning;
		} else {
			_mLoadWarning = QStringLiteral("设置文件损坏，备份失败：%1").arg(_mFilePath);
			qWarning().noquote() << _mLoadWarning;
		}
		return;
	}

	const auto root	   = document.object();
	const int  version = root.value(QStringLiteral("version")).toInt(0);
	if(version > 3) {
		const auto backup = backupPathFor(_mFilePath);
		if(QFile::copy(_mFilePath, backup))
			_mLoadWarning =
				QStringLiteral("设置文件版本 %1 高于当前程序，已备份到 %2；本次以只读默认值运行。")
					.arg(version)
					.arg(backup);
		else
			_mLoadWarning =
				QStringLiteral("设置文件版本 %1 高于当前程序；备份失败，本次以只读默认值运行。").arg(version);
		_mReadOnly = true;
		qWarning().noquote() << _mLoadWarning;
		return;
	}

	_mSyncBrightness	= root.value(QStringLiteral("sync_brightness")).toBool(false);
	_mSyncNight			= root.value(QStringLiteral("sync_night")).toBool(false);
	_mDefaultPreset		= root.value(QStringLiteral("default_preset")).toString();
	_mQuietHoursEnabled = root.value(QStringLiteral("quiet_hours_enabled")).toBool(false);
	_mQuietHoursStart =
		sanitizedTime(root.value(QStringLiteral("quiet_hours_start")).toString(), QStringLiteral("22:00"));
	_mQuietHoursEnd =
		sanitizedTime(root.value(QStringLiteral("quiet_hours_end")).toString(), QStringLiteral("08:00"));
	_mPausedUntil =
		QDateTime::fromString(root.value(QStringLiteral("paused_until")).toString(), Qt::ISODateWithMs);
	_mAutostartEnabled = root.value(QStringLiteral("autostart")).toBool(false);

	auto geo		   = root.value(QStringLiteral("window_geometry")).toString();
	if(!geo.isEmpty())
		_mWindowGeometry = QByteArray::fromBase64(geo.toLatin1());

	_mMonitors.clear();
	for(const auto& value : root.value(QStringLiteral("monitors")).toArray()) {
		const auto		object = value.toObject();
		MonitorSettings settings;
		settings._name		 = object.value(QStringLiteral("name")).toString();
		settings._brightness = object.value(QStringLiteral("brightness")).toInt(100);
		settings._nightMode	 = object.value(QStringLiteral("night_mode")).toBool(false);
		settings._warmth	 = object.value(QStringLiteral("warmth")).toInt(5000);
		_mMonitors.append(settings);
	}

	const auto reminderValues = root.value(QStringLiteral("reminders")).toArray();
	if(!reminderValues.isEmpty()) {
		_mReminders.clear();
		for(const auto& value : reminderValues) {
			if(!value.isObject())
				continue;
			_mReminders.append(reminderFromJson(value.toObject()));
		}
	} else if(root.contains(QStringLiteral("water_enabled"))
			  || root.contains(QStringLiteral("break_enabled"))) {
		// Migrate the flat v2 schema. Preserve user values and fill in the
		// descriptive fields that older settings files did not have.
		_mReminders.clear();
		{
			auto defaults = defaultReminders();
			for(auto& config : defaults) {
				if(config._id == QStringLiteral("water")) {
					config._enabled			= root.value(QStringLiteral("water_enabled")).toBool(true);
					config._intervalMinutes = root.value(QStringLiteral("water_interval_minutes")).toInt(50);
					config._unitsPerEvent	= root.value(QStringLiteral("water_volume_ml")).toInt(250);
				} else if(config._id == QStringLiteral("eye_break")) {
					config._enabled			= root.value(QStringLiteral("break_enabled")).toBool(true);
					config._intervalMinutes = root.value(QStringLiteral("work_minutes")).toInt(120);
					config._breakMinutes	= root.value(QStringLiteral("break_minutes")).toInt(20);
					config._unitsPerEvent	= config._breakMinutes;
					config._message			= root.value(QStringLiteral("break_message")).toString();
				}
				_mReminders.append(sanitized(config));
			}
		}
	}
}

void SettingsManager::save() {
	if(_mReadOnly)
		return;

	QJsonArray monitors;
	for(const auto& monitor : _mMonitors) {
		QJsonObject object;
		object[QStringLiteral("name")]		 = monitor._name;
		object[QStringLiteral("brightness")] = monitor._brightness;
		object[QStringLiteral("night_mode")] = monitor._nightMode;
		if(monitor._nightMode)
			object[QStringLiteral("warmth")] = monitor._warmth;
		monitors.append(object);
	}

	QJsonArray reminders;
	for(const auto& config : _mReminders)
		reminders.append(reminderToJson(config));

	QJsonObject root;
	root[QStringLiteral("version")]				= 3;
	root[QStringLiteral("sync_brightness")]		= _mSyncBrightness;
	root[QStringLiteral("sync_night")]			= _mSyncNight;
	root[QStringLiteral("default_preset")]		= _mDefaultPreset;
	root[QStringLiteral("monitors")]			= monitors;
	root[QStringLiteral("reminders")]			= reminders;
	root[QStringLiteral("quiet_hours_enabled")] = _mQuietHoursEnabled;
	root[QStringLiteral("quiet_hours_start")]	= _mQuietHoursStart;
	root[QStringLiteral("quiet_hours_end")]		= _mQuietHoursEnd;
	root[QStringLiteral("autostart")]			= _mAutostartEnabled;
	if(_mPausedUntil.isValid())
		root[QStringLiteral("paused_until")] = _mPausedUntil.toString(Qt::ISODateWithMs);
	if(!_mWindowGeometry.isEmpty())
		root[QStringLiteral("window_geometry")] = QString::fromLatin1(_mWindowGeometry.toBase64());

	QSaveFile file(_mFilePath);
	if(!file.open(QIODevice::WriteOnly))
		return;

	const auto contents = QJsonDocument(root).toJson();
	if(file.write(contents) == contents.size())
		file.commit();
}

bool SettingsManager::applyAutostart(bool enabled, QString* error) {
	const auto directory =
		QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart");
	const auto path = directory + QStringLiteral("/brohealth.desktop");

	if(!enabled) {
		if(QFile::exists(path) && !QFile::remove(path)) {
			if(error)
				*error = QStringLiteral("无法删除开机启动项：%1").arg(path);
			return false;
		}
		return true;
	}

	if(!QDir().mkpath(directory)) {
		if(error)
			*error = QStringLiteral("无法创建开机启动目录：%1").arg(directory);
		return false;
	}

	QString executable = QCoreApplication::applicationFilePath();
	executable.replace(QLatin1Char('"'), QStringLiteral("\\\""));
	const auto contents = QStringLiteral("[Desktop Entry]\n"
										 "Type=Application\n"
										 "Name=Protect Brother's Health\n"
										 "GenericName=Brother Health Reminder\n"
										 "Comment=Water and eye-break reminders\n"
										 "Exec=\"%1\" --hidden\n"
										 "Icon=brohealth\n"
										 "Terminal=false\n"
										 "X-GNOME-Autostart-enabled=true\n"
										 "StartupNotify=false\n")
							  .arg(executable)
							  .toUtf8();

	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
		if(error)
			*error = QStringLiteral("无法写入开机启动项：%1").arg(path);
		return false;
	}
	return true;
}
