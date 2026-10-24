#include "break_reminder.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>

namespace {
class BreakWindow final : public QWidget {
  public:
	BreakWindow(QScreen* screen, QString message, const QDateTime& deadline, std::function<void()> onSkip)
		: _mMessage(std::move(message))
		, _mDeadline(deadline)
		, _mOnSkip(std::move(onSkip)) {
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

		auto* hint = new QLabel(QStringLiteral("按 Esc 或点击下方按钮可以申请跳过；默认继续休息。"), this);
		hint->setObjectName(QStringLiteral("hint"));
		hint->setAlignment(Qt::AlignCenter);
		hint->setStyleSheet(QStringLiteral("color: #c2ccd6; font-size: 15px;"));
		layout->addWidget(hint);

		auto* skipButton = new QPushButton(QStringLiteral("申请跳过…"), this);
		skipButton->setFocusPolicy(Qt::NoFocus);
		skipButton->setStyleSheet(
			QStringLiteral("QPushButton { background: #2b3a48; color: #f4f7fa; border: 1px solid #4a5c6e;"
						   " border-radius: 6px; padding: 8px 18px; font-size: 15px; }"
						   "QPushButton:hover { background: #37495a; }"));
		if(_mOnSkip)
			connect(skipButton, &QPushButton::clicked, this, [this] {
				if(_mOnSkip)
					_mOnSkip();
			});
		layout->addWidget(skipButton, 0, Qt::AlignCenter);
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

	QString				  _mMessage;
	QDateTime			  _mDeadline;
	std::function<void()> _mOnSkip;
	QLabel*				  _mMessageLabel   = nullptr;
	QLabel*				  _mRemainingLabel = nullptr;
	bool				  _mAllowClose	   = false;
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

	auto* window = new BreakWindow(screen, _mMessage, _mDeadline, [this, screen] {
		for(const auto& guarded : _mWindows) {
			if(auto* widget = guarded.data(); widget && widget->screen() == screen) {
				showSkipConfirmation(widget);
				return;
			}
		}
	});
	window->setAttribute(Qt::WA_QuitOnClose, false);
	_mWindows.append(window);

	auto* shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), window);
	shortcut->setAutoRepeat(false);
	connect(shortcut, &QShortcut::activated, this, [this, window] { showSkipConfirmation(window); });

	window->showFullScreen();
	window->raise();
	window->activateWindow();
}

void BreakReminder::showSkipConfirmation(QWidget* parentWindow) {
	if(_mSkipConfirmationOpen)
		return;

	_mSkipConfirmationOpen = true;
	auto* dialog		   = new QMessageBox(parentWindow);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setIcon(QMessageBox::Question);
	dialog->setWindowTitle(QStringLiteral("确认跳过休息？"));
	dialog->setText(QStringLiteral("兄弟，真的要跳过这次休息吗？"));
	auto informativeText = _mActionPrompt.trimmed();
	if(!informativeText.isEmpty())
		informativeText += QLatin1Char('\n');
	informativeText += QStringLiteral("请用鼠标点击「确认跳过」；否则继续休息。");
	dialog->setInformativeText(informativeText);
	dialog->setWindowModality(Qt::WindowModal);
	dialog->setWindowFlag(Qt::WindowStaysOnTopHint);

	auto* continueButton = dialog->addButton(QStringLiteral("继续休息"), QMessageBox::RejectRole);
	auto* skipButton	 = dialog->addButton(QStringLiteral("确认跳过"), QMessageBox::AcceptRole);
	dialog->setDefaultButton(continueButton);
	dialog->setEscapeButton(continueButton);
	skipButton->setAutoDefault(false);
	skipButton->setDefault(false);
	skipButton->setFocusPolicy(Qt::NoFocus);

	const QPointer<QMessageBox> dialogGuard(dialog);
	connect(dialog, &QMessageBox::finished, this, [this, dialogGuard, skipButton] {
		const bool confirmed   = dialogGuard && dialogGuard->clickedButton() == skipButton;
		_mSkipConfirmationOpen = false;
		if(confirmed)
			QTimer::singleShot(0, this, [this] { emit skipConfirmed(); });
	});
	connect(dialog, &QObject::destroyed, this, [this] { _mSkipConfirmationOpen = false; });
	dialog->open();
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
