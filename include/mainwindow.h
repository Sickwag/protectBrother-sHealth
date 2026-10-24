#pragma once
#include "preset_manager.h"
#include "reminder_manager.h"
#include "settings_manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimeEdit>
#include <QVector>

class QAction;

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
	explicit MainWindow(bool startHidden = false, QWidget* parent = nullptr);
	~MainWindow() override = default;

  protected:
	void closeEvent(QCloseEvent* event) override;

  private slots:
	void onBrightnessChanged(int idx, int value);
	void onNightModeToggled(int idx, bool checked);
	void onWarmthChanged(int idx, int value);
	void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
	void onBreakSettingsChanged();
	void onWaterSettingsChanged();
	void onQuietHoursChanged();
	void onAutostartToggled(bool enabled);
	void onPauseReminders();
	void onResumeReminders();
	void onCompleteWater();
	void onSkipWater();
	void onSnoozeWater();
	void onTriggerWater();
	void onRepaySelected();
	void onBreakStarted(int breakMinutes);
	void onBreakFinished();
	void onBreakSkipped();
	void onReminderStatus(const QString& message);
	void onReminderError(const QString& message);
	void onCountdownChanged(const QTime& remaining, bool working);
	void updateReminderState();
	void editPhraseConfiguration();
	void previewPhrase();
	void openPhraseFolder();
	void openReminderTab();
	void setDayMode();
	void setNightMode();

	void saveCurrentAsPreset();
	void loadSelectedPreset();
	void setSelectedAsDefault();
	void deleteSelectedPreset();
	void onPresetListDoubleClicked(QListWidgetItem* item);

  private:
	void					setupUi();
	void					setupTray();
	void					setupReminderManager();
	void					applyMonitor(int idx);
	void					applyPreset(const QJsonObject& preset);
	void					loadSettings();
	void					saveSettings();
	void					refreshPresetList();
	void					setBreakControls(const QJsonObject& settings);
	void					applyReminderUiToSettings();
	QVector<ReminderConfig> buildReminderConfigs() const;
	void					refreshTrayActions();
	QString					taskStatusText() const;

	SettingsManager _mSettings;
	PresetManager	_mPresets;

	QVector<MonitorWidgets> _mMonitors;

	QTabWidget*		 _mTabWidget			  = nullptr;
	QCheckBox*		 _mSyncBrightness		  = nullptr;
	QCheckBox*		 _mSyncNight			  = nullptr;
	QSystemTrayIcon* _mTrayIcon				  = nullptr;
	QListWidget*	 _mPresetList			  = nullptr;
	QLineEdit*		 _mPresetNameEdit		  = nullptr;
	QCheckBox*		 _mBreakEnabled			  = nullptr;
	QSpinBox*		 _mWorkMinutes			  = nullptr;
	QSpinBox*		 _mBreakMinutes			  = nullptr;
	QSpinBox*		 _mBreakWeight			  = nullptr;
	QLineEdit*		 _mBreakMessage			  = nullptr;
	QCheckBox*		 _mWaterEnabled			  = nullptr;
	QSpinBox*		 _mWaterIntervalMinutes	  = nullptr;
	QSpinBox*		 _mWaterVolumeMilliliters = nullptr;
	QSpinBox*		 _mWaterWeight			  = nullptr;
	QLabel*			 _mTaskStatusLabel		  = nullptr;
	QLabel*			 _mDebtReportLabel		  = nullptr;
	QLabel*			 _mWaterPendingLabel	  = nullptr;
	QLabel*			 _mActionPromptLabel	  = nullptr;
	QLabel*			 _mPhrasePathLabel		  = nullptr;
	QLabel*			 _mPhraseErrorLabel		  = nullptr;
	QPushButton*	 _mCompleteWaterButton	  = nullptr;
	QPushButton*	 _mSkipWaterButton		  = nullptr;
	QPushButton*	 _mSnoozeWaterButton	  = nullptr;
	QPushButton*	 _mTriggerWaterButton	  = nullptr;
	QPushButton*	 _mRepayWaterButton		  = nullptr;
	QPushButton*	 _mRepayBreakButton		  = nullptr;
	QComboBox*		 _mRepayTaskCombo		  = nullptr;
	QPushButton*	 _mRepaySelectedButton	  = nullptr;
	QPushButton*	 _mPauseButton			  = nullptr;
	QPushButton*	 _mResumeButton			  = nullptr;
	QCheckBox*		 _mQuietHoursEnabled	  = nullptr;
	QTimeEdit*		 _mQuietHoursStart		  = nullptr;
	QTimeEdit*		 _mQuietHoursEnd		  = nullptr;
	QCheckBox*		 _mAutostart			  = nullptr;
	QLabel*			 _mCountdownLabel		  = nullptr;
	QAction*		 _mTrayDrinkAction		  = nullptr;
	QAction*		 _mTraySnoozeAction		  = nullptr;
	QAction*		 _mTraySkipAction		  = nullptr;
	QAction*		 _mTrayPauseAction		  = nullptr;
	QAction*		 _mTrayResumeAction		  = nullptr;
	ReminderManager* _mReminderManager		  = nullptr;
	int				 _mReminderTabIndex		  = -1;
	bool			 _mUpdating				  = false;

  private:
	bool _mStartHidden = false;
};
