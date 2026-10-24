#include "mainwindow.h"
#include "color_temp.h"
#include "monitor_ctrl.h"

#include <QApplication>
#include <QCloseEvent>
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
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <qnamespace.h>
#include <qspinbox.h>

static constexpr int BRIGHTNESS_MIN = 10;
static constexpr int BRIGHTNESS_MAX = 200;
static constexpr int WARMTH_MIN		= 1000;
static constexpr int WARMTH_MAX		= 7500;

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent) {

	setWindowTitle("Protect Brother's Health");
	setMinimumWidth(400);

	for(const auto& mi : MonitorController::discover()) {
		MonitorWidgets mw;
		mw._outputName = mi._name;
		_mMonitors.append(mw);
	}

	setupUi();
	setupTray();
	loadSettings();
	setupBreakScheduler();

	connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::saveSettings);
}

// ============================================================
//  UI Setup
// ============================================================

void MainWindow::setupUi() {
	auto* central	 = new QWidget(this);
	auto* mainLayout = new QVBoxLayout(central);
	mainLayout->setContentsMargins(12, 12, 12, 12);
	mainLayout->setSpacing(8);

	_mTabWidget			= new QTabWidget;

	// ---------- Tab 1: Display ----------
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
		auto* mw	= &_mMonitors[i];

		auto* group = new QGroupBox;
		auto* gl	= new QVBoxLayout(group);
		gl->setSpacing(6);

		// Monitor name
		const auto displayName = mw->_outputName == QStringLiteral("wayland-global")
									 ? QStringLiteral("Wayland（所有显示器）")
									 : mw->_outputName;
		auto*	   name		   = new QLabel(displayName);
		QFont	   nameFont	   = name->font();
		nameFont.setBold(true);
		nameFont.setPointSize(nameFont.pointSize() + 1);
		name->setFont(nameFont);
		gl->addWidget(name);

		// Brightness row
		auto* brRow	  = new QHBoxLayout;
		auto* brLabel = new QLabel("亮度");
		brLabel->setFixedWidth(36);
		auto* brSlider = new QSlider(Qt::Horizontal);
		brSlider->setRange(BRIGHTNESS_MIN, MonitorController::isWaylandSession() ? 100 : BRIGHTNESS_MAX);

		// bright spin box
		auto* brSpin = new QSpinBox;
		brSpin->setRange(BRIGHTNESS_MIN, MonitorController::isWaylandSession() ? 100 : BRIGHTNESS_MAX);
		brSpin->setSuffix("%");
		brSpin->setSingleStep(1);
		brSpin->setKeyboardTracking(false);
		brSpin->setAccelerated(true);
		brSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		brSpin->setValue(100);

		brSlider->setValue(100);

		brRow->addWidget(brLabel);
		brRow->addWidget(brSlider, 1);
		brRow->addWidget(brSpin);
		gl->addLayout(brRow);

		// Night mode toggle
		auto* night = new QCheckBox("护眼模式");
		gl->addWidget(night);

		// Warmth row (hidden until night mode on)
		auto* wmRow	  = new QHBoxLayout;
		auto* wmLabel = new QLabel("色温");
		wmLabel->setFixedWidth(36);
		auto* wmSlider = new QSlider(Qt::Horizontal);
		wmSlider->setRange(WARMTH_MIN, WARMTH_MAX);
		wmSlider->setSingleStep(50);
		wmSlider->setPageStep(500);
		wmSlider->setValue(5000);

		// wm spin box
		auto* wmSpin = new QSpinBox;
		wmSpin->setRange(WARMTH_MIN, WARMTH_MAX);
		wmSpin->setSuffix("K");
		wmSpin->setSingleStep(50);
		wmSpin->setKeyboardTracking(false);
		wmSpin->setAccelerated(true);
		wmSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		wmSpin->setValue(5000);

		wmRow->addWidget(wmLabel);
		wmRow->addWidget(wmSlider, 1);
		wmRow->addWidget(wmSpin);

		auto* wmContainer = new QWidget;
		wmContainer->setLayout(wmRow);
		wmContainer->setVisible(false);
		gl->addWidget(wmContainer);

		monitorsLayout->addWidget(group);

		mw->_nameLabel		  = name;
		mw->_brightnessSlider = brSlider;
		mw->_brightnessSpin	  = brSpin;
		mw->_nightMode		  = night;
		mw->_warmthSlider	  = wmSlider;
		mw->_warmthSpin		  = wmSpin;
		mw->_warmthContainer  = wmContainer;

		// Brightness connections
		connect(brSlider, &QSlider::valueChanged, this, [this, i](int v) { onBrightnessChanged(i, v); });

		connect(brSlider, &QSlider::valueChanged, brSpin, &QSpinBox::setValue);
		connect(brSpin, QOverload<int>::of(&QSpinBox::valueChanged), brSlider, &QSlider::setValue);

		connect(night, &QCheckBox::toggled, this, [this, i](bool c) { onNightModeToggled(i, c); });

		// Warmth connections
		connect(wmSlider, &QSlider::valueChanged, this, [this, i](int v) { onWarmthChanged(i, v); });
		connect(wmSlider, &QSlider::valueChanged, wmSpin, &QSpinBox::setValue);
		connect(wmSpin, QOverload<int>::of(&QSpinBox::valueChanged), wmSlider, &QSlider::setValue);
	}

	if(_mMonitors.isEmpty()) {
		auto* unavailable = new QLabel(MonitorController::isWaylandSession()
										   ? QStringLiteral("未检测到可调整的显示器；休息提醒仍可使用。")
										   : QStringLiteral("未检测到 xrandr 显示输出；休息提醒仍可使用。"));
		unavailable->setWordWrap(true);
		unavailable->setStyleSheet(QStringLiteral("color: #a33;"));
		monitorsLayout->addWidget(unavailable);
	} else if(MonitorController::isWaylandSession()) {
		auto* waylandHint =
			new QLabel(QStringLiteral("Wayland "
									  "色温和亮度由桌面合成器统一调整，休息提醒会显示在每个屏幕。"));
		waylandHint->setWordWrap(true);
		waylandHint->setStyleSheet(QStringLiteral("color: gray;"));
		monitorsLayout->addWidget(waylandHint);
	}

	scroll->setWidget(scrollContent);
	displayLayout->addWidget(scroll, 1);

	// Separator
	auto* sep = new QFrame;
	sep->setFrameShape(QFrame::HLine);
	sep->setFrameShadow(QFrame::Sunken);
	displayLayout->addWidget(sep);

	// Sync toggles
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

	// ---------- Tab 2: Break reminders ----------
	auto* breakTab	  = new QWidget;
	auto* breakLayout = new QVBoxLayout(breakTab);
	breakLayout->setContentsMargins(12, 12, 12, 12);
	breakLayout->setSpacing(10);

	_mBreakEnabled = new QCheckBox(QStringLiteral("启用定时休息提醒"));
	breakLayout->addWidget(_mBreakEnabled);

	auto* breakForm = new QFormLayout;
	_mWorkMinutes	= new QSpinBox;
	_mWorkMinutes->setRange(1, 150);
	_mWorkMinutes->setSuffix(QStringLiteral(" 分钟"));
	_mWorkMinutes->setValue(120);
	_mBreakMinutes = new QSpinBox;
	_mBreakMinutes->setRange(1, 150);
	_mBreakMinutes->setSuffix(QStringLiteral(" 分钟"));
	_mBreakMinutes->setValue(20);
	_mBreakMessage = new QLineEdit;
	_mBreakMessage->setPlaceholderText(QStringLiteral("输入每个屏幕都显示的休息提示"));
	breakForm->addRow(QStringLiteral("工作时长"), _mWorkMinutes);
	breakForm->addRow(QStringLiteral("休息时长"), _mBreakMinutes);
	breakForm->addRow(QStringLiteral("提示语句"), _mBreakMessage);
	breakLayout->addLayout(breakForm);

	_mCountdownLabel = new QLabel(QStringLiteral("休息提醒尚未启动"));
	_mCountdownLabel->setAlignment(Qt::AlignCenter);
	breakLayout->addWidget(_mCountdownLabel);
	breakLayout->addStretch();
	_mTabWidget->addTab(breakTab, QStringLiteral("休息"));

	// ---------- Tab 3: Presets ----------
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

	// Save as preset
	auto* saveRow	 = new QHBoxLayout;
	_mPresetNameEdit = new QLineEdit;
	_mPresetNameEdit->setPlaceholderText("输入预设名称...");
	auto* saveBtn = new QPushButton("保存当前为预设");
	saveRow->addWidget(_mPresetNameEdit, 1);
	saveRow->addWidget(saveBtn);
	presetsLayout->addLayout(saveRow);

	// Load / Delete / Set default
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

	// Hint
	auto* hint = new QLabel("关闭窗口后进入系统托盘；桌面没有托盘服务时会退出程序。");
	hint->setAlignment(Qt::AlignCenter);
	QFont hintFont = hint->font();
	hintFont.setPointSize(hintFont.pointSize() - 1);
	hint->setFont(hintFont);
	hint->setStyleSheet("color: gray;");
	mainLayout->addWidget(hint);

	setCentralWidget(central);

	// Connections for preset tab
	connect(saveBtn, &QPushButton::clicked, this, &MainWindow::saveCurrentAsPreset);
	connect(loadBtn, &QPushButton::clicked, this, &MainWindow::loadSelectedPreset);
	connect(delBtn, &QPushButton::clicked, this, &MainWindow::deleteSelectedPreset);
	connect(defBtn, &QPushButton::clicked, this, &MainWindow::setSelectedAsDefault);
	connect(_mPresetList, &QListWidget::itemDoubleClicked, this, &MainWindow::onPresetListDoubleClicked);

	connect(_mBreakEnabled, &QCheckBox::toggled, this, &MainWindow::onBreakSettingsChanged);
	connect(_mWorkMinutes, &QSpinBox::valueChanged, this, &MainWindow::onBreakSettingsChanged);
	connect(_mBreakMinutes, &QSpinBox::valueChanged, this, &MainWindow::onBreakSettingsChanged);
	connect(_mBreakMessage, &QLineEdit::editingFinished, this, &MainWindow::onBreakSettingsChanged);
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
	QPixmap pix(22, 22);
	pix.fill(Qt::transparent);
	{
		QPainter p(&pix);
		p.setRenderHint(QPainter::Antialiasing);
		p.setBrush(QColor(255, 180, 30));
		p.setPen(Qt::NoPen);
		p.drawEllipse(3, 3, 16, 16);
	}

	_mTrayIcon = new QSystemTrayIcon(QIcon(pix), this);
	_mTrayIcon->setToolTip("Protect Brother's Health");
	setWindowIcon(QIcon(pix));

	auto* trayMenu = new QMenu(this);
	trayMenu->addAction("白天模式 (6500K)", this, &MainWindow::setDayMode);
	trayMenu->addAction("护眼模式 (3500K)", this, &MainWindow::setNightMode);
	trayMenu->addSeparator();
	trayMenu->addAction("显示/隐藏", this, [this] { setVisible(!isVisible()); });
	trayMenu->addSeparator();
	trayMenu->addAction("退出", qApp, &QApplication::quit);

	_mTrayIcon->setContextMenu(trayMenu);
	connect(_mTrayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
	_mTrayIcon->show();

	if(!QSystemTrayIcon::isSystemTrayAvailable()) {
		statusBar()->showMessage(QStringLiteral("当前桌面未提供系统托盘服务；关闭窗口会退出程序。"));
	}
}

void MainWindow::setupBreakScheduler() {
	_mTimer			= new BreakTimer(_mWorkMinutes->value(), _mBreakMinutes->value(), this);
	_mBreakReminder = new BreakReminder(this);

	connect(_mTimer, &BreakTimer::workPeriodFinished, this, &MainWindow::onWorkPeriodFinished);
	connect(_mTimer, &BreakTimer::breakPeriodFinished, this, &MainWindow::onBreakPeriodFinished);
	connect(_mTimer, &BreakTimer::remainingTimeChanged, this, &MainWindow::updateCountdown);

	if(_mBreakEnabled->isChecked())
		_mTimer->startWork();
	else
		_mCountdownLabel->setText(QStringLiteral("休息提醒已暂停"));
}

// ============================================================
//  Window Events
// ============================================================

void MainWindow::closeEvent(QCloseEvent* event) {
	if(_mTrayIcon && _mTrayIcon->isVisible() && QSystemTrayIcon::isSystemTrayAvailable()) {
		hide();
		event->ignore();
	} else {
		event->accept();
	}
}

void MainWindow::onBreakSettingsChanged() {
	if(_mUpdating)
		return;

	if(_mTimer) {
		_mTimer->setDurations(_mWorkMinutes->value(), _mBreakMinutes->value());
		if(_mBreakEnabled->isChecked()) {
			_mBreakReminder->closeBreak();
			_mTimer->startWork();
		} else {
			_mTimer->stop();
			_mBreakReminder->closeBreak();
			_mCountdownLabel->setText(QStringLiteral("休息提醒已暂停"));
		}
	}
	saveSettings();
}

void MainWindow::onWorkPeriodFinished(int breakMinutes) {
	Q_UNUSED(breakMinutes);
	if(!_mBreakEnabled->isChecked())
		return;

	_mBreakReminder->showBreakWithMessage(_mBreakMinutes->value(), _mBreakMessage->text());
	_mTimer->startBreak();
}

void MainWindow::onBreakPeriodFinished() {
	_mBreakReminder->closeBreak();
	if(_mBreakEnabled->isChecked())
		_mTimer->startWork();
}

void MainWindow::updateCountdown(const QTime& remaining, bool isWorking) {
	const auto phase = isWorking ? QStringLiteral("距离休息还有") : QStringLiteral("休息结束还剩");
	_mCountdownLabel->setText(QStringLiteral("%1 %2").arg(phase, remaining.toString("hh:mm:ss")));
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
//  Mode Presets (tray)
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
//  Signal Handlers
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

// ============================================================
//  Apply
// ============================================================

void MainWindow::applyMonitor(int idx) {
	auto&	  mw		  = _mMonitors[idx];
	double	  b			  = mw._brightnessSlider->value() / 100.0;
	const int temperature = mw._nightMode->isChecked() ? mw._warmthSlider->value() : 6500;
	QString	  error;
	if(!MonitorController::apply(mw._outputName, b, temperature, &error))
		statusBar()->showMessage(QStringLiteral("显示设置未应用：%1").arg(error), 5000);
}

// ============================================================
//  Settings Persistence
// ============================================================

void MainWindow::loadSettings() {
	_mSettings.load();

	// Suppress signal-triggered save_settings() during initialization
	_mUpdating = true;

	_mSyncBrightness->setChecked(_mSettings.syncBrightness());
	_mSyncNight->setChecked(_mSettings.syncNight());
	_mBreakEnabled->setChecked(_mSettings.breakEnabled());
	_mWorkMinutes->setValue(_mSettings.workMinutes());
	_mBreakMinutes->setValue(_mSettings.breakMinutes());
	_mBreakMessage->setText(_mSettings.breakMessage());

	auto monitors = MonitorController::discover();
	for(int i = 0; i < _mMonitors.size(); ++i) {
		auto&		mw	 = _mMonitors[i];
		const auto& name = mw._outputName;

		// Check if this monitor has saved settings
		bool found		 = false;
		for(const auto& ms : _mSettings.monitors()) {
			if(ms._name == name) {
				found = true;
				mw._brightnessSlider->setValue(ms._brightness);
				mw._nightMode->setChecked(ms._nightMode);
				mw._warmthContainer->setVisible(ms._nightMode);
				if(ms._nightMode) {
					mw._warmthSlider->setValue(ms._warmth);
				}
				break;
			}
		}

		if(!found) {
			// Use current xrandr state
			auto mi = std::find_if(
				monitors.begin(), monitors.end(), [&](const MonitorInfo& m) { return m._name == name; });
			if(mi != monitors.end()) {
				int br = static_cast<int>(mi->_brightness * 100.0);
				br	   = std::clamp(br, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
				mw._brightnessSlider->setValue(br);

				uint16_t k = estimateKelvin(mi->_gammaR, mi->_gammaG, mi->_gammaB);
				if(k != 6500) {
					mw._nightMode->setChecked(true);
					mw._warmthContainer->setVisible(true);
					mw._warmthSlider->setValue(static_cast<int>(k));
				}
			}
		}
	}

	// Window geometry
	auto geo = _mSettings.windowGeometry();
	if(!geo.isEmpty())
		restoreGeometry(geo);

	QString def			   = _mSettings.defaultPreset();
	bool	defaultApplied = false;
	if(!def.isEmpty()) {
		auto preset = _mPresets.load(def);
		if(!preset.isEmpty()) {
			_mUpdating = false;	 // apply_preset sets its own guard
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

	// Persist UI state once after full initialization
	saveSettings();
}

void MainWindow::saveSettings() {
	// Sync current UI state into SettingsManager
	_mSettings.setSyncBrightness(_mSyncBrightness->isChecked());
	_mSettings.setSyncNight(_mSyncNight->isChecked());
	_mSettings.setBreakEnabled(_mBreakEnabled->isChecked());
	_mSettings.setWorkMinutes(_mWorkMinutes->value());
	_mSettings.setBreakMinutes(_mBreakMinutes->value());
	_mSettings.setBreakMessage(_mBreakMessage->text().trimmed());

	auto& monitors = _mSettings.monitors();
	monitors.clear();
	for(const auto& mw : _mMonitors) {
		MonitorSettings ms;
		ms._name	   = mw._outputName;
		ms._brightness = mw._brightnessSlider->value();
		ms._nightMode  = mw._nightMode->isChecked();
		if(ms._nightMode)
			ms._warmth = mw._warmthSlider->value();
		monitors.append(ms);
	}

	_mSettings.setWindowGeometry(saveGeometry());
	_mSettings.save();
}

// ============================================================
//  Preset Management
// ============================================================

void MainWindow::refreshPresetList() {
	_mPresetList->clear();
	QString def	  = _mSettings.defaultPreset();
	auto	names = _mPresets.listPresets();
	for(const auto& name : names) {
		auto  display = (name == def) ? QString("%1  (默认)").arg(name) : name;
		auto* item	  = new QListWidgetItem(display);
		if(name == def) {
			QFont f = item->font();
			f.setBold(true);
			item->setFont(f);
		}
		item->setData(Qt::UserRole, name);	// store real name for operations
		_mPresetList->addItem(item);
	}
}

void MainWindow::saveCurrentAsPreset() {
	QString name = _mPresetNameEdit->text().trimmed();
	if(name.isEmpty()) {
		QMessageBox::information(this, "提示", "请输入预设名称");
		return;
	}

	// Build JSON from current UI state
	QJsonObject preset;
	preset["name"]			  = name;
	preset["sync_brightness"] = _mSyncBrightness->isChecked();
	preset["sync_night"]	  = _mSyncNight->isChecked();

	QJsonObject breakSettings;
	breakSettings["enabled"]	   = _mBreakEnabled->isChecked();
	breakSettings["work_minutes"]  = _mWorkMinutes->value();
	breakSettings["break_minutes"] = _mBreakMinutes->value();
	breakSettings["message"]	   = _mBreakMessage->text().trimmed();
	preset["break"]				   = breakSettings;

	QJsonArray arr;
	for(const auto& mw : _mMonitors) {
		QJsonObject obj;
		obj["name"]		  = mw._outputName;
		obj["brightness"] = mw._brightnessSlider->value();
		obj["night_mode"] = mw._nightMode->isChecked();
		if(mw._nightMode->isChecked())
			obj["warmth"] = mw._warmthSlider->value();
		arr.append(obj);
	}
	preset["monitors"] = arr;

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
	auto items = _mPresetList->selectedItems();
	if(items.isEmpty())
		return;

	QString name   = items[0]->data(Qt::UserRole).toString();
	auto	preset = _mPresets.load(name);
	if(preset.isEmpty())
		return;

	applyPreset(preset);
	saveSettings();
}

void MainWindow::onPresetListDoubleClicked(QListWidgetItem* item) {
	QString name   = item->data(Qt::UserRole).toString();
	auto	preset = _mPresets.load(name);
	if(preset.isEmpty())
		return;

	applyPreset(preset);
	saveSettings();
}

void MainWindow::deleteSelectedPreset() {
	auto items = _mPresetList->selectedItems();
	if(items.isEmpty())
		return;

	QString name  = items[0]->data(Qt::UserRole).toString();
	auto	reply = QMessageBox::question(this,
										  "确认删除",
										  QString("确定要删除预设 \"%1\" 吗？").arg(name),
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
	auto items = _mPresetList->selectedItems();
	if(items.isEmpty())
		return;

	QString name = items[0]->data(Qt::UserRole).toString();
	_mSettings.setDefaultPreset(name);
	refreshPresetList();
	saveSettings();

	auto status = statusBar();
	if(status)
		status->showMessage(QString("已将 \"%1\" 设为默认预设").arg(name), 3000);
}

void MainWindow::applyPreset(const QJsonObject& preset) {
	_mUpdating = true;

	_mSyncBrightness->setChecked(preset["sync_brightness"].toBool(false));
	_mSyncNight->setChecked(preset["sync_night"].toBool(false));

	setBreakControls(preset.value("break").toObject());

	auto arr = preset["monitors"].toArray();
	for(const auto& val : arr) {
		auto obj	 = val.toObject();
		auto monName = obj["name"].toString();
		for(int i = 0; i < _mMonitors.size(); ++i) {
			if(_mMonitors[i]._outputName != monName)
				continue;

			auto& mw = _mMonitors[i];
			mw._brightnessSlider->setValue(obj["brightness"].toInt(100));

			bool night = obj["night_mode"].toBool(false);
			mw._nightMode->setChecked(night);
			mw._warmthContainer->setVisible(night);

			if(night) {
				mw._warmthSlider->setValue(obj["warmth"].toInt(5000));
			}
			break;
		}
	}

	_mUpdating = false;

	for(int i = 0; i < _mMonitors.size(); ++i)
		applyMonitor(i);
	onBreakSettingsChanged();
}

void MainWindow::setBreakControls(const QJsonObject& settings) {
	if(settings.contains("enabled"))
		_mBreakEnabled->setChecked(settings.value("enabled").toBool(true));
	if(settings.contains("work_minutes"))
		_mWorkMinutes->setValue(settings.value("work_minutes").toInt(_mWorkMinutes->value()));
	if(settings.contains("break_minutes"))
		_mBreakMinutes->setValue(settings.value("break_minutes").toInt(_mBreakMinutes->value()));
	if(settings.contains("message"))
		_mBreakMessage->setText(settings.value("message").toString());
}
