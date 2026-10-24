#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

class DebtLedger {
  public:
	enum class EventKind {
		Skipped,
		Missed,
		Completed,
		Repayment,
	};

	struct Event {
		QString	  _id;
		QString	  _taskId;
		EventKind _kind	  = EventKind::Skipped;
		int		  _units  = 0;
		int		  _weight = 0;
		QString	  _repaidEventId;
		QString	  _instanceId;	// idempotency key for a pending reminder
		QDateTime _createdAt;
	};

	struct LoadResult {
		bool	_ok		   = true;
		bool	_recovered = false;
		QString _error;
		QString _backupPath;
	};

	explicit DebtLedger(QString filePath = {});

	// Loads the append-only event log. When the file is corrupt the original
	// bytes are copied next to it before the ledger is reset, so a damaged file
	// can never be silently overwritten by the next reminder.
	LoadResult load();

	bool
	recordDebt(const QString& taskId, EventKind kind, int units, int weight, const QString& instanceId = {});
	bool recordCompletion(const QString& taskId, int units, const QString& instanceId = {});

	// True when an event was already committed for this pending reminder.
	// Checking it before recording makes the ledger/pending crash window
	// idempotent.
	bool hasEventForInstance(const QString& instanceId) const;

	// Appends a repayment event for the oldest unpaid debt of this task. The
	// debt event itself is kept forever so reports and audits remain possible.
	bool repayOne(const QString& taskId);

	int	   outstandingCount(const QString& taskId) const;
	qint64 outstandingUnits(const QString& taskId) const;
	int	   lifetimeDebtCount(const QString& taskId) const;
	qint64 lifetimeDebtUnits(const QString& taskId) const;
	int	   repaymentCount(const QString& taskId) const;

	// Task ids that still have unpaid debt, including tasks that are no longer
	// present in the current configuration. Reports use this to avoid hiding
	// debt from removed tasks.
	QStringList outstandingTaskIds() const;

	// Weighted debt score over all outstanding debt events. Repayments reduce
	// the score; completed events never do.
	int score() const;

	const QList<Event>& events() const { return _mEvents; }

	QString filePath() const { return _mFilePath; }

	bool isWritable() const { return _mWritable; }

	static QString	 levelForScore(int score);
	static QString	 kindName(EventKind kind);
	static EventKind kindFromName(const QString& name, bool* ok = nullptr);

  private:
	bool save() const;

	QString		 _mFilePath;
	QList<Event> _mEvents;
	bool		 _mWritable = true;
};
