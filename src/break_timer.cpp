#include "break_timer.h"

#include <algorithm>

namespace {
constexpr int K_MINIMUM_MINUTES = 1;
constexpr int K_MAXIMUM_MINUTES = 150;
}  // namespace

BreakTimer::BreakTimer(int workMinutes, int breakMinutes, QObject* parent)
	: QObject(parent)
	, _mWorkMinutes(workMinutes)
	, _mBreakMinutes(breakMinutes) {
	_mWorkTimer.setSingleShot(true);
	_mBreakTimer.setSingleShot(true);
	_mUpdateTimer.setInterval(1000);

	connect(&_mWorkTimer, &QTimer::timeout, this, [this] { emit workPeriodFinished(_mBreakMinutes); });
	connect(&_mBreakTimer, &QTimer::timeout, this, &BreakTimer::breakPeriodFinished);
	connect(&_mUpdateTimer, &QTimer::timeout, this, [this] {
		emit remainingTimeChanged(remainingTime(), _mIsWorking);
	});

	setDurations(workMinutes, breakMinutes);
}

void BreakTimer::setDurations(int workMinutes, int breakMinutes) {
	_mWorkMinutes  = std::clamp(workMinutes, K_MINIMUM_MINUTES, K_MAXIMUM_MINUTES);
	_mBreakMinutes = std::clamp(breakMinutes, K_MINIMUM_MINUTES, K_MAXIMUM_MINUTES);
}

void BreakTimer::startWork() {
	_mBreakTimer.stop();
	_mIsWorking = true;
	_mElapsedTimer.start();
	_mWorkTimer.start(_mWorkMinutes * 60 * 1000);
	_mUpdateTimer.start();
	emit remainingTimeChanged(remainingTime(), _mIsWorking);
}

void BreakTimer::startBreak() {
	_mWorkTimer.stop();
	_mIsWorking = false;
	_mElapsedTimer.start();
	_mBreakTimer.start(_mBreakMinutes * 60 * 1000);
	_mUpdateTimer.start();
	emit remainingTimeChanged(remainingTime(), _mIsWorking);
}

void BreakTimer::stop() {
	_mWorkTimer.stop();
	_mBreakTimer.stop();
	_mUpdateTimer.stop();
}

QTime BreakTimer::remainingTime() const {
	const bool timerActive = _mIsWorking ? _mWorkTimer.isActive() : _mBreakTimer.isActive();
	if(!timerActive || !_mElapsedTimer.isValid())
		return QTime(0, 0, 0);

	const int	 phaseMinutes	  = _mIsWorking ? _mWorkMinutes : _mBreakMinutes;
	const qint64 remainingSeconds = std::max<qint64>(
		0, (static_cast<qint64>(phaseMinutes) * 60 * 1000 - _mElapsedTimer.elapsed()) / 1000);
	return QTime(static_cast<int>(remainingSeconds / 3600),
				 static_cast<int>((remainingSeconds % 3600) / 60),
				 static_cast<int>(remainingSeconds % 60));
}
