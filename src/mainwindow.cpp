#include "mainwindow.h"

#include "color_temp.h"
#include "monitor_ctrl.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QScrollArea>
#include <QStatusBar>
#include <QTime>
#include <QTimeEdit>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

static constexpr int BRIGHTNESS_MIN = 10;
static constexpr int BRIGHTNESS_MAX = 200;
static constexpr int WARMTH_MIN		= 1000;
static constexpr int WARMTH_MAX		= 7500;

namespace {
QDateTime localNow() { return QDateTime::currentDateTime(); }

QString formatDue(const QDateTime& due) {
	return due.isValid() ? due.toString(QStringLiteral("hh:mm")) : QStringLiteral("--:--");
}
}  // namespace

MainWindow::MainWindow(bool startHidden, QWidget* parent)
	: QMainWindow(parent)
	, _mStartHidden(startHidden) {
	setWindowTitle("Protect Brother's Health");
	setMinimumWidth(460);

	for(const auto& mi : MonitorController::discover()) {
		MonitorWidgets widgets;
		widgets._outputName = mi._name;
		_mMonitors.append(widgets);
	}

	setupUi();
	setupTray();
	loadSettings();
	setupReminderManager();

	connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::saveSettings);

	if(_mStartHidden && QSystemTrayIcon::isSystemTrayAvailable())
		hide();
}

// ============================================================
//  UI setup
// ============================================================

void MainWindow::setupUi() {
	auto* central	 = new QWidget(this);
	auto* mainLayout = new QVBoxLayout(central);
	mainLayout->setContentsMargins(12, 12, 12, 12);
	mainLayout->setSpacing(8);

	_mTabWidget			= new QTabWidget;

	// ---------- Display ----------
	auto* displayTab	= new QWidget;
	auto* displayLayout = new QVBoxLayout(displayTab);
	displayLayout->setContentsMargins(0, 8, 0, 0);
	displayLayout->setSpacing(8);

	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* scrollContent	 = new QWidget;
	auto* monitorsLayout = new QVBoxLayout(scrollContent);
	monitorsLayout->setSpacing(10);

	for(int i = 0; i < _mMonitors.size(); ++i) {
		auto* widgets = &_mMonitors[i];

		auto* group	  = new QGroupBox;
		auto* gl	  = new QVBoxLayout(group);
		gl->setSpacing(6);

		const auto displayName = widgets->_outputName == QStringLiteral("wayland-global")
									 ? QStringLiteral("Wayland（所有显示器）")
									 : widgets->_outputName;
		auto*	   nameLabel   = new QLabel(displayName);
		QFont	   nameFont	   = nameLabel->font();
		nameFont.setBold(true);
		nameFont.setPointSize(nameFont.pointSize() + 1);
		nameLabel->setFont(nameFont);
		gl->addWidget(nameLabel);

		auto* brightnessRow	  = new QHBoxLayout;
		auto* brightnessLabel = new QLabel("亮度");
		brightnessLabel->setFixedWidth(36);
		auto*	  brightnessSlider = new QSlider(Qt::Horizontal);
		const int maxBrightness	   = MonitorController::isWaylandSession() ? 100 : BRIGHTNESS_MAX;
		brightnessSlider->setRange(BRIGHTNESS_MIN, maxBrightness);

		auto* brightnessSpin = new QSpinBox;
		brightnessSpin->setRange(BRIGHTNESS_MIN, maxBrightness);
		brightnessSpin->setSuffix("%");
		brightnessSpin->setSingleStep(1);
		brightnessSpin->setKeyboardTracking(false);
		brightnessSpin->setAccelerated(true);
		brightnessSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		brightnessSpin->setValue(100);
		brightnessSlider->setValue(100);

		brightnessRow->addWidget(brightnessLabel);
		brightnessRow->addWidget(brightnessSlider, 1);
		brightnessRow->addWidget(brightnessSpin);
		gl->addLayout(brightnessRow);

		auto* nightMode = new QCheckBox("护眼模式");
		gl->addWidget(nightMode);

		auto* warmthRow	  = new QHBoxLayout;
		auto* warmthLabel = new QLabel("色温");
		warmthLabel->setFixedWidth(36);
		auto* warmthSlider = new QSlider(Qt::Horizontal);
		warmthSlider->setRange(WARMTH_MIN, WARMTH_MAX);
		warmthSlider->setSingleStep(50);
		warmthSlider->setPageStep(500);
		warmthSlider->setValue(5000);

		auto* warmthSpin = new QSpinBox;
		warmthSpin->setRange(WARMTH_MIN, WARMTH_MAX);
		warmthSpin->setSuffix("K");
		warmthSpin->setSingleStep(50);
		warmthSpin->setKeyboardTracking(false);
		warmthSpin->setAccelerated(true);
		warmthSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		warmthSpin->setValue(5000);

		warmthRow->addWidget(warmthLabel);
		warmthRow->addWidget(warmthSlider, 1);
		warmthRow->addWidget(warmthSpin);

		auto* warmthContainer = new QWidget;
		warmthContainer->setLayout(warmthRow);
		warmthContainer->setVisible(false);
		gl->addWidget(warmthContainer);

		monitorsLayout->addWidget(group);

		widgets->_nameLabel		   = nameLabel;
		widgets->_brightnessSlider = brightnessSlider;
		widgets->_brightnessSpin   = brightnessSpin;
		widgets->_nightMode		   = nightMode;
		widgets->_warmthSlider	   = warmthSlider;
		widgets->_warmthSpin	   = warmthSpin;
		widgets->_warmthContainer  = warmthContainer;

		connect(brightnessSlider, &QSlider::valueChanged, this, [this, i](int value) {
			onBrightnessChanged(i, value);
		});
		connect(brightnessSlider, &QSlider::valueChanged, brightnessSpin, &QSpinBox::setValue);
		connect(brightnessSpin,
				QOverload<int>::of(&QSpinBox::valueChanged),
				brightnessSlider,
				&QSlider::setValue);

		connect(nightMode, &QCheckBox::toggled, this, [this, i](bool checked) {
			onNightModeToggled(i, checked);
		});

		connect(
			warmthSlider, &QSlider::valueChanged, this, [this, i](int value) { onWarmthChanged(i, value); });
		connect(warmthSlider, &QSlider::valueChanged, warmthSpin, &QSpinBox::setValue);
		connect(warmthSpin, QOverload<int>::of(&QSpinBox::valueChanged), warmthSlider, &QSlider::setValue);
	}

	if(_mMonitors.isEmpty()) {
		auto* unavailable = new QLabel(MonitorController::isWaylandSession()
										   ? QStringLiteral("未检测到可调整的显示器；提醒功能仍可使用。")
										   : QStringLiteral("未检测到 xrandr 显示输出；提醒功能仍可使用。"));
		unavailable->setWordWrap(true);
		unavailable->setStyleSheet(QStringLiteral("color: #a33;"));
		monitorsLayout->addWidget(unavailable);
	} else if(MonitorController::isWaylandSession()) {
		auto* waylandHint =
			new QLabel(QStringLiteral("Wayland 色温和亮度由合成器统一调整，休息提醒会显示在每个屏幕。"));
		waylandHint->setWordWrap(true);
		waylandHint->setStyleSheet(QStringLiteral("color: gray;"));
		monitorsLayout->addWidget(waylandHint);
	}

	scroll->setWidget(scrollContent);
	displayLayout->addWidget(scroll, 1);

	auto* separator = new QFrame;
	separator->setFrameShape(QFrame::HLine);
	separator->setFrameShadow(QFrame::Sunken);
	displayLayout->addWidget(separator);

	auto* syncRow	 = new QHBoxLayout;
	_mSyncBrightness = new QCheckBox("同步亮度");
	_mSyncNight		 = new QCheckBox("同步护眼模式");
	syncRow->addWidget(_mSyncBrightness);
	syncRow->addStretch();
	syncRow->addWidget(_mSyncNight);
	auto* syncContainer = new QWidget;
	syncContainer->setLayout(syncRow);
	displayLayout->addWidget(syncContainer);

	_mTabWidget->addTab(displayTab, "显示");

	// ---------- Break settings ----------
	auto* breakTab	  = new QWidget;
	auto* breakLayout = new QVBoxLayout(breakTab);
	breakLayout->setContentsMargins(12, 12, 12, 12);
	breakLayout->setSpacing(10);

	_mBreakEnabled = new QCheckBox(QStringLiteral("启用定时休息提醒"));
	breakLayout->addWidget(_mBreakEnabled);

	auto* breakForm = new QFormLayout;
	_mWorkMinutes	= new QSpinBox;
	_mWorkMinutes->setRange(1, 1440);
	_mWorkMinutes->setSuffix(QStringLiteral(" 分钟"));
	_mWorkMinutes->setValue(120);
	_mBreakMinutes = new QSpinBox;
	_mBreakMinutes->setRange(1, 600);
	_mBreakMinutes->setSuffix(QStringLiteral(" 分钟"));
	_mBreakMinutes->setValue(20);
	_mBreakWeight = new QSpinBox;
	_mBreakWeight->setRange(1, 100);
	_mBreakWeight->setValue(2);
	_mBreakMessage = new QLineEdit;
	_mBreakMessage->setPlaceholderText(QStringLiteral("留空则使用文案池里的休息文案"));
	breakForm->addRow(QStringLiteral("工作时长"), _mWorkMinutes);
	breakForm->addRow(QStringLiteral("休息时长"), _mBreakMinutes);
	breakForm->addRow(QStringLiteral("欠账权重"), _mBreakWeight);
	breakForm->addRow(QStringLiteral("提示语句"), _mBreakMessage);
	breakLayout->addLayout(breakForm);

	_mCountdownLabel = new QLabel(QStringLiteral("休息提醒尚未启动"));
	_mCountdownLabel->setAlignment(Qt::AlignCenter);
	breakLayout->addWidget(_mCountdownLabel);
	breakLayout->addStretch();
	_mTabWidget->addTab(breakTab, QStringLiteral("休息"));

	// ---------- Reminder centre and ledger ----------
	auto* reminderTab	 = new QWidget;
	auto* reminderScroll = new QScrollArea;
	reminderScroll->setWidgetResizable(true);
	reminderScroll->setFrameShape(QFrame::NoFrame);
	auto* reminderContent = new QWidget;
	auto* reminderLayout  = new QVBoxLayout(reminderContent);
	reminderLayout->setContentsMargins(12, 12, 12, 12);
	reminderLayout->setSpacing(8);
	reminderScroll->setWidget(reminderContent);
	auto* reminderTabLayout = new QVBoxLayout(reminderTab);
	reminderTabLayout->setContentsMargins(0, 0, 0, 0);
	reminderTabLayout->addWidget(reminderScroll);

	auto* waterGroup  = new QGroupBox(QStringLiteral("喝水提醒"));
	auto* waterLayout = new QVBoxLayout(waterGroup);
	_mWaterEnabled	  = new QCheckBox(QStringLiteral("启用喝水提醒"));
	waterLayout->addWidget(_mWaterEnabled);
	auto* waterForm		   = new QFormLayout;
	_mWaterIntervalMinutes = new QSpinBox;
	_mWaterIntervalMinutes->setRange(1, 1440);
	_mWaterIntervalMinutes->setSuffix(QStringLiteral(" 分钟"));
	_mWaterIntervalMinutes->setValue(50);
	_mWaterVolumeMilliliters = new QSpinBox;
	_mWaterVolumeMilliliters->setRange(1, 10000);
	_mWaterVolumeMilliliters->setSuffix(QStringLiteral(" ml"));
	_mWaterVolumeMilliliters->setValue(250);
	_mWaterWeight = new QSpinBox;
	_mWaterWeight->setRange(1, 100);
	_mWaterWeight->setValue(1);
	waterForm->addRow(QStringLiteral("提醒间隔"), _mWaterIntervalMinutes);
	waterForm->addRow(QStringLiteral("每次记账"), _mWaterVolumeMilliliters);
	waterForm->addRow(QStringLiteral("欠账权重"), _mWaterWeight);
	waterLayout->addLayout(waterForm);
	reminderLayout->addWidget(waterGroup);

	auto* taskTitle		= new QLabel(QStringLiteral("中控状态"));
	QFont taskTitleFont = taskTitle->font();
	taskTitleFont.setBold(true);
	taskTitle->setFont(taskTitleFont);
	reminderLayout->addWidget(taskTitle);

	_mTaskStatusLabel = new QLabel;
	_mTaskStatusLabel->setWordWrap(true);
	_mTaskStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	reminderLayout->addWidget(_mTaskStatusLabel);

	_mWaterPendingLabel = new QLabel;
	_mWaterPendingLabel->setWordWrap(true);
	reminderLayout->addWidget(_mWaterPendingLabel);

	auto* waterActions	  = new QHBoxLayout;
	_mCompleteWaterButton = new QPushButton(QStringLiteral("喝了，结清本次"));
	_mSnoozeWaterButton	  = new QPushButton(QStringLiteral("5 分钟后再提醒"));
	_mSkipWaterButton	  = new QPushButton(QStringLiteral("跳过，记一笔"));
	_mTriggerWaterButton  = new QPushButton(QStringLiteral("立即提醒"));
	waterActions->addWidget(_mCompleteWaterButton);
	waterActions->addWidget(_mSnoozeWaterButton);
	waterActions->addWidget(_mSkipWaterButton);
	waterActions->addWidget(_mTriggerWaterButton);
	reminderLayout->addLayout(waterActions);

	auto* debtTitle		= new QLabel(QStringLiteral("兄弟账本"));
	QFont debtTitleFont = debtTitle->font();
	debtTitleFont.setBold(true);
	debtTitle->setFont(debtTitleFont);
	reminderLayout->addWidget(debtTitle);

	_mDebtReportLabel = new QLabel;
	_mDebtReportLabel->setWordWrap(true);
	_mDebtReportLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	reminderLayout->addWidget(_mDebtReportLabel);

	_mActionPromptLabel = new QLabel;
	_mActionPromptLabel->setWordWrap(true);
	reminderLayout->addWidget(_mActionPromptLabel);

	auto* repaymentActions = new QHBoxLayout;
	_mRepayWaterButton	   = new QPushButton(QStringLiteral("补喝一杯，抵一笔"));
	_mRepayBreakButton	   = new QPushButton(QStringLiteral("补休息，抵一笔"));
	_mRepayTaskCombo	   = new QComboBox;
	_mRepaySelectedButton  = new QPushButton(QStringLiteral("补还选中欠账"));
	repaymentActions->addWidget(_mRepayWaterButton);
	repaymentActions->addWidget(_mRepayBreakButton);
	reminderLayout->addLayout(repaymentActions);

	auto* repaySelectedRow = new QHBoxLayout;
	repaySelectedRow->addWidget(new QLabel(QStringLiteral("其他任务：")));
	repaySelectedRow->addWidget(_mRepayTaskCombo, 1);
	repaySelectedRow->addWidget(_mRepaySelectedButton);
	reminderLayout->addLayout(repaySelectedRow);

	auto* pauseActions = new QHBoxLayout;
	_mPauseButton	   = new QPushButton(QStringLiteral("暂停 30 分钟"));
	_mResumeButton	   = new QPushButton(QStringLiteral("恢复提醒"));
	pauseActions->addWidget(_mPauseButton);
	pauseActions->addWidget(_mResumeButton);
	reminderLayout->addLayout(pauseActions);

	auto* quietRow		= new QHBoxLayout;
	_mQuietHoursEnabled = new QCheckBox(QStringLiteral("免打扰时段"));
	_mQuietHoursStart	= new QTimeEdit(QTime(22, 0));
	_mQuietHoursStart->setDisplayFormat(QStringLiteral("HH:mm"));
	_mQuietHoursEnd = new QTimeEdit(QTime(8, 0));
	_mQuietHoursEnd->setDisplayFormat(QStringLiteral("HH:mm"));
	_mAutostart = new QCheckBox(QStringLiteral("开机启动"));
	quietRow->addWidget(_mQuietHoursEnabled);
	quietRow->addWidget(_mQuietHoursStart);
	quietRow->addWidget(new QLabel(QStringLiteral("至")));
	quietRow->addWidget(_mQuietHoursEnd);
	quietRow->addStretch();
	quietRow->addWidget(_mAutostart);
	reminderLayout->addLayout(quietRow);

	auto* phraseHelp = new QLabel(
		QStringLiteral("文案模板随安装包提供，首次运行会复制到用户目录。修改后自动生效，无需管理员权限。"));
	phraseHelp->setWordWrap(true);
	reminderLayout->addWidget(phraseHelp);
	_mPhrasePathLabel = new QLabel;
	_mPhrasePathLabel->setWordWrap(true);
	_mPhrasePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	reminderLayout->addWidget(_mPhrasePathLabel);
	_mPhraseErrorLabel = new QLabel;
	_mPhraseErrorLabel->setWordWrap(true);
	_mPhraseErrorLabel->setStyleSheet(QStringLiteral("color: #a33;"));
	_mPhraseErrorLabel->setVisible(false);
	reminderLayout->addWidget(_mPhraseErrorLabel);

	auto* phraseActions		   = new QHBoxLayout;
	auto* editPhrasesButton	   = new QPushButton(QStringLiteral("编辑文案"));
	auto* reloadPhrasesButton  = new QPushButton(QStringLiteral("重新加载"));
	auto* previewPhrasesButton = new QPushButton(QStringLiteral("刷新预览"));
	auto* openFolderButton	   = new QPushButton(QStringLiteral("打开目录"));
	phraseActions->addWidget(editPhrasesButton);
	phraseActions->addWidget(reloadPhrasesButton);
	phraseActions->addWidget(previewPhrasesButton);
	phraseActions->addWidget(openFolderButton);
	reminderLayout->addLayout(phraseActions);
	reminderLayout->addStretch();

	_mReminderTabIndex	= _mTabWidget->addTab(reminderTab, QStringLiteral("提醒与账本"));

	// ---------- Presets ----------
	auto* presetsTab	= new QWidget;
	auto* presetsLayout = new QVBoxLayout(presetsTab);
	presetsLayout->setContentsMargins(8, 8, 8, 8);
	presetsLayout->setSpacing(8);

	auto* presetTitle = new QLabel("预设管理");
	QFont titleFont	  = presetTitle->font();
	titleFont.setBold(true);
	titleFont.setPointSize(titleFont.pointSize() + 1);
	presetTitle->setFont(titleFont);
	presetsLayout->addWidget(presetTitle);

	_mPresetList = new QListWidget;
	_mPresetList->setAlternatingRowColors(true);
	presetsLayout->addWidget(_mPresetList, 1);

	auto* saveRow	 = new QHBoxLayout;
	_mPresetNameEdit = new QLineEdit;
	_mPresetNameEdit->setPlaceholderText("输入预设名称...");
	auto* saveBtn = new QPushButton("保存当前为预设");
	saveRow->addWidget(_mPresetNameEdit, 1);
	saveRow->addWidget(saveBtn);
	presetsLayout->addLayout(saveRow);

	auto* actionRow = new QHBoxLayout;
	auto* loadBtn	= new QPushButton("加载预设");
	auto* delBtn	= new QPushButton("删除预设");
	auto* defBtn	= new QPushButton("设为默认预设");
	actionRow->addWidget(loadBtn);
	actionRow->addWidget(delBtn);
	actionRow->addWidget(defBtn);
	presetsLayout->addLayout(actionRow);
	presetsLayout->addStretch();

	_mTabWidget->addTab(presetsTab, "预设");
	mainLayout->addWidget(_mTabWidget, 1);

	auto* hint = new QLabel("关闭窗口后继续在托盘里计时；没有托盘时会询问是否最小化。");
	hint->setAlignment(Qt::AlignCenter);
	hint->setStyleSheet("color: gray;");
	mainLayout->addWidget(hint);
	setCentralWidget(central);

	connect(saveBtn, &QPushButton::clicked, this, &MainWindow::saveCurrentAsPreset);
	connect(loadBtn, &QPushButton::clicked, this, &MainWindow::loadSelectedPreset);
	connect(delBtn, &QPushButton::clicked, this, &MainWindow::deleteSelectedPreset);
	connect(defBtn, &QPushButton::clicked, this, &MainWindow::setSelectedAsDefault);
	connect(_mPresetList, &QListWidget::itemDoubleClicked, this, &MainWindow::onPresetListDoubleClicked);

	connect(_mBreakEnabled, &QCheckBox::toggled, this, &MainWindow::onBreakSettingsChanged);
	connect(_mWorkMinutes, &QSpinBox::valueChanged, this, &MainWindow::onBreakSettingsChanged);
	connect(_mBreakMinutes, &QSpinBox::valueChanged, this, &MainWindow::onBreakSettingsChanged);
	connect(_mBreakWeight, &QSpinBox::valueChanged, this, &MainWindow::onBreakSettingsChanged);
	connect(_mBreakMessage, &QLineEdit::editingFinished, this, &MainWindow::onBreakSettingsChanged);
	connect(_mWaterEnabled, &QCheckBox::toggled, this, &MainWindow::onWaterSettingsChanged);
	connect(_mWaterIntervalMinutes, &QSpinBox::valueChanged, this, &MainWindow::onWaterSettingsChanged);
	connect(_mWaterVolumeMilliliters, &QSpinBox::valueChanged, this, &MainWindow::onWaterSettingsChanged);
	connect(_mWaterWeight, &QSpinBox::valueChanged, this, &MainWindow::onWaterSettingsChanged);
	connect(_mCompleteWaterButton, &QPushButton::clicked, this, &MainWindow::onCompleteWater);
	connect(_mSnoozeWaterButton, &QPushButton::clicked, this, &MainWindow::onSnoozeWater);
	connect(_mSkipWaterButton, &QPushButton::clicked, this, &MainWindow::onSkipWater);
	connect(_mTriggerWaterButton, &QPushButton::clicked, this, &MainWindow::onTriggerWater);
	connect(_mRepayWaterButton, &QPushButton::clicked, this, [this] {
		if(_mReminderManager && !_mReminderManager->repayOne(QStringLiteral("water")))
			statusBar()->showMessage(QStringLiteral("水债暂时为零，兄弟先记你一分。"), 3000);
	});
	connect(_mRepayBreakButton, &QPushButton::clicked, this, [this] {
		if(_mReminderManager && !_mReminderManager->repayOne(QStringLiteral("eye_break")))
			statusBar()->showMessage(QStringLiteral("休息债暂时为零，兄弟先记你一分。"), 3000);
	});
	connect(_mRepaySelectedButton, &QPushButton::clicked, this, &MainWindow::onRepaySelected);
	connect(_mPauseButton, &QPushButton::clicked, this, &MainWindow::onPauseReminders);
	connect(_mResumeButton, &QPushButton::clicked, this, &MainWindow::onResumeReminders);
	connect(_mQuietHoursEnabled, &QCheckBox::toggled, this, &MainWindow::onQuietHoursChanged);
	connect(_mQuietHoursStart, &QTimeEdit::timeChanged, this, &MainWindow::onQuietHoursChanged);
	connect(_mQuietHoursEnd, &QTimeEdit::timeChanged, this, &MainWindow::onQuietHoursChanged);
	connect(_mAutostart, &QCheckBox::toggled, this, &MainWindow::onAutostartToggled);
	connect(editPhrasesButton, &QPushButton::clicked, this, &MainWindow::editPhraseConfiguration);
	connect(reloadPhrasesButton, &QPushButton::clicked, this, [this] {
		if(_mReminderManager && !_mReminderManager->reloadPhrases())
			statusBar()->showMessage(_mReminderManager->phraseLoadError(), 6000);
		updateReminderState();
	});
	connect(previewPhrasesButton, &QPushButton::clicked, this, &MainWindow::previewPhrase);
	connect(openFolderButton, &QPushButton::clicked, this, &MainWindow::openPhraseFolder);
	connect(_mSyncBrightness, &QCheckBox::toggled, this, [this] {
		if(!_mUpdating)
			saveSettings();
	});
	connect(_mSyncNight, &QCheckBox::toggled, this, [this] {
		if(!_mUpdating)
			saveSettings();
	});
}

void MainWindow::setupTray() {
	QPixmap pixmap(22, 22);
	pixmap.fill(Qt::transparent);
	{
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setBrush(QColor(255, 180, 30));
		painter.setPen(Qt::NoPen);
		painter.drawEllipse(3, 3, 16, 16);
	}

	_mTrayIcon = new QSystemTrayIcon(QIcon(pixmap), this);
	_mTrayIcon->setToolTip("Protect Brother's Health");
	setWindowIcon(QIcon(pixmap));

	auto* trayMenu = new QMenu(this);
	trayMenu->addAction("白天模式 (6500K)", this, &MainWindow::setDayMode);
	trayMenu->addAction("护眼模式 (3500K)", this, &MainWindow::setNightMode);
	trayMenu->addSeparator();
	_mTrayDrinkAction  = trayMenu->addAction("喝了（本次）", this, &MainWindow::onCompleteWater);
	_mTraySnoozeAction = trayMenu->addAction("5 分钟后再提醒", this, &MainWindow::onSnoozeWater);
	_mTraySkipAction   = trayMenu->addAction("跳过并记一笔", this, &MainWindow::onSkipWater);
	trayMenu->addSeparator();
	_mTrayPauseAction  = trayMenu->addAction("暂停 30 分钟", this, &MainWindow::onPauseReminders);
	_mTrayResumeAction = trayMenu->addAction("恢复提醒", this, &MainWindow::onResumeReminders);
	trayMenu->addSeparator();
	trayMenu->addAction("兄弟债务账本", this, &MainWindow::openReminderTab);
	trayMenu->addAction("显示/隐藏", this, [this] { setVisible(!isVisible()); });
	trayMenu->addSeparator();
	trayMenu->addAction("退出", qApp, &QApplication::quit);

	_mTrayIcon->setContextMenu(trayMenu);
	connect(_mTrayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
	_mTrayIcon->show();

	if(!QSystemTrayIcon::isSystemTrayAvailable())
		statusBar()->showMessage(QStringLiteral("当前桌面未提供系统托盘；提醒会在窗口最小化时继续。"));
}

void MainWindow::setupReminderManager() {
	_mReminderManager = new ReminderManager(_mTrayIcon, this);
	connect(_mReminderManager, &ReminderManager::stateChanged, this, &MainWindow::updateReminderState);
	connect(_mReminderManager, &ReminderManager::openLedgerRequested, this, &MainWindow::openReminderTab);
	connect(_mReminderManager, &ReminderManager::breakStarted, this, &MainWindow::onBreakStarted);
	connect(_mReminderManager, &ReminderManager::breakFinished, this, &MainWindow::onBreakFinished);
	connect(_mReminderManager, &ReminderManager::breakSkipped, this, &MainWindow::onBreakSkipped);
	connect(_mReminderManager, &ReminderManager::countdownChanged, this, &MainWindow::onCountdownChanged);
	connect(_mReminderManager, &ReminderManager::statusMessage, this, &MainWindow::onReminderStatus);
	connect(_mReminderManager, &ReminderManager::errorOccurred, this, &MainWindow::onReminderError);

	_mReminderManager->setBreakMessage(_mBreakMessage->text());
	_mReminderManager->setQuietHours(_mQuietHoursEnabled->isChecked(),
									 _mQuietHoursStart->time().toString(QStringLiteral("HH:mm")),
									 _mQuietHoursEnd->time().toString(QStringLiteral("HH:mm")));
	_mReminderManager->setPausedUntil(_mSettings.pausedUntil());
	_mReminderManager->configure(buildReminderConfigs());
	_mReminderManager->start();
	updateReminderState();
	refreshTrayActions();
}

// ============================================================
//  Window events
// ============================================================

void MainWindow::closeEvent(QCloseEvent* event) {
	if(_mTrayIcon && _mTrayIcon->isVisible() && QSystemTrayIcon::isSystemTrayAvailable()) {
		hide();
		event->ignore();
		return;
	}

	const auto reply = QMessageBox::question(
		this,
		QStringLiteral("继续计时？"),
		QStringLiteral("当前桌面没有托盘。关闭窗口会停止提醒和计时。\n要最小化到任务栏继续运行吗？"),
		QMessageBox::Yes | QMessageBox::No,
		QMessageBox::Yes);
	if(reply == QMessageBox::Yes) {
		showMinimized();
		event->ignore();
	} else {
		event->accept();
	}
}

// ============================================================
//  Reminder settings and actions
// ============================================================

void MainWindow::onBreakSettingsChanged() {
	if(_mUpdating)
		return;
	applyReminderUiToSettings();
	saveSettings();
	if(_mReminderManager) {
		_mReminderManager->setBreakMessage(_mBreakMessage->text());
		_mReminderManager->configure(buildReminderConfigs());
	}
}

void MainWindow::onWaterSettingsChanged() {
	if(_mUpdating)
		return;
	applyReminderUiToSettings();
	saveSettings();
	if(_mReminderManager)
		_mReminderManager->configure(buildReminderConfigs());
}

void MainWindow::onQuietHoursChanged() {
	if(_mUpdating)
		return;
	applyReminderUiToSettings();
	saveSettings();
	if(_mReminderManager) {
		_mReminderManager->setQuietHours(_mQuietHoursEnabled->isChecked(),
										 _mQuietHoursStart->time().toString(QStringLiteral("HH:mm")),
										 _mQuietHoursEnd->time().toString(QStringLiteral("HH:mm")));
	}
}

void MainWindow::onAutostartToggled(bool enabled) {
	if(_mUpdating)
		return;
	QString error;
	if(!SettingsManager::applyAutostart(enabled, &error)) {
		statusBar()->showMessage(error, 6000);
		_mUpdating = true;
		_mAutostart->setChecked(!enabled);
		_mUpdating = false;
		return;
	}
	_mSettings.setAutostartEnabled(enabled);
	saveSettings();
}

void MainWindow::onPauseReminders() {
	if(!_mReminderManager)
		return;
	const auto until = localNow().addSecs(30 * 60);
	_mSettings.setPausedUntil(until);
	_mReminderManager->setPausedUntil(until);
	saveSettings();
	updateReminderState();
	statusBar()->showMessage(QStringLiteral("提醒暂停到 %1。").arg(until.toString(QStringLiteral("hh:mm"))),
							 5000);
}

void MainWindow::onResumeReminders() {
	if(!_mReminderManager)
		return;
	_mSettings.setPausedUntil(QDateTime());
	_mReminderManager->setPausedUntil(QDateTime());
	saveSettings();
	updateReminderState();
	statusBar()->showMessage(QStringLiteral("提醒已恢复。"), 3000);
}

void MainWindow::onCompleteWater() {
	if(_mReminderManager)
		_mReminderManager->completeTask(QStringLiteral("water"));
	updateReminderState();
}

void MainWindow::onSkipWater() {
	if(_mReminderManager)
		_mReminderManager->skipTask(QStringLiteral("water"));
	updateReminderState();
}

void MainWindow::onSnoozeWater() {
	if(_mReminderManager)
		_mReminderManager->snoozeTask(QStringLiteral("water"));
	updateReminderState();
}

void MainWindow::onRepaySelected() {
	if(!_mReminderManager)
		return;
	const auto taskId = _mRepayTaskCombo->currentData().toString();
	if(taskId.isEmpty())
		return;
	if(!_mReminderManager->repayOne(taskId))
		statusBar()->showMessage(QStringLiteral("这项欠账暂时为零，兄弟先记你一分。"), 3000);
	updateReminderState();
}

void MainWindow::onTriggerWater() {
	if(_mReminderManager && !_mReminderManager->triggerNow(QStringLiteral("water"))) {
		statusBar()->showMessage(_mWaterEnabled->isChecked() ? QStringLiteral("水杯提醒正在等待回应。")
															 : QStringLiteral("喝水提醒已暂停。"),
								 3000);
	}
	updateReminderState();
}

void MainWindow::onBreakStarted(int breakMinutes) {
	statusBar()->showMessage(QStringLiteral("休息开始：兄弟把屏幕收走了 %1 分钟。").arg(breakMinutes), 5000);
	updateReminderState();
}

void MainWindow::onBreakFinished() {
	statusBar()->showMessage(QStringLiteral("休息结束，兄弟把键盘还给你了。"), 4000);
	updateReminderState();
}

void MainWindow::onBreakSkipped() {
	statusBar()->showMessage(QStringLiteral("这次休息记在兄弟账上了。"), 4000);
	updateReminderState();
}

void MainWindow::onReminderStatus(const QString& message) { statusBar()->showMessage(message, 6000); }

void MainWindow::onReminderError(const QString& message) {
	statusBar()->showMessage(message, 10000);
	// The phrase panel owns its own error text and is refreshed by
	// updateReminderState(); ledger errors must not masquerade as phrase errors.
}

void MainWindow::onCountdownChanged(const QTime& remaining, bool working) {
	if(!_mReminderManager)
		return;
	if(!_mBreakEnabled->isChecked()) {
		_mCountdownLabel->setText(QStringLiteral("休息提醒已暂停"));
		return;
	}
	const auto phase = working ? QStringLiteral("距离休息还有") : QStringLiteral("休息结束还剩");
	_mCountdownLabel->setText(QStringLiteral("%1 %2").arg(phase, remaining.toString("hh:mm:ss")));
}

void MainWindow::updateReminderState() {
	if(!_mReminderManager)
		return;

	_mDebtReportLabel->setText(_mReminderManager->debtReport());
	_mActionPromptLabel->setText(_mReminderManager->actionPrompt());
	_mTaskStatusLabel->setText(taskStatusText());

	const auto* water	   = _mSettings.reminder(QStringLiteral("water"));
	QString		phrasePath = QStringLiteral("用户文案：%1").arg(_mReminderManager->phraseConfigPath());
	if(_mReminderManager->phraseSourcePath() != _mReminderManager->phraseConfigPath())
		phrasePath += QStringLiteral("\n实际读取：%1").arg(_mReminderManager->phraseSourcePath());
	if(water)
		phrasePath += QStringLiteral("\n喝水单次：%1 %2").arg(water->_unitsPerEvent).arg(water->_unit);
	_mPhrasePathLabel->setText(phrasePath);

	const auto error = _mReminderManager->phraseLoadError();
	_mPhraseErrorLabel->setText(error);
	_mPhraseErrorLabel->setVisible(!error.isEmpty());

	const bool pending = _mReminderManager->hasPending(QStringLiteral("water"));
	if(pending) {
		_mWaterPendingLabel->setText(
			QStringLiteral("水杯提醒正在等你回应：%1 ml。可以喝了、稍后提醒，或跳过记一笔。")
				.arg(_mReminderManager->pendingUnits(QStringLiteral("water"))));
	} else if(_mReminderManager->isBreaking()) {
		_mWaterPendingLabel->setText(QStringLiteral("正在休息，喝水提醒会等你回来。"));
	} else if(!_mWaterEnabled->isChecked()) {
		_mWaterPendingLabel->setText(QStringLiteral("喝水提醒已暂停。已有欠账仍会保留在兄弟账本里。"));
	} else {
		_mWaterPendingLabel->setText(
			QStringLiteral("喝水提醒每 %1 分钟发送一次。").arg(_mWaterIntervalMinutes->value()));
	}

	_mCompleteWaterButton->setEnabled(pending);
	_mSnoozeWaterButton->setEnabled(pending);
	_mSkipWaterButton->setEnabled(pending);
	_mTriggerWaterButton->setEnabled(_mWaterEnabled->isChecked() && !pending);
	_mRepayWaterButton->setEnabled(_mReminderManager->outstandingCount(QStringLiteral("water")) > 0);
	_mRepayBreakButton->setEnabled(_mReminderManager->outstandingCount(QStringLiteral("eye_break")) > 0);

	const auto previousRepayTask = _mRepayTaskCombo->currentData().toString();
	_mRepayTaskCombo->blockSignals(true);
	_mRepayTaskCombo->clear();
	QStringList listedTasks;
	for(const auto& task : _mReminderManager->tasks()) {
		const auto count = _mReminderManager->outstandingCount(task._id);
		if(count <= 0)
			continue;
		_mRepayTaskCombo->addItem(QStringLiteral("%1（欠 %2 次 / %3 %4）")
									  .arg(task._name)
									  .arg(count)
									  .arg(_mReminderManager->outstandingUnits(task._id))
									  .arg(task._unit),
								  task._id);
		listedTasks.append(task._id);
	}
	for(const auto& taskId : _mReminderManager->outstandingTaskIds()) {
		if(listedTasks.contains(taskId))
			continue;
		_mRepayTaskCombo->addItem(
			QStringLiteral("%1（欠 %2 次）").arg(taskId).arg(_mReminderManager->outstandingCount(taskId)),
			taskId);
	}
	const auto previousIndex = _mRepayTaskCombo->findData(previousRepayTask);
	if(previousIndex >= 0)
		_mRepayTaskCombo->setCurrentIndex(previousIndex);
	_mRepayTaskCombo->blockSignals(false);
	_mRepaySelectedButton->setEnabled(_mRepayTaskCombo->count() > 0);

	const bool paused = _mSettings.pausedUntil().isValid() && _mSettings.pausedUntil() > localNow();
	_mPauseButton->setEnabled(!paused);
	_mResumeButton->setEnabled(paused);
	refreshTrayActions();
}

QString MainWindow::taskStatusText() const {
	if(!_mReminderManager)
		return {};
	QStringList lines;
	for(const auto& task : _mReminderManager->tasks()) {
		QString status;
		if(task._id == QStringLiteral("eye_break") && _mReminderManager->isBreaking())
			status = QStringLiteral("休息中");
		else if(_mReminderManager->hasPending(task._id))
			status = QStringLiteral("等待回应（%1 %2）")
						 .arg(_mReminderManager->pendingUnits(task._id))
						 .arg(task._unit);
		else
			status = QStringLiteral("下次 %1").arg(formatDue(_mReminderManager->nextDue(task._id)));
		lines.append(
			QStringLiteral("%1：%2 · %3 · 欠 %4 次 / %5 %6")
				.arg(task._name, task._enabled ? QStringLiteral("启用") : QStringLiteral("暂停"), status)
				.arg(_mReminderManager->outstandingCount(task._id))
				.arg(_mReminderManager->outstandingUnits(task._id))
				.arg(task._unit));
	}
	return lines.join(QLatin1Char('\n'));
}

void MainWindow::refreshTrayActions() {
	if(!_mReminderManager || !_mTrayDrinkAction)
		return;
	const bool waterPending = _mReminderManager->hasPending(QStringLiteral("water"));
	_mTrayDrinkAction->setEnabled(waterPending);
	_mTraySnoozeAction->setEnabled(waterPending);
	_mTraySkipAction->setEnabled(waterPending);
	_mTrayDrinkAction->setText(
		waterPending
			? QStringLiteral("喝了（%1 ml）").arg(_mReminderManager->pendingUnits(QStringLiteral("water")))
			: QStringLiteral("喝了（本次）"));
	const bool paused = _mSettings.pausedUntil().isValid() && _mSettings.pausedUntil() > localNow();
	_mTrayPauseAction->setEnabled(!paused);
	_mTrayResumeAction->setEnabled(paused);

	QString tooltip = QStringLiteral("Protect Brother's Health\n债务分：%1（%2）")
						  .arg(_mReminderManager->score())
						  .arg(_mReminderManager->levelName());
	if(waterPending)
		tooltip += QStringLiteral("\n水杯在等你：%1 ml")
					   .arg(_mReminderManager->pendingUnits(QStringLiteral("water")));
	if(_mReminderManager->isBreaking())
		tooltip += QStringLiteral("\n正在休息");
	_mTrayIcon->setToolTip(tooltip);
}

void MainWindow::editPhraseConfiguration() {
	if(!_mReminderManager)
		return;
	QString error;
	if(!_mReminderManager->preparePhraseConfig(&error)) {
		statusBar()->showMessage(error.isEmpty() ? QStringLiteral("无法准备文案文件") : error, 8000);
		updateReminderState();
		return;
	}
	const auto path = _mReminderManager->phraseConfigPath();
	if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
		statusBar()->showMessage(QStringLiteral("文案文件已准备好：%1").arg(path), 5000);
	updateReminderState();
}

void MainWindow::previewPhrase() {
	updateReminderState();
	statusBar()->showMessage(QStringLiteral("已按当前账本重新抽取文案。"), 3000);
}

void MainWindow::openPhraseFolder() {
	const auto path = _mReminderManager ? _mReminderManager->phraseConfigPath() : QString();
	const auto dir	= QFileInfo(path).absolutePath();
	if(!QDesktopServices::openUrl(QUrl::fromLocalFile(dir)))
		statusBar()->showMessage(QStringLiteral("文案目录：%1").arg(dir), 5000);
}

void MainWindow::openReminderTab() {
	if(_mReminderTabIndex >= 0)
		_mTabWidget->setCurrentIndex(_mReminderTabIndex);
	show();
	raise();
	activateWindow();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
	if(reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
		setVisible(!isVisible());
		if(isVisible()) {
			raise();
			activateWindow();
		}
	}
}

// ============================================================
//  Mode presets (tray)
// ============================================================

void MainWindow::setDayMode() {
	for(int i = 0; i < _mMonitors.size(); ++i)
		_mMonitors[i]._nightMode->setChecked(false);
}

void MainWindow::setNightMode() {
	for(int i = 0; i < _mMonitors.size(); ++i) {
		_mMonitors[i]._nightMode->setChecked(true);
		_mMonitors[i]._warmthSlider->setValue(3500);
	}
}

// ============================================================
//  Display handlers
// ============================================================

void MainWindow::onBrightnessChanged(int idx, int value) {
	if(_mUpdating)
		return;
	_mUpdating = true;

	if(_mSyncBrightness->isChecked()) {
		for(int i = 0; i < _mMonitors.size(); ++i) {
			if(i == idx)
				continue;
			_mMonitors[i]._brightnessSlider->blockSignals(true);
			_mMonitors[i]._brightnessSlider->setValue(value);
			_mMonitors[i]._brightnessSlider->blockSignals(false);
			_mMonitors[i]._brightnessSpin->blockSignals(true);
			_mMonitors[i]._brightnessSpin->setValue(value);
			_mMonitors[i]._brightnessSpin->blockSignals(false);
		}
	}

	if(_mSyncBrightness->isChecked()) {
		for(int i = 0; i < _mMonitors.size(); ++i)
			applyMonitor(i);
	} else {
		applyMonitor(idx);
	}

	_mUpdating = false;
	saveSettings();
}

void MainWindow::onNightModeToggled(int idx, bool checked) {
	if(_mUpdating)
		return;
	_mUpdating = true;

	_mMonitors[idx]._warmthContainer->setVisible(checked);

	if(_mSyncNight->isChecked()) {
		for(int i = 0; i < _mMonitors.size(); ++i) {
			if(i == idx)
				continue;
			_mMonitors[i]._nightMode->blockSignals(true);
			_mMonitors[i]._nightMode->setChecked(checked);
			_mMonitors[i]._nightMode->blockSignals(false);
			_mMonitors[i]._warmthContainer->setVisible(checked);
		}
	}

	if(_mSyncNight->isChecked()) {
		for(int i = 0; i < _mMonitors.size(); ++i)
			applyMonitor(i);
	} else {
		applyMonitor(idx);
	}

	_mUpdating = false;
	saveSettings();
}

void MainWindow::onWarmthChanged(int idx, int value) {
	if(_mUpdating)
		return;
	_mUpdating = true;

	_mMonitors[idx]._warmthSpin->setValue(value);

	if(_mSyncNight->isChecked()) {
		for(int i = 0; i < _mMonitors.size(); ++i) {
			if(i == idx)
				continue;
			_mMonitors[i]._warmthSlider->blockSignals(true);
			_mMonitors[i]._warmthSlider->setValue(value);
			_mMonitors[i]._warmthSlider->blockSignals(false);
			_mMonitors[i]._warmthSpin->blockSignals(true);
			_mMonitors[i]._warmthSpin->setValue(value);
			_mMonitors[i]._warmthSpin->blockSignals(false);
		}
	}

	if(_mSyncNight->isChecked()) {
		for(int i = 0; i < _mMonitors.size(); ++i)
			applyMonitor(i);
	} else {
		applyMonitor(idx);
	}

	_mUpdating = false;
	saveSettings();
}

void MainWindow::applyMonitor(int idx) {
	auto&	  widgets	  = _mMonitors[idx];
	double	  brightness  = widgets._brightnessSlider->value() / 100.0;
	const int temperature = widgets._nightMode->isChecked() ? widgets._warmthSlider->value() : 6500;
	QString	  error;
	if(!MonitorController::apply(widgets._outputName, brightness, temperature, &error))
		statusBar()->showMessage(QStringLiteral("显示设置未应用：%1").arg(error), 5000);
}

// ============================================================
//  Settings
// ============================================================

void MainWindow::loadSettings() {
	_mSettings.load();
	if(!_mSettings.loadWarning().isEmpty())
		statusBar()->showMessage(_mSettings.loadWarning(), 12000);

	_mUpdating = true;

	_mSyncBrightness->setChecked(_mSettings.syncBrightness());
	_mSyncNight->setChecked(_mSettings.syncNight());

	const auto* water = _mSettings.reminder(QStringLiteral("water"));
	if(water) {
		_mWaterEnabled->setChecked(water->_enabled);
		_mWaterIntervalMinutes->setValue(water->_intervalMinutes);
		_mWaterVolumeMilliliters->setValue(water->_unitsPerEvent);
		_mWaterWeight->setValue(water->_weight);
	}
	const auto* eyeBreak = _mSettings.reminder(QStringLiteral("eye_break"));
	if(eyeBreak) {
		_mBreakEnabled->setChecked(eyeBreak->_enabled);
		_mWorkMinutes->setValue(eyeBreak->_intervalMinutes);
		_mBreakMinutes->setValue(eyeBreak->_breakMinutes);
		_mBreakWeight->setValue(eyeBreak->_weight);
		_mBreakMessage->setText(eyeBreak->_message);
	}

	_mQuietHoursEnabled->setChecked(_mSettings.quietHoursEnabled());
	_mQuietHoursStart->setTime(QTime::fromString(_mSettings.quietHoursStart(), QStringLiteral("HH:mm")));
	_mQuietHoursEnd->setTime(QTime::fromString(_mSettings.quietHoursEnd(), QStringLiteral("HH:mm")));
	_mAutostart->setChecked(_mSettings.autostartEnabled());
	if(_mSettings.autostartEnabled()) {
		QString autostartError;
		if(!SettingsManager::applyAutostart(true, &autostartError))
			statusBar()->showMessage(autostartError, 6000);
	}

	const auto monitors = MonitorController::discover();
	for(int i = 0; i < _mMonitors.size(); ++i) {
		auto&		widgets = _mMonitors[i];
		const auto& name	= widgets._outputName;
		bool		found	= false;
		for(const auto& monitor : _mSettings.monitors()) {
			if(monitor._name == name) {
				found = true;
				widgets._brightnessSlider->setValue(monitor._brightness);
				widgets._nightMode->setChecked(monitor._nightMode);
				widgets._warmthContainer->setVisible(monitor._nightMode);
				if(monitor._nightMode)
					widgets._warmthSlider->setValue(monitor._warmth);
				break;
			}
		}

		if(!found) {
			auto monitor = std::find_if(monitors.begin(), monitors.end(), [&](const MonitorInfo& info) {
				return info._name == name;
			});
			if(monitor != monitors.end()) {
				int brightness = static_cast<int>(monitor->_brightness * 100.0);
				brightness	   = std::clamp(brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
				widgets._brightnessSlider->setValue(brightness);
				const auto kelvin = estimateKelvin(monitor->_gammaR, monitor->_gammaG, monitor->_gammaB);
				if(kelvin != 6500) {
					widgets._nightMode->setChecked(true);
					widgets._warmthContainer->setVisible(true);
					widgets._warmthSlider->setValue(static_cast<int>(kelvin));
				}
			}
		}
	}

	const auto geometry = _mSettings.windowGeometry();
	if(!geometry.isEmpty())
		restoreGeometry(geometry);

	const auto defaultPreset  = _mSettings.defaultPreset();
	bool	   defaultApplied = false;
	if(!defaultPreset.isEmpty()) {
		const auto preset = _mPresets.load(defaultPreset);
		if(!preset.isEmpty()) {
			_mUpdating = false;
			applyPreset(preset);
			defaultApplied = true;
		} else {
			_mSettings.setDefaultPreset(QString());
		}
	}

	_mUpdating = false;
	refreshPresetList();

	if(!defaultApplied) {
		for(int i = 0; i < _mMonitors.size(); ++i)
			applyMonitor(i);
	}

	saveSettings();
}

void MainWindow::saveSettings() {
	_mSettings.setSyncBrightness(_mSyncBrightness->isChecked());
	_mSettings.setSyncNight(_mSyncNight->isChecked());
	_mSettings.setQuietHoursEnabled(_mQuietHoursEnabled->isChecked());
	_mSettings.setQuietHours(_mQuietHoursStart->time().toString(QStringLiteral("HH:mm")),
							 _mQuietHoursEnd->time().toString(QStringLiteral("HH:mm")));
	_mSettings.setAutostartEnabled(_mAutostart->isChecked());

	applyReminderUiToSettings();

	QVector<MonitorSettings> current;
	for(const auto& widgets : _mMonitors) {
		MonitorSettings monitor;
		monitor._name		= widgets._outputName;
		monitor._brightness = widgets._brightnessSlider->value();
		monitor._nightMode	= widgets._nightMode->isChecked();
		if(monitor._nightMode)
			monitor._warmth = widgets._warmthSlider->value();
		current.append(monitor);
	}
	_mSettings.monitors() = SettingsManager::mergeMonitors(_mSettings.monitors(), current);

	_mSettings.setWindowGeometry(saveGeometry());
	_mSettings.save();
}

void MainWindow::applyReminderUiToSettings() {
	auto water = _mSettings.reminder(QStringLiteral("water"));
	if(!water) {
		const auto defaults = SettingsManager::defaultReminders();
		for(const auto& config : defaults) {
			if(config._id == QStringLiteral("water"))
				_mSettings.setReminder(config);
		}
		water = _mSettings.reminder(QStringLiteral("water"));
	}
	if(water) {
		water->_enabled			= _mWaterEnabled->isChecked();
		water->_intervalMinutes = _mWaterIntervalMinutes->value();
		water->_unitsPerEvent	= _mWaterVolumeMilliliters->value();
		water->_weight			= _mWaterWeight->value();
	}

	auto eyeBreak = _mSettings.reminder(QStringLiteral("eye_break"));
	if(!eyeBreak) {
		const auto defaults = SettingsManager::defaultReminders();
		for(const auto& config : defaults) {
			if(config._id == QStringLiteral("eye_break"))
				_mSettings.setReminder(config);
		}
		eyeBreak = _mSettings.reminder(QStringLiteral("eye_break"));
	}
	if(eyeBreak) {
		eyeBreak->_enabled		   = _mBreakEnabled->isChecked();
		eyeBreak->_intervalMinutes = _mWorkMinutes->value();
		eyeBreak->_breakMinutes	   = _mBreakMinutes->value();
		eyeBreak->_unitsPerEvent   = _mBreakMinutes->value();
		eyeBreak->_weight		   = _mBreakWeight->value();
		eyeBreak->_message		   = _mBreakMessage->text().trimmed();
	}
}

QVector<ReminderConfig> MainWindow::buildReminderConfigs() const {
	auto tasks	= _mSettings.reminders();

	auto ensure = [&](const QString& id) -> ReminderConfig& {
		for(auto& config : tasks) {
			if(config._id == id)
				return config;
		}
		const auto defaults = SettingsManager::defaultReminders();
		for(const auto& config : defaults) {
			if(config._id == id) {
				tasks.append(config);
				return tasks.last();
			}
		}
		ReminderConfig fallback;
		fallback._id   = id;
		fallback._name = id;
		fallback._unit = QStringLiteral("次");
		tasks.append(fallback);
		return tasks.last();
	};

	auto& water				  = ensure(QStringLiteral("water"));
	water._enabled			  = _mWaterEnabled->isChecked();
	water._intervalMinutes	  = _mWaterIntervalMinutes->value();
	water._unitsPerEvent	  = _mWaterVolumeMilliliters->value();
	water._weight			  = _mWaterWeight->value();

	auto& eyeBreak			  = ensure(QStringLiteral("eye_break"));
	eyeBreak._enabled		  = _mBreakEnabled->isChecked();
	eyeBreak._intervalMinutes = _mWorkMinutes->value();
	eyeBreak._breakMinutes	  = _mBreakMinutes->value();
	eyeBreak._unitsPerEvent	  = _mBreakMinutes->value();
	eyeBreak._weight		  = _mBreakWeight->value();
	eyeBreak._message		  = _mBreakMessage->text().trimmed();

	return tasks;
}

// ============================================================
//  Presets
// ============================================================

void MainWindow::refreshPresetList() {
	_mPresetList->clear();
	const auto defaultPreset = _mSettings.defaultPreset();
	for(const auto& name : _mPresets.listPresets()) {
		const auto display = (name == defaultPreset) ? QStringLiteral("%1  (默认)").arg(name) : name;
		auto*	   item	   = new QListWidgetItem(display);
		if(name == defaultPreset) {
			QFont font = item->font();
			font.setBold(true);
			item->setFont(font);
		}
		item->setData(Qt::UserRole, name);
		_mPresetList->addItem(item);
	}
}

void MainWindow::saveCurrentAsPreset() {
	const auto name = _mPresetNameEdit->text().trimmed();
	if(name.isEmpty()) {
		QMessageBox::information(this, "提示", "请输入预设名称");
		return;
	}

	applyReminderUiToSettings();
	QJsonObject preset;
	preset["name"]			  = name;
	preset["sync_brightness"] = _mSyncBrightness->isChecked();
	preset["sync_night"]	  = _mSyncNight->isChecked();

	QJsonArray reminders;
	for(const auto& config : buildReminderConfigs()) {
		QJsonObject object;
		object["id"]			   = config._id;
		object["name"]			   = config._name;
		object["unit"]			   = config._unit;
		object["kind"]			   = config._kind;
		object["message"]		   = config._message;
		object["enabled"]		   = config._enabled;
		object["interval_minutes"] = config._intervalMinutes;
		object["break_minutes"]	   = config._breakMinutes;
		object["units_per_event"]  = config._unitsPerEvent;
		object["weight"]		   = config._weight;
		reminders.append(object);
	}
	preset["reminders"] = reminders;

	QJsonArray monitors;
	for(const auto& widgets : _mMonitors) {
		QJsonObject object;
		object["name"]		 = widgets._outputName;
		object["brightness"] = widgets._brightnessSlider->value();
		object["night_mode"] = widgets._nightMode->isChecked();
		if(widgets._nightMode->isChecked())
			object["warmth"] = widgets._warmthSlider->value();
		monitors.append(object);
	}
	preset["monitors"] = monitors;

	if(_mPresets.save(name, preset)) {
		_mPresetNameEdit->clear();
		refreshPresetList();
	} else {
		QMessageBox::warning(this,
							 QStringLiteral("保存失败"),
							 QStringLiteral("预设名称不能包含路径分隔符，或预设文件无法写入。"));
	}
}

void MainWindow::loadSelectedPreset() {
	const auto items = _mPresetList->selectedItems();
	if(items.isEmpty())
		return;
	const auto preset = _mPresets.load(items[0]->data(Qt::UserRole).toString());
	if(preset.isEmpty())
		return;
	applyPreset(preset);
	saveSettings();
}

void MainWindow::onPresetListDoubleClicked(QListWidgetItem* item) {
	const auto preset = _mPresets.load(item->data(Qt::UserRole).toString());
	if(preset.isEmpty())
		return;
	applyPreset(preset);
	saveSettings();
}

void MainWindow::deleteSelectedPreset() {
	const auto items = _mPresetList->selectedItems();
	if(items.isEmpty())
		return;
	const auto name	 = items[0]->data(Qt::UserRole).toString();
	const auto reply = QMessageBox::question(this,
											 "确认删除",
											 QStringLiteral("确定要删除预设「%1」吗？").arg(name),
											 QMessageBox::Yes | QMessageBox::No);
	if(reply == QMessageBox::Yes) {
		_mPresets.remove(name);
		if(_mSettings.defaultPreset() == name)
			_mSettings.setDefaultPreset(QString());
		refreshPresetList();
		saveSettings();
	}
}

void MainWindow::setSelectedAsDefault() {
	const auto items = _mPresetList->selectedItems();
	if(items.isEmpty())
		return;
	const auto name = items[0]->data(Qt::UserRole).toString();
	_mSettings.setDefaultPreset(name);
	refreshPresetList();
	saveSettings();
	statusBar()->showMessage(QStringLiteral("已将「%1」设为默认预设").arg(name), 3000);
}

void MainWindow::applyPreset(const QJsonObject& preset) {
	_mUpdating = true;

	_mSyncBrightness->setChecked(preset["sync_brightness"].toBool(false));
	_mSyncNight->setChecked(preset["sync_night"].toBool(false));

	if(preset.contains("reminders")) {
		for(const auto& value : preset["reminders"].toArray()) {
			const auto object = value.toObject();
			const auto id	  = object["id"].toString();
			if(id.isEmpty())
				continue;
			ReminderConfig config;
			config._id				= id;
			config._name			= object["name"].toString(id);
			config._unit			= object["unit"].toString();
			config._kind			= object["kind"].toString(QStringLiteral("periodic"));
			config._message			= object["message"].toString();
			config._enabled			= object["enabled"].toBool(true);
			config._intervalMinutes = object["interval_minutes"].toInt(50);
			config._breakMinutes	= object["break_minutes"].toInt(20);
			config._unitsPerEvent	= object["units_per_event"].toInt(config.unitsPerEvent());
			config._weight			= object["weight"].toInt(1);
			_mSettings.setReminder(config);
			if(id == "water") {
				_mWaterEnabled->setChecked(config._enabled);
				_mWaterIntervalMinutes->setValue(config._intervalMinutes);
				_mWaterVolumeMilliliters->setValue(config._unitsPerEvent);
				_mWaterWeight->setValue(config._weight);
			} else if(id == "eye_break") {
				_mBreakEnabled->setChecked(config._enabled);
				_mWorkMinutes->setValue(config._intervalMinutes);
				_mBreakMinutes->setValue(config._breakMinutes);
				_mBreakWeight->setValue(config._weight);
				_mBreakMessage->setText(config._message);
			}
		}
	} else {
		// Legacy presets stored "break" and "water" objects.
		setBreakControls(preset.value("break").toObject());
		const auto water = preset.value("water").toObject();
		if(water.contains("enabled"))
			_mWaterEnabled->setChecked(water.value("enabled").toBool(true));
		if(water.contains("interval_minutes"))
			_mWaterIntervalMinutes->setValue(water.value("interval_minutes").toInt(50));
		if(water.contains("volume_ml"))
			_mWaterVolumeMilliliters->setValue(water.value("volume_ml").toInt(250));
	}

	const auto monitors = preset["monitors"].toArray();
	for(const auto& value : monitors) {
		const auto object = value.toObject();
		const auto name	  = object["name"].toString();
		for(auto& widgets : _mMonitors) {
			if(widgets._outputName != name)
				continue;
			widgets._brightnessSlider->setValue(object["brightness"].toInt(100));
			const bool night = object["night_mode"].toBool(false);
			widgets._nightMode->setChecked(night);
			widgets._warmthContainer->setVisible(night);
			if(night)
				widgets._warmthSlider->setValue(object["warmth"].toInt(5000));
			break;
		}
	}

	_mUpdating = false;

	for(int i = 0; i < _mMonitors.size(); ++i)
		applyMonitor(i);

	saveSettings();
	if(_mReminderManager) {
		_mReminderManager->setBreakMessage(_mBreakMessage->text());
		_mReminderManager->configure(buildReminderConfigs());
	}
	updateReminderState();
}

void MainWindow::setBreakControls(const QJsonObject& settings) {
	if(settings.contains("enabled"))
		_mBreakEnabled->setChecked(settings.value("enabled").toBool(true));
	if(settings.contains("work_minutes"))
		_mWorkMinutes->setValue(settings.value("work_minutes").toInt(120));
	if(settings.contains("break_minutes"))
		_mBreakMinutes->setValue(settings.value("break_minutes").toInt(20));
	if(settings.contains("message"))
		_mBreakMessage->setText(settings.value("message").toString());
}
