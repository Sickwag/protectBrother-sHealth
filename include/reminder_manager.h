#pragma once

#include "debt_ledger.h"
#include "desktop_notifier.h"
#include "phrase_catalog.h"
#include "settings_manager.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTime>
#include <QTimer>
#include <QVector>

class BreakReminder;
class QSystemTrayIcon;

// Central scheduler and debt ledger for all reminder tasks. Water and eye
// breaks are just two configured tasks; adding another periodic task does not
// require another timer or another report code path.
class ReminderManager : public QObject {
	Q_OBJECT
  public:
	explicit ReminderManager(QSystemTrayIcon* trayIcon,
							 QObject*		  parent			  = nullptr,
							 bool			  enableNotifications = true);
	~ReminderManager() override;

	// Replaces the task list. Existing pending instances are preserved for
	// tasks that remain enabled; disabled tasks are cleared without a debt.
	void configure(const QVector<ReminderConfig>& tasks);
	void start();
	void stop();

	void setQuietHours(bool enabled, const QString& start, const QString& end);
	void setPausedUntil(const QDateTime& until);
	void setBreakMessage(const QString& message);
	void setPhraseAutoReload(bool enabled);

	// User actions. All return false when there is nothing to act on and keep
	// the pending instance intact when persistence fails.
	bool completeTask(const QString& taskId);
	bool skipTask(const QString& taskId);
	bool snoozeTask(const QString& taskId);
	bool repayOne(const QString& taskId);
	bool triggerNow(const QString& taskId);
	void skipCurrentBreak();

	bool	  hasPending(const QString& taskId) const;
	int		  pendingUnits(const QString& taskId) const;
	bool	  isBreaking() const;
	QTime	  breakRemaining() const;
	QDateTime nextDue(const QString& taskId) const;

	int			score() const;
	QString		levelName() const;
	int			outstandingCount(const QString& taskId) const;
	qint64		outstandingUnits(const QString& taskId) const;
	int			lifetimeDebtCount(const QString& taskId) const;
	qint64		lifetimeDebtUnits(const QString& taskId) const;
	int			repaymentCount(const QString& taskId) const;
	QStringList outstandingTaskIds() const;

	QVector<ReminderConfig> tasks() const;
	QString					taskName(const QString& taskId) const;
	QString					taskUnit(const QString& taskId) const;

	QString debtReport();
	QString shortDebtSummary() const;
	QString notificationPreview(const QString& taskId);
	QString actionPrompt();

	QString lastStatusPhrase() const { return _mLastStatusPhrase; }

	QString phraseConfigPath() const;
	QString phraseSourcePath() const;
	QString phraseLoadError() const;
	bool	phrasesUsingFallback() const;
	bool	reloadPhrases();
	bool	preparePhraseConfig(QString* error = nullptr);

	QString breakMessage() const { return _mBreakMessage; }

	// Deterministic clock hooks for tests and for suspend/resume handling.
	void setNowForTest(const QDateTime& now);
	void clearNowForTest();
	void pollAt(const QDateTime& now);

  signals:
	void stateChanged();
	void openLedgerRequested();
	void breakStarted(int minutes);
	void breakFinished();
	void breakSkipped();
	void countdownChanged(const QTime& remaining, bool working);
	void statusMessage(const QString& message);
	void errorOccurred(const QString& message);

  private slots:
	void onTick();
	void onNotificationAction(uint notificationId, const QString& action);
	void onNotificationClosed(uint notificationId, uint reason);
	void onBreakSkipConfirmed();
	void onPhrasesChanged();
	void onPhrasesReloadFailed(const QString& error);

  private:
	struct Runtime {
		ReminderConfig _config;
		QDateTime	   _nextDue;
		QDateTime	   _pendingDueAt;
		QDateTime	   _pendingDeadline;
		QString		   _instanceId;
		int			   _pendingUnits  = 0;
		int			   _pendingWeight = 0;
		int			   _snoozeCount	  = 0;
		bool		   _pending		  = false;
		bool		   _warningSent	  = false;
	};

	QDateTime now() const;
	void	  poll(const QDateTime& now);
	void	  pollTask(Runtime& runtime, const QDateTime& now);
	void	  pollBreak(Runtime& runtime, const QDateTime& now);
	void	  expirePending(Runtime& runtime, const QDateTime& now);
	void	  createPending(Runtime& runtime, const QDateTime& now);
	void	  notifyPending(Runtime& runtime);
	void	  notifyBreakWarning(const Runtime& runtime);
	void	  beginBreak(Runtime& runtime, const QDateTime& now);
	void	  finishBreak(const QDateTime& now, bool skipped);
	void	  emitCountdown(const QDateTime& now);

	void scheduleNext(Runtime& runtime, const QDateTime& from);
	void clearPending(Runtime& runtime, bool removeFile);
	void closeNotificationsForInstance(const QString& instanceId);
	bool recordDebtAndClear(Runtime& runtime, DebtLedger::EventKind kind, const QString& phraseEvent);
	bool isSuppressed(const QDateTime& now) const;
	bool isBreakTask(const QString& taskId) const;

	Runtime*	   runtimeFor(const QString& taskId);
	const Runtime* runtimeFor(const QString& taskId) const;

	bool	savePending();
	void	loadPending();
	void	emitError(const QString& message);
	void	setStatusPhrase(const QString& phrase);
	QString buildDebtSentence() const;
	QString notificationBody(const QString& taskId, int units);
	QString displayLevel() const;

	DesktopNotifier _mNotifier;
	DebtLedger		_mLedger;
	PhraseCatalog	_mPhrases;
	QTimer			_mTick;
	BreakReminder*	_mBreakReminder = nullptr;

	QHash<QString, Runtime> _mTasks;
	QStringList				_mTaskOrder;
	QHash<uint, QString>	_mNotificationInstances;

	QString	  _mBreakTaskId = QStringLiteral("eye_break");
	QString	  _mBreakMessage;
	QDateTime _mBreakEndsAt;
	QString	  _mBreakingTaskId;
	bool	  _mBreaking				= false;
	bool	  _mStarted					= false;
	bool	  _mEnableNotifications		= true;
	bool	  _mPendingLoaded			= false;
	bool	  _mPhraseWarningEmitted	= false;
	bool	  _mLedgerWritable			= true;
	bool	  _mPendingSaveErrorEmitted = false;
	bool	  _mShuttingDown			= false;

	QDateTime _mNowOverride;
	QDateTime _mPausedUntil;
	bool	  _mQuietHoursEnabled = false;
	QString	  _mQuietHoursStart	  = QStringLiteral("22:00");
	QString	  _mQuietHoursEnd	  = QStringLiteral("08:00");

	QString _mLastStatusPhrase;
	QString _mPendingStorePath;
	QString _mStartupError;
};
