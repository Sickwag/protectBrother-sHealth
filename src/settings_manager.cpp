#include "settings_manager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

SettingsManager::SettingsManager() {
	const auto configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
	QDir	   configRootDir(configRoot);
	if(!QDir(configDir()).exists() && configRootDir.exists("screen-light-adjust"))
		configRootDir.rename("screen-light-adjust", "brohealth");

	auto dir = configDir();
	QDir().mkpath(dir);
	_mFilePath = dir + "/settings.json";
}

QString SettingsManager::configDir() {
	return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/brohealth";
}

QVector<MonitorSettings>& SettingsManager::monitors() { return _mMonitors; }

const QVector<MonitorSettings>& SettingsManager::monitors() const { return _mMonitors; }

void SettingsManager::load() {
	QFile file(_mFilePath);
	if(!file.open(QIODevice::ReadOnly))
		return;	 // First launch, use defaults

	auto doc = QJsonDocument::fromJson(file.readAll());
	if(doc.isNull() || !doc.isObject())
		return;

	auto root		 = doc.object();

	_mSyncBrightness = root.value("sync_brightness").toBool(false);
	_mSyncNight		 = root.value("sync_night").toBool(false);
	_mDefaultPreset	 = root.value("default_preset").toString();
	_mBreakEnabled	 = root.value("break_enabled").toBool(true);
	_mWorkMinutes	 = root.value("work_minutes").toInt(120);
	_mBreakMinutes	 = root.value("break_minutes").toInt(20);
	_mBreakMessage =
		root.value("break_message").toString(QStringLiteral("该休息一下了，眺望远方并活动身体。"));

	auto geo = root.value("window_geometry").toString();
	if(!geo.isEmpty())
		_mWindowGeometry = QByteArray::fromBase64(geo.toLatin1());

	_mMonitors.clear();
	auto arr = root.value("monitors").toArray();
	for(const auto& val : arr) {
		auto			obj = val.toObject();
		MonitorSettings ms;
		ms._name	   = obj.value("name").toString();
		ms._brightness = obj.value("brightness").toInt(100);
		ms._nightMode  = obj.value("night_mode").toBool(false);
		ms._warmth	   = obj.value("warmth").toInt(5000);
		_mMonitors.append(ms);
	}
}

void SettingsManager::save() {
	QJsonArray arr;
	for(const auto& ms : _mMonitors) {
		QJsonObject obj;
		obj["name"]		  = ms._name;
		obj["brightness"] = ms._brightness;
		obj["night_mode"] = ms._nightMode;
		if(ms._nightMode)
			obj["warmth"] = ms._warmth;
		arr.append(obj);
	}

	QJsonObject root;
	root["version"]			= 2;
	root["sync_brightness"] = _mSyncBrightness;
	root["sync_night"]		= _mSyncNight;
	root["default_preset"]	= _mDefaultPreset;
	root["break_enabled"]	= _mBreakEnabled;
	root["work_minutes"]	= _mWorkMinutes;
	root["break_minutes"]	= _mBreakMinutes;
	root["break_message"]	= _mBreakMessage;
	root["monitors"]		= arr;

	if(!_mWindowGeometry.isEmpty())
		root["window_geometry"] = QString::fromLatin1(_mWindowGeometry.toBase64());

	QSaveFile file(_mFilePath);
	if(!file.open(QIODevice::WriteOnly))
		return;

	const auto contents = QJsonDocument(root).toJson();
	if(file.write(contents) == contents.size())
		file.commit();
}
