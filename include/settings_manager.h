#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

struct MonitorSettings {
	QString _name;
	int		_brightness = 100;
	bool	_nightMode	= false;
	int		_warmth		= 5000;
};

class SettingsManager {
  public:
	SettingsManager();

	void load();
	void save();

	// Monitor settings
	QVector<MonitorSettings>&		monitors();
	const QVector<MonitorSettings>& monitors() const;

	// Sync flags
	bool syncBrightness() const { return _mSyncBrightness; }

	void setSyncBrightness(bool v) { _mSyncBrightness = v; }

	bool syncNight() const { return _mSyncNight; }

	void setSyncNight(bool v) { _mSyncNight = v; }

	bool breakEnabled() const { return _mBreakEnabled; }

	void setBreakEnabled(bool v) { _mBreakEnabled = v; }

	int workMinutes() const { return _mWorkMinutes; }

	void setWorkMinutes(int v) { _mWorkMinutes = v; }

	int breakMinutes() const { return _mBreakMinutes; }

	void setBreakMinutes(int v) { _mBreakMinutes = v; }

	QString breakMessage() const { return _mBreakMessage; }

	void setBreakMessage(const QString& v) { _mBreakMessage = v; }

	// Window geometry
	QByteArray windowGeometry() const { return _mWindowGeometry; }

	void setWindowGeometry(const QByteArray& g) { _mWindowGeometry = g; }

	// Default preset
	QString defaultPreset() const { return _mDefaultPreset; }

	void setDefaultPreset(const QString& name) { _mDefaultPreset = name; }

	// Config directory path
	static QString configDir();

  private:
	QString					 _mFilePath;
	QVector<MonitorSettings> _mMonitors;
	bool					 _mSyncBrightness = false;
	bool					 _mSyncNight	  = false;
	bool					 _mBreakEnabled	  = true;
	int						 _mWorkMinutes	  = 120;
	int						 _mBreakMinutes	  = 20;
	QString	   _mBreakMessage = QStringLiteral("该休息一下了，眺望远方并活动身体。");
	QByteArray _mWindowGeometry;
	QString	   _mDefaultPreset;
};
