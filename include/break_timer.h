#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTime>
#include <QTimer>

class BreakTimer : public QObject {
	Q_OBJECT
  public:
	explicit BreakTimer(int workMinutes, int breakMinutes, QObject* parent = nullptr);

	void setDurations(int workMinutes, int breakMinutes);
	void startWork();
	void startBreak();
	void stop();

	QTime remainingTime() const;

	bool isWorking() const { return _mIsWorking; }

  signals:
	void workPeriodFinished(int breakMinutes);
	void breakPeriodFinished();
	void remainingTimeChanged(const QTime& remaining, bool isWorking);

  private:
	QTimer		  _mWorkTimer;
	QTimer		  _mBreakTimer;
	QTimer		  _mUpdateTimer;
	QElapsedTimer _mElapsedTimer;
	int			  _mWorkMinutes;
	int			  _mBreakMinutes;
	bool		  _mIsWorking = true;
};
