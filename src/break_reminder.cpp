#include "break_reminder.h"

#include <QApplication>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace {
class BreakWindow final : public QWidget {
  public:
	BreakWindow(QScreen* screen, QString message, const QDateTime& deadline)
		: _mMessage(std::move(message))
		, _mDeadline(deadline) {
		setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
		setWindowTitle(QStringLiteral("休息时间"));
		setAttribute(Qt::WA_DeleteOnClose, false);
		setStyleSheet(QStringLiteral("QWidget { background: #17212b; color: #f4f7fa; }"
									 "QLabel#title { color: #9ac8ff; font-size: 20px; }"
									 "QLabel#message { font-size: 32px; font-weight: 600; }"
									 "QLabel#remaining { color: #c2ccd6; font-size: 18px; }"));

		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(48, 48, 48, 48);
		layout->setSpacing(20);
		layout->addStretch();

		auto* title = new QLabel(QStringLiteral("休息时间"), this);
		title->setObjectName(QStringLiteral("title"));
		title->setAlignment(Qt::AlignCenter);
		layout->addWidget(title);

		_mMessageLabel = new QLabel(_mMessage, this);
		_mMessageLabel->setObjectName(QStringLiteral("message"));
		_mMessageLabel->setAlignment(Qt::AlignCenter);
		_mMessageLabel->setWordWrap(true);
		layout->addWidget(_mMessageLabel);

		_mRemainingLabel = new QLabel(this);
		_mRemainingLabel->setObjectName(QStringLiteral("remaining"));
		_mRemainingLabel->setAlignment(Qt::AlignCenter);
		layout->addWidget(_mRemainingLabel);
		layout->addStretch();

		setScreen(screen);
		setGeometry(screen->geometry());
		updateRemaining();

		auto* refreshTimer = new QTimer(this);
		refreshTimer->setInterval(1000);
		connect(refreshTimer, &QTimer::timeout, this, [this] { updateRemaining(); });
		refreshTimer->start();
	}

	QString message() const { return _mMessage; }

	void dismiss() {
		_mAllowClose = true;
		close();
	}

  protected:
	void closeEvent(QCloseEvent* event) override {
		if(_mAllowClose)
			event->accept();
		else
			event->ignore();
	}

	void keyPressEvent(QKeyEvent* event) override { event->ignore(); }

  private:
	void updateRemaining() {
		const qint64 seconds		  = std::max<qint64>(0, QDateTime::currentDateTime().secsTo(_mDeadline));
		const auto	 minutes		  = seconds / 60;
		const auto	 remainingSeconds = seconds % 60;
		_mRemainingLabel->setText(QStringLiteral("休息剩余 %1:%2")
									  .arg(minutes, 2, 10, QLatin1Char('0'))
									  .arg(remainingSeconds, 2, 10, QLatin1Char('0')));
	}

	QString	  _mMessage;
	QDateTime _mDeadline;
	QLabel*	  _mMessageLabel   = nullptr;
	QLabel*	  _mRemainingLabel = nullptr;
	bool	  _mAllowClose	   = false;
};
}  // namespace

BreakReminder::BreakReminder(QObject* parent)
	: QObject(parent) {
	connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
		if(_mActive)
			addWindow(screen);
	});
	connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen* screen) {
		for(auto it = _mWindows.begin(); it != _mWindows.end();) {
			auto* widget = it->data();
			if(widget && widget->screen() == screen) {
				auto* window = static_cast<BreakWindow*>(widget);
				window->dismiss();
				delete window;
				it = _mWindows.erase(it);
			} else {
				++it;
			}
		}
	});
}

BreakReminder::~BreakReminder() { closeBreak(); }

void BreakReminder::showBreak(int breakMinutes) {
	showBreakWithMessage(breakMinutes, QStringLiteral("请暂停工作，眺望远方并活动身体。"));
}

void BreakReminder::showBreakWithMessage(int breakMinutes, const QString& message) {
	showBreakOnScreens(breakMinutes, message, QGuiApplication::screens());
}

void BreakReminder::showBreakOnScreens(int					  breakMinutes,
									   const QString&		  message,
									   const QList<QScreen*>& screens) {
	closeBreak();
	_mMessage =
		message.trimmed().isEmpty() ? QStringLiteral("请暂停工作，眺望远方并活动身体。") : message.trimmed();
	_mDeadline = QDateTime::currentDateTime().addSecs(std::max(1, breakMinutes) * 60);
	_mActive   = true;

	for(auto* screen : screens)
		addWindow(screen);
}

void BreakReminder::addWindow(QScreen* screen) {
	if(!screen)
		return;

	auto* window = new BreakWindow(screen, _mMessage, _mDeadline);
	_mWindows.append(window);
	window->showFullScreen();
	window->raise();
}

void BreakReminder::closeBreak() {
	_mActive = false;
	for(const auto& guardedWindow : _mWindows) {
		if(auto* widget = guardedWindow.data()) {
			static_cast<BreakWindow*>(widget)->dismiss();
			delete widget;
		}
	}
	_mWindows.clear();
}

int BreakReminder::visibleWindowCount() const {
	return static_cast<int>(std::count_if(_mWindows.cbegin(), _mWindows.cend(), [](const auto& window) {
		return window && window->isVisible();
	}));
}

QStringList BreakReminder::visibleMessages() const {
	QStringList messages;
	for(const auto& guardedWindow : _mWindows) {
		if(const auto* widget = guardedWindow.data()) {
			if(widget->isVisible())
				messages.append(static_cast<const BreakWindow*>(widget)->message());
		}
	}
	return messages;
}
