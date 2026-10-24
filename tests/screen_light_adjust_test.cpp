#include "break_reminder.h"
#include "break_timer.h"
#include "preset_manager.h"
#include "settings_manager.h"

#include <QApplication>
#include <QJsonObject>
#include <QScreen>
#include <QtTest>

class ScreenLightAdjustTest : public QObject {
	Q_OBJECT

  private slots:
	void reminderCreatesOneWindowPerScreen();
	void reminderUsesTheSameMessageOnEveryScreen();
	void reminderWindowsCoverTheirTargetScreens();
	void timerTracksWorkAndBreakPhases();
	void settingsPersistBreakOptionsAndDefaultPreset();
	void presetsStoreDisplayAndBreakSettingsTogether();
	void presetsRejectPathTraversalNames();
};

void ScreenLightAdjustTest::reminderCreatesOneWindowPerScreen() {
	const auto screens = QGuiApplication::screens();
	QVERIFY(!screens.isEmpty());

	BreakReminder reminder;
	reminder.showBreakOnScreens(20, QStringLiteral("休息一下"), screens);

	QCOMPARE(reminder.visibleWindowCount(), screens.size());
	reminder.closeBreak();
	QCOMPARE(reminder.visibleWindowCount(), 0);
}

void ScreenLightAdjustTest::reminderUsesTheSameMessageOnEveryScreen() {
	const auto screens = QGuiApplication::screens();
	QVERIFY(!screens.isEmpty());

	const QString message = QStringLiteral("请休息并眺望远方");
	BreakReminder reminder;
	reminder.showBreakOnScreens(15, message, screens);

	QCOMPARE(reminder.visibleMessages(), QStringList(screens.size(), message));
	reminder.closeBreak();
}

void ScreenLightAdjustTest::reminderWindowsCoverTheirTargetScreens() {
	const auto screens = QGuiApplication::screens();
	QVERIFY(!screens.isEmpty());

	BreakReminder reminder;
	reminder.showBreakOnScreens(1, QStringLiteral("请休息一下"), screens);

	QCOMPARE(reminder.visibleWindowCount(), screens.size());
	for(const auto* screen : screens) {
		bool foundWindowCoveringScreen = false;
		for(auto* widget : QApplication::topLevelWidgets()) {
			if(widget->windowTitle() == QStringLiteral("休息时间") && widget->isVisible()
			   && widget->geometry() == screen->geometry()) {
				foundWindowCoveringScreen = true;
				break;
			}
		}
		QVERIFY2(foundWindowCoveringScreen,
				 qPrintable(QStringLiteral("No break window covers screen %1 at %2")
								.arg(screen->name(),
									 QStringLiteral("(%1,%2 %3x%4)")
										 .arg(screen->geometry().x())
										 .arg(screen->geometry().y())
										 .arg(screen->geometry().width())
										 .arg(screen->geometry().height()))));
	}

	reminder.closeBreak();
}

void ScreenLightAdjustTest::timerTracksWorkAndBreakPhases() {
	BreakTimer timer(50, 10);
	QVERIFY(timer.isWorking());

	timer.startWork();
	QVERIFY(timer.remainingTime() > QTime(0, 0, 0));
	timer.startBreak();
	QVERIFY(!timer.isWorking());
	QVERIFY(timer.remainingTime() > QTime(0, 0, 0));

	timer.stop();
	QCOMPARE(timer.remainingTime(), QTime(0, 0, 0));
}

void ScreenLightAdjustTest::settingsPersistBreakOptionsAndDefaultPreset() {
	SettingsManager settings;
	settings.setDefaultPreset(QStringLiteral("晚间"));
	settings.setBreakEnabled(true);
	settings.setWorkMinutes(50);
	settings.setBreakMinutes(10);
	settings.setBreakMessage(QStringLiteral("抬头看看远处"));
	settings.save();

	SettingsManager restored;
	restored.load();
	QCOMPARE(restored.defaultPreset(), QStringLiteral("晚间"));
	QVERIFY(restored.breakEnabled());
	QCOMPARE(restored.workMinutes(), 50);
	QCOMPARE(restored.breakMinutes(), 10);
	QCOMPARE(restored.breakMessage(), QStringLiteral("抬头看看远处"));
}

void ScreenLightAdjustTest::presetsStoreDisplayAndBreakSettingsTogether() {
	PresetManager	  presets;
	const QJsonObject preset{
		{QStringLiteral("sync_brightness"), true									},
		{QStringLiteral("monitors"),
		 QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("DP-1")},
								{QStringLiteral("brightness"), 90}}}				},
		{QStringLiteral("break"),
		 QJsonObject{{QStringLiteral("enabled"), true},
					 {QStringLiteral("work_minutes"), 50},
					 {QStringLiteral("break_minutes"), 10},
					 {QStringLiteral("message"), QStringLiteral("休息一下")}}}
	  };

	QVERIFY(presets.save(QStringLiteral("unified"), preset));
	QCOMPARE(presets.load(QStringLiteral("unified")), preset);
	QVERIFY(presets.remove(QStringLiteral("unified")));
}

void ScreenLightAdjustTest::presetsRejectPathTraversalNames() {
	PresetManager presets;
	QVERIFY(!presets.save(QStringLiteral("../outside"), {}));
	QVERIFY(presets.load(QStringLiteral("../outside")).isEmpty());
	QVERIFY(!presets.remove(QStringLiteral("../outside")));
}

int main(int argc, char* argv[]) {
	QTemporaryDir configHome;
	if(!configHome.isValid())
		return 2;
	qputenv("XDG_CONFIG_HOME", configHome.path().toLocal8Bit());

	QApplication		  app(argc, argv);
	ScreenLightAdjustTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "screen_light_adjust_test.moc"
