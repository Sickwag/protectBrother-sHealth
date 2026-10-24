#pragma once
#include "break_reminder.h"
#include "break_timer.h"
#include "preset_manager.h"
#include "settings_manager.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QVector>

struct MonitorWidgets {
	QLabel*	   _nameLabel		 = nullptr;
	QSlider*   _brightnessSlider = nullptr;
	QSpinBox*  _brightnessSpin	 = nullptr;
	QCheckBox* _nightMode		 = nullptr;
	QSlider*   _warmthSlider	 = nullptr;
	QSpinBox*  _warmthSpin		 = nullptr;
	QWidget*   _warmthContainer	 = nullptr;
	QString	   _outputName;
};

class MainWindow : public QMainWindow {
	Q_OBJECT
  public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override = default;

  protected:
	void closeEvent(QCloseEvent* event) override;

  private slots:
	void onBrightnessChanged(int idx, int value);
	void onNightModeToggled(int idx, bool checked);
	void onWarmthChanged(int idx, int value);
	void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
	void onBreakSettingsChanged();
	void onWorkPeriodFinished(int breakMinutes);
	void onBreakPeriodFinished();
	void updateCountdown(const QTime& remaining, bool isWorking);
	void setDayMode();
	void setNightMode();

	void saveCurrentAsPreset();
	void loadSelectedPreset();
	void setSelectedAsDefault();
	void deleteSelectedPreset();
	void onPresetListDoubleClicked(QListWidgetItem* item);

  private:
	void setupUi();
	void setupTray();
	void setupBreakScheduler();
	void applyMonitor(int idx);
	void applyPreset(const QJsonObject& preset);
	void loadSettings();
	void saveSettings();
	void refreshPresetList();
	void setBreakControls(const QJsonObject& settings);

	SettingsManager _mSettings;
	PresetManager	_mPresets;

	QVector<MonitorWidgets> _mMonitors;

	QTabWidget*		 _mTabWidget	  = nullptr;
	QCheckBox*		 _mSyncBrightness = nullptr;
	QCheckBox*		 _mSyncNight	  = nullptr;
	QSystemTrayIcon* _mTrayIcon		  = nullptr;
	QListWidget*	 _mPresetList	  = nullptr;
	QLineEdit*		 _mPresetNameEdit = nullptr;
	QCheckBox*		 _mBreakEnabled	  = nullptr;
	QSpinBox*		 _mWorkMinutes	  = nullptr;
	QSpinBox*		 _mBreakMinutes	  = nullptr;
	QLineEdit*		 _mBreakMessage	  = nullptr;
	QLabel*			 _mCountdownLabel = nullptr;
	BreakTimer*		 _mTimer		  = nullptr;
	BreakReminder*	 _mBreakReminder  = nullptr;
	bool			 _mUpdating		  = false;
};
