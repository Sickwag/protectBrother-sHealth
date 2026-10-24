#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

struct MonitorSettings {
	QString _name;
	int		_brightness = 100;
	bool	_nightMode	= false;
	int		_warmth		= 5000;
};

// A user-configurable reminder task. Keeping the descriptive fields next to the
// values makes saved presets self-describing and lets future tasks be added
// without touching every settings accessor.
struct ReminderConfig {
	QString _id;
	QString _name;
	QString _unit;
	QString _kind = QStringLiteral("periodic");	 // periodic | work_break
	QString _message;							 // user-provided overlay text (work_break)
	bool	_enabled		 = true;
	int		_intervalMinutes = 50;	 // periodic interval, or work duration for work_break
	int		_breakMinutes	 = 20;	 // work_break only
	int		_unitsPerEvent	 = 250;	 // water: ml; work_break: mirrors _breakMinutes
	int		_weight			 = 1;

	bool isBreakTask() const { return _kind == QStringLiteral("work_break"); }

	// The amount recorded for one skipped/completed event.
	int unitsPerEvent() const { return isBreakTask() ? _breakMinutes : _unitsPerEvent; }
};

class SettingsManager {
  public:
	SettingsManager();

	void load();
	void save();

	// Monitor settings
	QVector<MonitorSettings>&		monitors();
	const QVector<MonitorSettings>& monitors() const;

	bool syncBrightness() const { return _mSyncBrightness; }

	void setSyncBrightness(bool v) { _mSyncBrightness = v; }

	bool syncNight() const { return _mSyncNight; }

	void setSyncNight(bool v) { _mSyncNight = v; }

	// Reminder task configuration. Unknown task ids are preserved so a future
	// build can add tasks without discarding the user's existing configuration.
	QVector<ReminderConfig>&	   reminders();
	const QVector<ReminderConfig>& reminders() const;
	ReminderConfig*				   reminder(const QString& id);
	const ReminderConfig*		   reminder(const QString& id) const;
	void						   setReminder(const ReminderConfig& config);
	bool						   removeReminder(const QString& id);

	static QVector<ReminderConfig> defaultReminders();

	// Preserves settings for monitors that are currently disconnected. The
	// current list is authoritative only for the outputs it actually contains.
	static QVector<MonitorSettings> mergeMonitors(QVector<MonitorSettings>		  stored,
												  const QVector<MonitorSettings>& current);

	// A settings file from a newer schema is opened read-only instead of being
	// downgraded and overwritten. A corrupt file is backed up and a warning is
	// exposed so the UI can tell the user.
	bool isReadOnly() const { return _mReadOnly; }

	QString loadWarning() const { return _mLoadWarning; }

	// Quiet hours and a one-shot global pause. Both suppress reminders without
	// creating debt; the user explicitly asked not to be disturbed.
	bool quietHoursEnabled() const { return _mQuietHoursEnabled; }

	void setQuietHoursEnabled(bool enabled) { _mQuietHoursEnabled = enabled; }

	QString quietHoursStart() const { return _mQuietHoursStart; }

	QString quietHoursEnd() const { return _mQuietHoursEnd; }

	void setQuietHours(const QString& start, const QString& end);

	QDateTime pausedUntil() const { return _mPausedUntil; }

	void setPausedUntil(const QDateTime& until) { _mPausedUntil = until; }

	bool autostartEnabled() const { return _mAutostartEnabled; }

	void setAutostartEnabled(bool enabled) { _mAutostartEnabled = enabled; }

	// Window geometry
	QByteArray windowGeometry() const { return _mWindowGeometry; }

	void setWindowGeometry(const QByteArray& g) { _mWindowGeometry = g; }

	// Default preset
	QString defaultPreset() const { return _mDefaultPreset; }

	void setDefaultPreset(const QString& name) { _mDefaultPreset = name; }

	// Config directory path
	static QString configDir();

	// Renames the pre-1.1 configuration directory before anything creates the
	// new one. main() must call this before mkpath()/QLockFile.
	static void migrateLegacyConfig();

	// Creates or removes ~/.config/autostart/brohealth.desktop.
	static bool applyAutostart(bool enabled, QString* error = nullptr);

  private:
	QString					 _mFilePath;
	QVector<MonitorSettings> _mMonitors;
	QVector<ReminderConfig>	 _mReminders;
	bool					 _mSyncBrightness	 = false;
	bool					 _mSyncNight		 = false;
	bool					 _mQuietHoursEnabled = false;
	QString					 _mQuietHoursStart	 = QStringLiteral("22:00");
	QString					 _mQuietHoursEnd	 = QStringLiteral("08:00");
	QDateTime				 _mPausedUntil;
	bool					 _mAutostartEnabled = false;
	QByteArray				 _mWindowGeometry;
	QString					 _mDefaultPreset;
	bool					 _mReadOnly = false;
	QString					 _mLoadWarning;
};
