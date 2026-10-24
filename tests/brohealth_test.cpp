#include "break_reminder.h"
#include "debt_ledger.h"
#include "phrase_catalog.h"
#include "preset_manager.h"
#include "reminder_manager.h"
#include "settings_manager.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTime>
#include <QTimer>
#include <QtTest>

#include <algorithm>

namespace {
QString sourceFile(const QString& name) { return QStringLiteral(BROHEALTH_SOURCE_DIR "/") + name; }

QString writeFile(const QString& directory, const QString& name, const QByteArray& contents) {
	const auto path = QDir(directory).filePath(name);
	QFile	   file(path);
	if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return {};
	file.write(contents);
	file.close();
	return path;
}

QString readFile(const QString& path) {
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
		return {};
	return QString::fromUtf8(file.readAll());
}

void resetBrohealthConfig() {
	QDir(SettingsManager::configDir()).removeRecursively();
	QDir().mkpath(SettingsManager::configDir());
	QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart"))
		.removeRecursively();
	QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
		 + QStringLiteral("/screen-light-adjust"))
		.removeRecursively();
}

QVector<ReminderConfig> defaultTasks();

QVector<ReminderConfig> waterOnlyTasks() {
	auto tasks = defaultTasks();
	for(auto& task : tasks) {
		if(task._id == QStringLiteral("eye_break"))
			task._enabled = false;
	}
	return tasks;
}

QVector<ReminderConfig> defaultTasks() {
	auto tasks = SettingsManager::defaultReminders();
	for(auto& task : tasks) {
		if(task._id == QStringLiteral("water")) {
			task._enabled		  = true;
			task._intervalMinutes = 50;
			task._unitsPerEvent	  = 250;
			task._weight		  = 1;
		} else if(task._id == QStringLiteral("eye_break")) {
			task._enabled		  = true;
			task._intervalMinutes = 120;
			task._breakMinutes	  = 20;
			task._unitsPerEvent	  = 20;
			task._weight		  = 2;
		}
	}
	return tasks;
}

QByteArray validPhraseFile(const QString& noDebt = QStringLiteral("没有欠账，兄弟很满意。")) {
	QString text;
	text += QStringLiteral("[no_debt]\n%1\n").arg(noDebt);
	text += QStringLiteral("[mild]\n轻微欠账。\n");
	text += QStringLiteral("[moderate]\n中等欠账。\n");
	text += QStringLiteral("[severe]\n严重欠账。\n");
	text += QStringLiteral("[critical]\n危急欠账。\n");
	text += QStringLiteral("[actions]\n真的？\n");
	text += QStringLiteral("[tasks.water.due]\n喝水时间到。\n");
	text += QStringLiteral("[tasks.water.skipped]\n这杯记在账上。\n");
	text += QStringLiteral("[tasks.water.completed]\n喝得好。\n");
	text += QStringLiteral("[tasks.eye_break.due]\n该休息了。\n");
	text += QStringLiteral("[tasks.eye_break.skipped]\n休息记一笔。\n");
	text += QStringLiteral("[tasks.eye_break.completed]\n眼睛谢谢你。\n");
	return text.toUtf8();
}
}  // namespace

class ProtectBrothersHealthTest : public QObject {
	Q_OBJECT

  private slots:
	void shippedPhraseCatalogMeetsContentContract();
	void phraseCatalogParsesTasksAndRejectsMalformedFiles();
	void phraseCatalogFallsBackAndRecreatesUserCopy();
	void phraseCatalogAutoReloadsChangedFile();
	void debtLedgerAppendsRepaymentEventsAndKeepsHistory();
	void debtLedgerMigratesV1AndBacksUpCorruption();
	void reminderManagerTracksWaterLifecycle();
	void reminderManagerSnoozesAndPersistsPending();
	void reminderManagerIsIdempotentAcrossCrashWindow();
	void reminderManagerSuppressesDuringPause();
	void reminderManagerBacksUpCorruptPending();
	void reminderManagerRepaysRemovedTask();
	void reminderNotificationCopyRespondsToLevel();
	void reminderManagerRunsBreakCycleAndBuildsReport();
	void settingsMigrateV2AndPersistReminders();
	void presetsStoreReminderConfiguration();
	void migratesLegacyConfigurationDirectory();
	void reminderCreatesOneWindowPerScreen();
	void reminderUsesTheSameMessageOnEveryScreen();
	void breakOverlayOffersVisibleSkipControl();
	void escapeConfirmationDefaultsToContinue();
	void mouseClickConfirmsBreakSkip();
	void closingBreakCancelsSkipConfirmation();
};

void ProtectBrothersHealthTest::shippedPhraseCatalogMeetsContentContract() {
	PhraseCatalog catalog;
	QString		  error;
	QVERIFY2(catalog.load(sourceFile(QStringLiteral("resources/phrases.conf")), &error), qPrintable(error));

	QCOMPARE(catalog.phraseCount(QStringLiteral("no_debt")), 100);
	QCOMPARE(catalog.phraseCount(QStringLiteral("mild")), 100);
	QCOMPARE(catalog.phraseCount(QStringLiteral("moderate")), 100);
	QCOMPARE(catalog.phraseCount(QStringLiteral("severe")), 100);
	QCOMPARE(catalog.phraseCount(QStringLiteral("critical")), 100);
	for(const auto& task : {QStringLiteral("water"), QStringLiteral("eye_break")}) {
		for(const auto& event :
			{QStringLiteral("due"), QStringLiteral("skipped"), QStringLiteral("completed")})
			QCOMPARE(catalog.taskPhraseCount(task, event), 10);
	}
	QCOMPARE(catalog.totalPhraseCount(), 600);

	const auto contents = readFile(sourceFile(QStringLiteral("resources/phrases.conf")));
	for(const auto& required : {QStringLiteral("你欠兄弟的太多了。"),
								QStringLiteral("你欠兄弟的，拿什么还？"),
								QStringLiteral("兄弟没有欠你，你为什么欠兄弟？"),
								QStringLiteral("欠的不多，兄弟打个哈哈也就过去了。"),
								QStringLiteral("兄弟只盼着你好。"),
								QStringLiteral("How dare YOU?")})
		QVERIFY2(contents.contains(required), qPrintable(QStringLiteral("缺少指定文案：%1").arg(required)));
	QVERIFY(contents.contains(QStringLiteral("吾尝闻之")));
	QVERIFY(!contents.contains(QChar(0x201C)) && !contents.contains(QChar(0x201D)));

	const auto longChineseLine =
		QStringLiteral("吾尝闻之，人际之间最惧名分混淆：受人恩惠，当偿其债；负于兄弟，须赎其颜，债可渐偿，颜"
					   "面一迟，则易薄，是以吾宁先急兄弟之谊，不敢使此情分于吾之手失其尊严。");
	int overlong = 0;
	for(const auto& rawLine : contents.split(QLatin1Char('\n'))) {
		const auto line = rawLine.trimmed();
		if(line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('[')))
			continue;
		const bool hasCjk = std::any_of(line.cbegin(), line.cend(), [](QChar character) {
			return character.unicode() >= 0x4E00 && character.unicode() <= 0x9FFF;
		});
		if(hasCjk && line.size() > 60 && line != longChineseLine)
			++overlong;
	}
	QCOMPARE(overlong, 0);

	QString report;
	QVERIFY(
		PhraseCatalog::validateFile(sourceFile(QStringLiteral("resources/phrases.conf")), &report, &error));
	QVERIFY2(report.contains(QStringLiteral("重复行：0")), qPrintable(report));
	QVERIFY2(report.contains(QStringLiteral("中文弯引号行：0")), qPrintable(report));
	QVERIFY2(report.contains(QStringLiteral("actions：40 条")), qPrintable(report));
	QVERIFY2(report.contains(QStringLiteral("合计：600 条")), qPrintable(report));
}

void ProtectBrothersHealthTest::phraseCatalogParsesTasksAndRejectsMalformedFiles() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto valid = writeFile(directory.path(), QStringLiteral("valid.conf"), validPhraseFile());
	QVERIFY(!valid.isEmpty());

	PhraseCatalog catalog;
	QString		  error;
	QVERIFY2(catalog.load(valid, &error), qPrintable(error));
	QVERIFY(catalog.isValid());
	QCOMPARE(catalog.phraseForScore(0), QStringLiteral("没有欠账，兄弟很满意。"));
	QCOMPARE(catalog.phraseForTask(QStringLiteral("water"), QStringLiteral("due"), 0),
			 QStringLiteral("喝水时间到。"));
	QCOMPARE(catalog.phraseForTask(QStringLiteral("water"), QStringLiteral("snoozed"), 0),
			 QStringLiteral("没有欠账，兄弟很满意。"));
	QCOMPARE(catalog.actionPrompt(), QStringLiteral("真的？"));

	const auto malformed = writeFile(directory.path(),
									 QStringLiteral("malformed.conf"),
									 QByteArray("[no_debt]\n好的\n[mild]\n轻微\n[actions]\n好\n"));
	QVERIFY(!catalog.load(malformed, &error));
	QVERIFY(!catalog.isValid());
	QVERIFY(!catalog.phraseForScore(0).isEmpty());
	QVERIFY(catalog.phraseForScore(0) != PhraseCatalog::missingConfigMessage());

	const auto badSection = writeFile(
		directory.path(), QStringLiteral("bad-section.conf"), QByteArray("[no_debt]\n好的\n[wat]\n坏的\n"));
	PhraseCatalog invalid;
	QString		  invalidError;
	int			  line = 0;
	QVERIFY(!invalid.load(badSection, &invalidError, &line));
	QVERIFY(line > 0);
	QVERIFY(!invalidError.isEmpty());

	const auto	  missingBracket = writeFile(directory.path(),
											 QStringLiteral("missing-bracket.conf"),
											 QByteArray("[no_debt]\n好的\n[mild\n轻微\n"));
	PhraseCatalog brokenSection;
	QVERIFY(!brokenSection.load(missingBracket, &invalidError, &line));
	QVERIFY(line > 0);
	QVERIFY(invalidError.contains(QStringLiteral("]")));
}

void ProtectBrothersHealthTest::phraseCatalogFallsBackAndRecreatesUserCopy() {
	resetBrohealthConfig();

	PhraseCatalog catalog;
	QString		  error;
	QVERIFY2(catalog.loadActive(&error), qPrintable(error));
	const auto userPath = PhraseCatalog::userConfigPath();
	QVERIFY(QFile::exists(userPath));
	QCOMPARE(catalog.configPath(), userPath);
	QCOMPARE(catalog.sourcePath(), userPath);
	QVERIFY(!catalog.usedFallback());

	const auto invalid = QByteArray("this is not a phrase file\n");
	QVERIFY(QFile::resize(userPath, 0));
	QFile file(userPath);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(invalid);
	file.close();

	// A reload failure must keep the last good user catalog instead of silently
	// switching to the installed template.
	QVERIFY(catalog.loadActive(&error));
	QVERIFY(catalog.isValid());
	QVERIFY(!catalog.usedFallback());
	QCOMPARE(catalog.sourcePath(), userPath);
	QVERIFY(!catalog.errorString().isEmpty());
	QVERIFY(catalog.phraseForScore(0) != PhraseCatalog::missingConfigMessage());

	PhraseCatalog fallback;
	QVERIFY(fallback.loadActive(&error));
	QVERIFY(fallback.isValid());
	QVERIFY(fallback.usedFallback());
	QVERIFY(!fallback.errorString().isEmpty());
	QVERIFY(fallback.phraseForScore(0) != PhraseCatalog::missingConfigMessage());
	QVERIFY(fallback.sourcePath() != userPath);

	QVERIFY(QFile::remove(userPath));
	PhraseCatalog recreated;
	QVERIFY(recreated.loadActive(&error));
	QVERIFY(QFile::exists(userPath));
}

void ProtectBrothersHealthTest::phraseCatalogAutoReloadsChangedFile() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto path = writeFile(directory.path(), QStringLiteral("phrases.conf"), validPhraseFile());
	QVERIFY(!path.isEmpty());

	qputenv("BROHEALTH_PHRASES_CONF", path.toLocal8Bit());
	PhraseCatalog catalog;
	catalog.setAutoReloadEnabled(true);
	QString error;
	QVERIFY2(catalog.loadActive(&error), qPrintable(error));
	QSignalSpy changed(&catalog, &PhraseCatalog::changed);
	QVERIFY(changed.isValid());

	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	file.write(validPhraseFile(QStringLiteral("新文案，兄弟换了件衣服。")));
	file.close();

	QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 5000);
	QCOMPARE(catalog.phraseForScore(0), QStringLiteral("新文案，兄弟换了件衣服。"));
	qunsetenv("BROHEALTH_PHRASES_CONF");
}

void ProtectBrothersHealthTest::debtLedgerAppendsRepaymentEventsAndKeepsHistory() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto path = directory.filePath(QStringLiteral("ledger.json"));

	DebtLedger ledger(path);
	QVERIFY(ledger.load()._ok);
	QVERIFY(ledger.recordDebt(QStringLiteral("water"), DebtLedger::EventKind::Skipped, 250, 1));
	QVERIFY(ledger.recordDebt(QStringLiteral("water"), DebtLedger::EventKind::Missed, 300, 1));
	QVERIFY(ledger.recordDebt(QStringLiteral("eye_break"), DebtLedger::EventKind::Skipped, 20, 2));

	QCOMPARE(ledger.outstandingCount(QStringLiteral("water")), 2);
	QCOMPARE(ledger.outstandingUnits(QStringLiteral("water")), qint64(550));
	QCOMPARE(ledger.score(), 4);
	QVERIFY(ledger.outstandingTaskIds().contains(QStringLiteral("water")));
	QVERIFY(ledger.outstandingTaskIds().contains(QStringLiteral("eye_break")));

	QVERIFY(ledger.repayOne(QStringLiteral("water")));
	QCOMPARE(ledger.outstandingCount(QStringLiteral("water")), 1);
	QCOMPARE(ledger.outstandingUnits(QStringLiteral("water")), qint64(300));
	QCOMPARE(ledger.lifetimeDebtCount(QStringLiteral("water")), 2);
	QCOMPARE(ledger.lifetimeDebtUnits(QStringLiteral("water")), qint64(550));
	QCOMPARE(ledger.repaymentCount(QStringLiteral("water")), 1);
	QCOMPARE(ledger.score(), 3);
	QCOMPARE(ledger.events().last()._kind, DebtLedger::EventKind::Repayment);
	QVERIFY(!ledger.events().last()._repaidEventId.isEmpty());

	DebtLedger restored(path);
	QVERIFY(restored.load()._ok);
	QCOMPARE(restored.outstandingCount(QStringLiteral("water")), 1);
	QVERIFY(restored.repayOne(QStringLiteral("eye_break")));
	QCOMPARE(restored.score(), 1);
	QVERIFY(!restored.repayOne(QStringLiteral("eye_break")));
	QVERIFY(restored.outstandingTaskIds().contains(QStringLiteral("water")));
	QVERIFY(!restored.outstandingTaskIds().contains(QStringLiteral("eye_break")));
}

void ProtectBrothersHealthTest::debtLedgerMigratesV1AndBacksUpCorruption() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto path = directory.filePath(QStringLiteral("v1.json"));
	const auto v1 =
		QJsonDocument(QJsonObject{
						  {QStringLiteral("version"), 1													   },
						  {QStringLiteral("events"),
						   QJsonArray{QJsonObject{
							   {QStringLiteral("task"), QStringLiteral("break")},
							   {QStringLiteral("units"), 20},
							   {QStringLiteral("created_at"), QStringLiteral("2026-01-01T00:00:00.000Z")}}}}
	})
			.toJson();
	QVERIFY(!writeFile(directory.path(), QStringLiteral("v1.json"), v1).isEmpty());

	DebtLedger migrated(path);
	const auto result = migrated.load();
	QVERIFY(result._ok);
	QCOMPARE(migrated.outstandingCount(QStringLiteral("eye_break")), 1);
	QCOMPARE(migrated.score(), 2);

	const auto corruptPath = directory.filePath(QStringLiteral("corrupt.json"));
	const auto corrupt	   = QByteArray("{\"version\":1,\"events\":[");
	QVERIFY(!writeFile(directory.path(), QStringLiteral("corrupt.json"), corrupt).isEmpty());
	DebtLedger broken(corruptPath);
	const auto brokenResult = broken.load();
	QVERIFY(brokenResult._ok);
	QVERIFY(brokenResult._recovered);
	QVERIFY(!brokenResult._backupPath.isEmpty());
	QVERIFY(QFile::exists(brokenResult._backupPath));
	QCOMPARE(readFile(brokenResult._backupPath), QString::fromUtf8(corrupt));
	QVERIFY(broken.recordDebt(QStringLiteral("water"), DebtLedger::EventKind::Skipped, 250, 1));
	const auto saved = QJsonDocument::fromJson(readFile(corruptPath).toUtf8());
	QCOMPARE(saved.object().value(QStringLiteral("version")).toInt(), 2);

	const auto highPath = directory.filePath(QStringLiteral("high-version.json"));
	const auto high		= QByteArray("{\"version\":3,\"events\":[{\"id\":\"x\",\"task\":\"water\","
									 "\"kind\":\"skipped\",\"units\":250,\"weight\":1,"
									 "\"created_at\":\"2026-01-01T00:00:00.000Z\"}]}");
	QVERIFY(!writeFile(directory.path(), QStringLiteral("high-version.json"), high).isEmpty());
	DebtLedger newer(highPath);
	const auto newerResult = newer.load();
	QVERIFY(!newerResult._ok);
	QVERIFY(!newerResult._recovered);
	QVERIFY(!newer.isWritable());
	QVERIFY(!newer.recordDebt(QStringLiteral("water"), DebtLedger::EventKind::Skipped, 250, 1));
	QCOMPARE(readFile(highPath), QString::fromUtf8(high));
}

void ProtectBrothersHealthTest::reminderManagerTracksWaterLifecycle() {
	resetBrohealthConfig();
	const auto t0	 = QDateTime(QDate(2026, 1, 1), QTime(9, 0));
	auto	   tasks = defaultTasks();
	for(auto& task : tasks) {
		if(task._id == QStringLiteral("eye_break"))
			task._enabled = false;
	}
	ReminderManager manager(nullptr, nullptr, false);
	manager.setNowForTest(t0);
	manager.configure(tasks);
	manager.pollAt(t0);
	QVERIFY(!manager.hasPending(QStringLiteral("water")));

	manager.pollAt(t0.addSecs(50 * 60));
	QVERIFY(manager.hasPending(QStringLiteral("water")));
	QCOMPARE(manager.pendingUnits(QStringLiteral("water")), 250);
	QVERIFY(manager.skipTask(QStringLiteral("water")));
	QVERIFY(!manager.hasPending(QStringLiteral("water")));
	QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 1);
	QCOMPARE(manager.outstandingUnits(QStringLiteral("water")), qint64(250));

	const auto nextDue = manager.nextDue(QStringLiteral("water"));
	manager.pollAt(nextDue);
	QVERIFY(manager.hasPending(QStringLiteral("water")));
	QVERIFY(manager.completeTask(QStringLiteral("water")));
	QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 1);
	QCOMPARE(manager.lifetimeDebtCount(QStringLiteral("water")), 1);
	const auto taskList = manager.tasks();
	QVERIFY(std::any_of(taskList.cbegin(), taskList.cend(), [](const ReminderConfig& task) {
		return task._id == QStringLiteral("water");
	}));

	const auto thirdDue = manager.nextDue(QStringLiteral("water"));
	manager.pollAt(thirdDue);
	QVERIFY(manager.hasPending(QStringLiteral("water")));
	manager.pollAt(thirdDue.addSecs(301));
	QVERIFY(!manager.hasPending(QStringLiteral("water")));
	QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 2);
	QCOMPARE(manager.score(), 2);
}

void ProtectBrothersHealthTest::reminderManagerSnoozesAndPersistsPending() {
	resetBrohealthConfig();
	const auto t0 = QDateTime(QDate(2026, 1, 1), QTime(9, 0));
	{
		ReminderManager manager(nullptr, nullptr, false);
		manager.setNowForTest(t0);
		manager.configure(defaultTasks());
		manager.pollAt(t0.addSecs(50 * 60));
		QVERIFY(manager.hasPending(QStringLiteral("water")));
		QVERIFY(manager.snoozeTask(QStringLiteral("water")));
		QVERIFY(!manager.hasPending(QStringLiteral("water")));
		QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 0);
		QCOMPARE(manager.nextDue(QStringLiteral("water")), t0.addSecs(55 * 60));

		manager.pollAt(t0.addSecs(55 * 60));
		QVERIFY(manager.hasPending(QStringLiteral("water")));
		QVERIFY(manager.snoozeTask(QStringLiteral("water")));
		QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 0);

		manager.pollAt(t0.addSecs(60 * 60));
		QVERIFY(manager.hasPending(QStringLiteral("water")));
		QVERIFY(manager.snoozeTask(QStringLiteral("water")));
		QVERIFY(!manager.hasPending(QStringLiteral("water")));
		QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 1);
	}

	// A new process must restore the pending reminder from pending.json and
	// turn it into a missed reminder after the grace period.
	resetBrohealthConfig();
	const auto t1 = QDateTime(QDate(2026, 1, 1), QTime(9, 0));
	{
		ReminderManager manager(nullptr, nullptr, false);
		manager.setNowForTest(t1);
		manager.configure(defaultTasks());
		manager.pollAt(t1.addSecs(50 * 60));
		QVERIFY(manager.hasPending(QStringLiteral("water")));
	}
	{
		ReminderManager restored(nullptr, nullptr, false);
		restored.setNowForTest(t1.addSecs(51 * 60));
		restored.configure(defaultTasks());
		QVERIFY(restored.hasPending(QStringLiteral("water")));
		QCOMPARE(restored.outstandingCount(QStringLiteral("water")), 0);
		restored.pollAt(t1.addSecs(56 * 60));
		QVERIFY(!restored.hasPending(QStringLiteral("water")));
		QCOMPARE(restored.outstandingCount(QStringLiteral("water")), 1);
	}
}

void ProtectBrothersHealthTest::reminderManagerIsIdempotentAcrossCrashWindow() {
	resetBrohealthConfig();
	const auto t0		   = QDateTime(QDate(2026, 1, 1), QTime(9, 0));
	const auto pendingPath = QDir(SettingsManager::configDir()).filePath(QStringLiteral("pending.json"));

	auto readPending	   = [&]() {
		  QFile file(pendingPath);
		  if(!file.open(QIODevice::ReadOnly))
			  return QByteArray();
		  return file.readAll();
	};

	// Record the skip, then restore the pre-skip pending file as if the process
	// crashed between the ledger commit and the pending cleanup.
	QByteArray stale;
	{
		ReminderManager manager(nullptr, nullptr, false);
		manager.setNowForTest(t0);
		manager.configure(waterOnlyTasks());
		manager.pollAt(t0.addSecs(50 * 60));
		QVERIFY(manager.hasPending(QStringLiteral("water")));
		stale = readPending();
		QVERIFY(!stale.isEmpty());
		QVERIFY(manager.skipTask(QStringLiteral("water")));
		QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 1);
	}
	QVERIFY(!writeFile(SettingsManager::configDir(), QStringLiteral("pending.json"), stale).isEmpty());
	{
		ReminderManager restored(nullptr, nullptr, false);
		restored.setNowForTest(t0.addSecs(51 * 60));
		restored.configure(waterOnlyTasks());
		QCOMPARE(restored.outstandingCount(QStringLiteral("water")), 1);
		QVERIFY(!restored.hasPending(QStringLiteral("water")));
	}

	// Same crash window for a completed reminder: no duplicate ledger event.
	resetBrohealthConfig();
	{
		ReminderManager manager(nullptr, nullptr, false);
		manager.setNowForTest(t0);
		manager.configure(waterOnlyTasks());
		manager.pollAt(t0.addSecs(50 * 60));
		stale = readPending();
		QVERIFY(!stale.isEmpty());
		QVERIFY(manager.completeTask(QStringLiteral("water")));
		QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 0);
	}
	QVERIFY(!writeFile(SettingsManager::configDir(), QStringLiteral("pending.json"), stale).isEmpty());
	{
		ReminderManager restored(nullptr, nullptr, false);
		restored.setNowForTest(t0.addSecs(51 * 60));
		restored.configure(waterOnlyTasks());
		QCOMPARE(restored.outstandingCount(QStringLiteral("water")), 0);
		QVERIFY(!restored.hasPending(QStringLiteral("water")));
	}
}

void ProtectBrothersHealthTest::reminderManagerSuppressesDuringPause() {
	resetBrohealthConfig();
	const auto t0 = QDateTime(QDate(2026, 1, 1), QTime(9, 0));

	// A reminder already pending when the user pauses must not become debt.
	{
		ReminderManager manager(nullptr, nullptr, false);
		manager.setNowForTest(t0);
		manager.configure(waterOnlyTasks());
		manager.pollAt(t0.addSecs(50 * 60));
		QVERIFY(manager.hasPending(QStringLiteral("water")));
		manager.setPausedUntil(t0.addSecs(2 * 60 * 60));
		manager.pollAt(t0.addSecs(50 * 60 + 301));
		QVERIFY(!manager.hasPending(QStringLiteral("water")));
		QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 0);
	}

	// Same rule while restoring a pending reminder from disk during a pause.
	resetBrohealthConfig();
	{
		ReminderManager manager(nullptr, nullptr, false);
		manager.setNowForTest(t0);
		manager.configure(waterOnlyTasks());
		manager.pollAt(t0.addSecs(50 * 60));
		QVERIFY(manager.hasPending(QStringLiteral("water")));
	}
	{
		ReminderManager restored(nullptr, nullptr, false);
		restored.setNowForTest(t0.addSecs(51 * 60));
		restored.setPausedUntil(t0.addSecs(2 * 60 * 60));
		restored.configure(waterOnlyTasks());
		QVERIFY(!restored.hasPending(QStringLiteral("water")));
		QCOMPARE(restored.outstandingCount(QStringLiteral("water")), 0);
	}

	// A reminder due during a pause is rescheduled without debt.
	resetBrohealthConfig();
	ReminderManager manager(nullptr, nullptr, false);
	manager.setNowForTest(t0);
	manager.configure(waterOnlyTasks());
	manager.setPausedUntil(t0.addSecs(2 * 60 * 60));
	manager.pollAt(t0.addSecs(50 * 60));
	QVERIFY(!manager.hasPending(QStringLiteral("water")));
	QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 0);
	manager.pollAt(t0.addSecs(2 * 60 * 60 + 60));
	QVERIFY(manager.hasPending(QStringLiteral("water")));
	QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 0);
	QVERIFY(manager.skipTask(QStringLiteral("water")));
	QCOMPARE(manager.outstandingCount(QStringLiteral("water")), 1);

	// Quiet hours that cross midnight.
	resetBrohealthConfig();
	ReminderManager quiet(nullptr, nullptr, false);
	const auto		evening = QDateTime(QDate(2026, 1, 1), QTime(23, 0));
	quiet.setNowForTest(evening);
	quiet.setQuietHours(true, QStringLiteral("22:00"), QStringLiteral("08:00"));
	quiet.configure(waterOnlyTasks());
	quiet.pollAt(evening.addSecs(50 * 60));
	QVERIFY(!quiet.hasPending(QStringLiteral("water")));
	quiet.pollAt(evening.addSecs(9 * 60 * 60 + 30 * 60));
	QVERIFY(quiet.hasPending(QStringLiteral("water")));
	QCOMPARE(quiet.outstandingCount(QStringLiteral("water")), 0);
	QVERIFY(quiet.skipTask(QStringLiteral("water")));
}

void ProtectBrothersHealthTest::reminderManagerBacksUpCorruptPending() {
	resetBrohealthConfig();
	QVERIFY(!writeFile(SettingsManager::configDir(),
					   QStringLiteral("pending.json"),
					   QByteArray("{ this is not a pending file"))
				 .isEmpty());

	ReminderManager manager(nullptr, nullptr, false);
	manager.setNowForTest(QDateTime(QDate(2026, 1, 1), QTime(9, 0)));
	manager.configure(waterOnlyTasks());
	QVERIFY(!QDir(SettingsManager::configDir())
				 .entryList({QStringLiteral("pending.corrupt-*.json")}, QDir::Files)
				 .isEmpty());
	QVERIFY(!manager.hasPending(QStringLiteral("water")));
}

void ProtectBrothersHealthTest::reminderManagerRepaysRemovedTask() {
	resetBrohealthConfig();
	const auto ledger = QByteArray("{\"version\":2,\"events\":[{\"id\":\"stretch-1\",\"task\":\"stretch\","
								   "\"kind\":\"skipped\",\"units\":10,\"weight\":3,"
								   "\"created_at\":\"2026-01-01T00:00:00.000Z\"}]}");
	QVERIFY(!writeFile(SettingsManager::configDir(), QStringLiteral("debt-ledger.json"), ledger).isEmpty());

	ReminderManager manager(nullptr, nullptr, false);
	manager.setNowForTest(QDateTime(QDate(2026, 1, 1), QTime(9, 0)));
	manager.configure(defaultTasks());
	QVERIFY(manager.outstandingTaskIds().contains(QStringLiteral("stretch")));
	QCOMPARE(manager.score(), 3);
	QVERIFY2(manager.debtReport().contains(QStringLiteral("stretch")), qPrintable(manager.debtReport()));
	QVERIFY(manager.repayOne(QStringLiteral("stretch")));
	QVERIFY(!manager.outstandingTaskIds().contains(QStringLiteral("stretch")));
	QCOMPARE(manager.score(), 0);
}

void ProtectBrothersHealthTest::reminderNotificationCopyRespondsToLevel() {
	resetBrohealthConfig();
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto phrasePath = writeFile(
		directory.path(),
		QStringLiteral("levelalerts.conf"),
		QByteArray("[no_debt]\nLEVEL_NO_DEBT\n[mild]\nLEVEL_MILD\n[moderate]\nLEVEL_MODERATE\n"
				   "[severe]\nLEVEL_SEVERE\n[critical]\nLEVEL_CRITICAL\n[actions]\nLEVEL_ACTION\n"
				   "[tasks.water.due]\nTASK_WATER_DUE\n[tasks.water.skipped]\nTASK_WATER_SKIP\n"
				   "[tasks.water.completed]\nTASK_WATER_DONE\n[tasks.eye_break.due]\nTASK_BREAK_DUE\n"
				   "[tasks.eye_break.skipped]\nTASK_BREAK_SKIP\n"
				   "[tasks.eye_break.completed]\nTASK_BREAK_DONE\n"));
	QVERIFY(!phrasePath.isEmpty());

	struct EnvironmentGuard {
		bool	   had;
		QByteArray previous;

		EnvironmentGuard()
			: had(qEnvironmentVariableIsSet("BROHEALTH_PHRASES_CONF"))
			, previous(qgetenv("BROHEALTH_PHRASES_CONF")) {}

		~EnvironmentGuard() {
			if(had)
				qputenv("BROHEALTH_PHRASES_CONF", previous);
			else
				qunsetenv("BROHEALTH_PHRASES_CONF");
		}
	} guard;

	qputenv("BROHEALTH_PHRASES_CONF", phrasePath.toLocal8Bit());

	{
		ReminderManager manager(nullptr, nullptr, false);
		manager.setNowForTest(QDateTime(QDate(2026, 1, 1), QTime(9, 0)));
		manager.configure(waterOnlyTasks());
		const auto body = manager.notificationPreview(QStringLiteral("water"));
		QVERIFY2(body.contains(QStringLiteral("TASK_WATER_DUE")), qPrintable(body));
		QVERIFY2(body.contains(QStringLiteral("LEVEL_NO_DEBT")), qPrintable(body));
		QVERIFY2(body.indexOf(QStringLiteral("LEVEL_NO_DEBT"))
					 < body.indexOf(QStringLiteral("TASK_WATER_DUE")),
				 qPrintable(body));
	}

	resetBrohealthConfig();
	const auto ledger = QByteArray("{\"version\":2,\"events\":[{\"id\":\"level-1\",\"task\":\"water\","
								   "\"kind\":\"skipped\",\"units\":250,\"weight\":20,"
								   "\"created_at\":\"2026-01-01T00:00:00.000Z\"}]}");
	QVERIFY(!writeFile(SettingsManager::configDir(), QStringLiteral("debt-ledger.json"), ledger).isEmpty());
	{
		ReminderManager manager(nullptr, nullptr, false);
		manager.setNowForTest(QDateTime(QDate(2026, 1, 1), QTime(9, 0)));
		manager.configure(waterOnlyTasks());
		QCOMPARE(manager.score(), 20);
		const auto body = manager.notificationPreview(QStringLiteral("water"));
		QVERIFY2(body.contains(QStringLiteral("TASK_WATER_DUE")), qPrintable(body));
		QVERIFY2(body.contains(QStringLiteral("LEVEL_CRITICAL")), qPrintable(body));
		QVERIFY2(body.contains(QStringLiteral("危急（20 分）")), qPrintable(body));
		QVERIFY2(body.indexOf(QStringLiteral("LEVEL_CRITICAL"))
					 < body.indexOf(QStringLiteral("TASK_WATER_DUE")),
				 qPrintable(body));
	}
}

void ProtectBrothersHealthTest::reminderManagerRunsBreakCycleAndBuildsReport() {
	resetBrohealthConfig();
	const auto		t0 = QDateTime(QDate(2026, 1, 1), QTime(9, 0));
	ReminderManager manager(nullptr, nullptr, false);
	manager.setNowForTest(t0);
	manager.configure(defaultTasks());
	QVERIFY(manager.debtReport().startsWith(QStringLiteral("兄弟账本清白")));
	QVERIFY(!manager.debtReport().contains(QStringLiteral("0 次喝水")));

	manager.pollAt(t0.addSecs(50 * 60));
	QVERIFY(manager.skipTask(QStringLiteral("water")));
	QVERIFY(manager.triggerNow(QStringLiteral("eye_break")));
	QVERIFY(manager.isBreaking());
	manager.skipCurrentBreak();
	QVERIFY(!manager.isBreaking());
	QCOMPARE(manager.outstandingCount(QStringLiteral("eye_break")), 1);
	QCOMPARE(manager.outstandingUnits(QStringLiteral("eye_break")), qint64(20));

	const auto report = manager.debtReport();
	QVERIFY2(report.contains(
				 QStringLiteral("你已经欠下了 1 次喝水，总计 250 ml；欠下了 1 次护眼休息，总计 20 分钟。")),
			 qPrintable(report));
	QVERIFY2(report.contains(QStringLiteral("兄弟债务分：3（轻微）")), qPrintable(report));
	QVERIFY(!manager.shortDebtSummary().isEmpty());

	QVERIFY(manager.triggerNow(QStringLiteral("eye_break")));
	manager.pollAt(t0.addSecs(140 * 60));
	QVERIFY(!manager.isBreaking());
	QCOMPARE(manager.outstandingCount(QStringLiteral("eye_break")), 1);
}

void ProtectBrothersHealthTest::settingsMigrateV2AndPersistReminders() {
	resetBrohealthConfig();
	const auto path = QDir(SettingsManager::configDir()).filePath(QStringLiteral("settings.json"));
	const auto v2	= QByteArray("{\"version\":2,\"default_preset\":\"old\",\"water_enabled\":false,"
								 "\"water_interval_minutes\":45,\"water_volume_ml\":300,"
								 "\"break_enabled\":true,\"work_minutes\":50,\"break_minutes\":10,"
								 "\"break_message\":\"抬头看看远处\",\"quiet_hours_enabled\":true,"
								 "\"quiet_hours_start\":\"23:00\",\"quiet_hours_end\":\"07:30\","
								 "\"autostart\":true}");
	QVERIFY(!writeFile(SettingsManager::configDir(), QStringLiteral("settings.json"), v2).isEmpty());

	SettingsManager settings;
	settings.load();
	const auto* water = settings.reminder(QStringLiteral("water"));
	const auto* rest  = settings.reminder(QStringLiteral("eye_break"));
	QVERIFY(water);
	QVERIFY(rest);
	QVERIFY(!water->_enabled);
	QCOMPARE(water->_intervalMinutes, 45);
	QCOMPARE(water->_unitsPerEvent, 300);
	QCOMPARE(rest->_intervalMinutes, 50);
	QCOMPARE(rest->_breakMinutes, 10);
	QCOMPARE(rest->_message, QStringLiteral("抬头看看远处"));
	QCOMPARE(settings.quietHoursStart(), QStringLiteral("23:00"));
	QCOMPARE(settings.quietHoursEnd(), QStringLiteral("07:30"));
	QVERIFY(settings.quietHoursEnabled());
	QVERIFY(settings.autostartEnabled());
	QCOMPARE(settings.defaultPreset(), QStringLiteral("old"));

	settings.save();
	const auto saved = QJsonDocument::fromJson(readFile(path).toUtf8()).object();
	QCOMPARE(saved.value(QStringLiteral("version")).toInt(), 3);
	QCOMPARE(saved.value(QStringLiteral("reminders")).toArray().size(), 2);

	SettingsManager restored;
	restored.load();
	QVERIFY(restored.reminder(QStringLiteral("water")));
	QCOMPARE(restored.reminder(QStringLiteral("water"))->_unitsPerEvent, 300);
	QCOMPARE(restored.reminder(QStringLiteral("eye_break"))->_breakMinutes, 10);

	QString error;
	QVERIFY(SettingsManager::applyAutostart(true, &error));
	const auto autostartPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
							   + QStringLiteral("/autostart/brohealth.desktop");
	QVERIFY(QFile::exists(autostartPath));
	const auto autostartContents = readFile(autostartPath);
	QVERIFY2(autostartContents.contains(QStringLiteral("--hidden")), qPrintable(autostartContents));
	QVERIFY2(autostartContents.contains(QStringLiteral("Exec=\"")), qPrintable(autostartContents));
	QVERIFY(SettingsManager::applyAutostart(false, &error));
	QVERIFY(!QFile::exists(autostartPath));

	QVERIFY(!writeFile(SettingsManager::configDir(),
					   QStringLiteral("settings.json"),
					   QByteArray("{ this is not settings"))
				 .isEmpty());
	SettingsManager corrupt;
	corrupt.load();
	QVERIFY(!corrupt.loadWarning().isEmpty());
	QVERIFY(!QDir(SettingsManager::configDir())
				 .entryList({QStringLiteral("settings.corrupt-*.json")}, QDir::Files)
				 .isEmpty());

	// A newer settings schema must never be downgraded and overwritten.
	const auto futureJson =
		QByteArray("{\"version\":4,\"future_field\":\"keep-me\",\"sync_brightness\":true}");
	QVERIFY(!writeFile(SettingsManager::configDir(), QStringLiteral("settings.json"), futureJson).isEmpty());
	SettingsManager future;
	future.load();
	QVERIFY(future.isReadOnly());
	QVERIFY(!future.loadWarning().isEmpty());
	future.save();
	QCOMPARE(readFile(path), QString::fromUtf8(futureJson));

	MonitorSettings storedA;
	storedA._name		= QStringLiteral("DP-1");
	storedA._brightness = 80;
	MonitorSettings storedB;
	storedB._name		= QStringLiteral("HDMI-1");
	storedB._brightness = 90;
	MonitorSettings currentA;
	currentA._name		 = QStringLiteral("DP-1");
	currentA._brightness = 50;
	const auto merged	 = SettingsManager::mergeMonitors({storedA, storedB}, {currentA});
	QCOMPARE(merged.size(), 2);
	QCOMPARE(merged.at(0)._brightness, 50);
	QCOMPARE(merged.at(1)._name, QStringLiteral("HDMI-1"));
	QCOMPARE(merged.at(1)._brightness, 90);
}

void ProtectBrothersHealthTest::presetsStoreReminderConfiguration() {
	resetBrohealthConfig();
	PresetManager presets;

	QJsonObject monitor;
	monitor[QStringLiteral("name")]		  = QStringLiteral("DP-1");
	monitor[QStringLiteral("brightness")] = 90;

	QJsonObject reminder;
	reminder[QStringLiteral("id")]				 = QStringLiteral("water");
	reminder[QStringLiteral("name")]			 = QStringLiteral("喝水");
	reminder[QStringLiteral("unit")]			 = QStringLiteral("ml");
	reminder[QStringLiteral("kind")]			 = QStringLiteral("periodic");
	reminder[QStringLiteral("enabled")]			 = true;
	reminder[QStringLiteral("interval_minutes")] = 50;
	reminder[QStringLiteral("units_per_event")]	 = 250;
	reminder[QStringLiteral("weight")]			 = 1;

	QJsonArray reminders;
	reminders.append(reminder);
	QJsonArray monitors;
	monitors.append(monitor);

	QJsonObject preset;
	preset[QStringLiteral("sync_brightness")] = true;
	preset[QStringLiteral("monitors")]		  = monitors;
	preset[QStringLiteral("reminders")]		  = reminders;

	QVERIFY(presets.save(QStringLiteral("unified"), preset));
	QCOMPARE(presets.load(QStringLiteral("unified")), preset);
	QVERIFY(presets.remove(QStringLiteral("unified")));
	QVERIFY(!presets.save(QStringLiteral("../outside"), {}));
	QVERIFY(presets.load(QStringLiteral("../outside")).isEmpty());
	QVERIFY(!presets.remove(QStringLiteral("../outside")));
}

void ProtectBrothersHealthTest::migratesLegacyConfigurationDirectory() {
	resetBrohealthConfig();
	// The migration only runs when the new directory does not exist yet.
	QDir(SettingsManager::configDir()).removeRecursively();
	const auto configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
	const auto legacyDir  = configRoot + "/screen-light-adjust";
	QVERIFY(QDir().mkpath(legacyDir));
	QVERIFY(!writeFile(legacyDir,
					   QStringLiteral("settings.json"),
					   QByteArray("{\"default_preset\":\"legacy\",\"break_enabled\":false}"))
				 .isEmpty());

	QVERIFY(!QDir(SettingsManager::configDir()).exists());
	SettingsManager::migrateLegacyConfig();
	QVERIFY(QDir(SettingsManager::configDir()).exists());
	QVERIFY(!QDir(legacyDir).exists());

	SettingsManager settings;
	settings.load();
	QCOMPARE(settings.defaultPreset(), QStringLiteral("legacy"));
	QVERIFY(!settings.reminder(QStringLiteral("eye_break"))->_enabled);
	QVERIFY(QFile::exists(SettingsManager::configDir() + "/settings.json"));
	QVERIFY(!QDir(legacyDir).exists());
}

// ---------------------------------------------------------------------------
// Break overlay tests
// ---------------------------------------------------------------------------

void ProtectBrothersHealthTest::reminderCreatesOneWindowPerScreen() {
	const auto screens = QGuiApplication::screens();
	QVERIFY(!screens.isEmpty());

	BreakReminder reminder;
	reminder.showBreakOnScreens(20, QStringLiteral("休息一下"), screens);
	QCOMPARE(reminder.visibleWindowCount(), screens.size());
	reminder.closeBreak();
	QCOMPARE(reminder.visibleWindowCount(), 0);
}

void ProtectBrothersHealthTest::reminderUsesTheSameMessageOnEveryScreen() {
	const auto screens = QGuiApplication::screens();
	QVERIFY(!screens.isEmpty());

	const QString message = QStringLiteral("请休息并眺望远方");
	BreakReminder reminder;
	reminder.showBreakOnScreens(15, message, screens);
	QCOMPARE(reminder.visibleMessages(), QStringList(screens.size(), message));
	reminder.closeBreak();
}

void ProtectBrothersHealthTest::breakOverlayOffersVisibleSkipControl() {
	const auto screens = QGuiApplication::screens();
	QVERIFY(!screens.isEmpty());

	BreakReminder reminder;
	reminder.showBreakOnScreens(1, QStringLiteral("请休息一下"), screens);
	QApplication::processEvents();

	QWidget* breakWindow = nullptr;
	for(auto* widget : QApplication::topLevelWidgets()) {
		if(widget->windowTitle() == QStringLiteral("休息时间")) {
			breakWindow = widget;
			break;
		}
	}
	QVERIFY(breakWindow);
	auto* skipButton = breakWindow->findChild<QPushButton*>();
	QVERIFY(skipButton);
	QCOMPARE(skipButton->text(), QStringLiteral("申请跳过…"));

	bool dialogShown = false;
	QTimer::singleShot(0, &reminder, [&dialogShown] {
		auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
		if(!dialog)
			return;
		dialogShown = true;
		dialog->reject();
	});
	QTest::mouseClick(skipButton, Qt::LeftButton);
	QTRY_VERIFY(dialogShown);
	reminder.closeBreak();
}

void ProtectBrothersHealthTest::escapeConfirmationDefaultsToContinue() {
	const auto screens = QGuiApplication::screens();
	QVERIFY(!screens.isEmpty());

	BreakReminder reminder;
	QSignalSpy	  skipSpy(&reminder, &BreakReminder::skipConfirmed);
	QVERIFY(skipSpy.isValid());
	reminder.showBreakOnScreens(1, QStringLiteral("请休息一下"), screens);
	QApplication::processEvents();

	QWidget* breakWindow = nullptr;
	for(auto* widget : QApplication::topLevelWidgets()) {
		if(widget->windowTitle() == QStringLiteral("休息时间")) {
			breakWindow = widget;
			break;
		}
	}
	QVERIFY(breakWindow);

	bool   continuePromptShown = false;
	QTimer continueWithEnter;
	continueWithEnter.setSingleShot(true);
	connect(&continueWithEnter, &QTimer::timeout, &reminder, [&continuePromptShown] {
		auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
		if(dialog) {
			continuePromptShown = true;
			QTest::keyClick(dialog, Qt::Key_Return);
		}
	});
	continueWithEnter.start();
	QTest::keyClick(breakWindow, Qt::Key_Escape);
	QTRY_VERIFY(continuePromptShown);
	QCOMPARE(skipSpy.count(), 0);
	QCOMPARE(reminder.visibleWindowCount(), screens.size());
	reminder.closeBreak();
}

void ProtectBrothersHealthTest::mouseClickConfirmsBreakSkip() {
	const auto screens = QGuiApplication::screens();
	QVERIFY(!screens.isEmpty());

	BreakReminder reminder;
	QSignalSpy	  skipSpy(&reminder, &BreakReminder::skipConfirmed);
	QVERIFY(skipSpy.isValid());
	reminder.showBreakOnScreens(1, QStringLiteral("请休息一下"), screens);
	QApplication::processEvents();

	QWidget* breakWindow = nullptr;
	for(auto* widget : QApplication::topLevelWidgets()) {
		if(widget->windowTitle() == QStringLiteral("休息时间")) {
			breakWindow = widget;
			break;
		}
	}
	QVERIFY(breakWindow);

	bool   skipButtonClicked = false;
	QTimer clickSkipButton;
	clickSkipButton.setSingleShot(true);
	connect(&clickSkipButton, &QTimer::timeout, &reminder, [&skipButtonClicked] {
		auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
		if(!dialog)
			return;
		for(auto* button : dialog->buttons()) {
			if(button->text() == QStringLiteral("确认跳过")) {
				QTest::mouseClick(button, Qt::LeftButton);
				skipButtonClicked = true;
				return;
			}
		}
	});
	clickSkipButton.start();
	QTest::keyClick(breakWindow, Qt::Key_Escape);
	QTRY_VERIFY(skipButtonClicked);
	QTRY_COMPARE(skipSpy.count(), 1);
	reminder.closeBreak();
}

void ProtectBrothersHealthTest::closingBreakCancelsSkipConfirmation() {
	const auto screens = QGuiApplication::screens();
	QVERIFY(!screens.isEmpty());

	BreakReminder reminder;
	reminder.showBreakOnScreens(1, QStringLiteral("请休息一下"), screens);
	QApplication::processEvents();

	QWidget* breakWindow = nullptr;
	for(auto* widget : QApplication::topLevelWidgets()) {
		if(widget->windowTitle() == QStringLiteral("休息时间")) {
			breakWindow = widget;
			break;
		}
	}
	QVERIFY(breakWindow);

	QTest::keyClick(breakWindow, Qt::Key_Escape);
	QTRY_VERIFY(qobject_cast<QMessageBox*>(QApplication::activeModalWidget()));
	reminder.closeBreak();
	QCOMPARE(reminder.visibleWindowCount(), 0);
	QTRY_VERIFY(QApplication::activeModalWidget() == nullptr);
}

int main(int argc, char* argv[]) {
	QTemporaryDir configHome;
	if(!configHome.isValid())
		return 2;
	qputenv("XDG_CONFIG_HOME", configHome.path().toLocal8Bit());

	QApplication			  app(argc, argv);
	ProtectBrothersHealthTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "brohealth_test.moc"
