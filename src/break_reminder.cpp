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
		: m_message(std::move(message))
		, m_deadline(deadline) {
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

		m_message_label = new QLabel(m_message, this);
		m_message_label->setObjectName(QStringLiteral("message"));
		m_message_label->setAlignment(Qt::AlignCenter);
		m_message_label->setWordWrap(true);
		layout->addWidget(m_message_label);

		m_remaining_label = new QLabel(this);
		m_remaining_label->setObjectName(QStringLiteral("remaining"));
		m_remaining_label->setAlignment(Qt::AlignCenter);
		layout->addWidget(m_remaining_label);
		layout->addStretch();

		setScreen(screen);
		setGeometry(screen->geometry());
		updateRemaining();

		auto* refresh_timer = new QTimer(this);
		refresh_timer->setInterval(1000);
		connect(refresh_timer, &QTimer::timeout, this, [this] { updateRemaining(); });
		refresh_timer->start();
	}

	QString message() const { return m_message; }

	void dismiss() {
		m_allow_close = true;
		close();
	}

  protected:
	void closeEvent(QCloseEvent* event) override {
		if(m_allow_close)
			event->accept();
		else
			event->ignore();
	}

	void keyPressEvent(QKeyEvent* event) override { event->ignore(); }

  private:
	void updateRemaining() {
		const qint64 seconds		   = std::max<qint64>(0, QDateTime::currentDateTime().secsTo(m_deadline));
		const auto	 minutes		   = seconds / 60;
		const auto	 remaining_seconds = seconds % 60;
		m_remaining_label->setText(QStringLiteral("休息剩余 %1:%2")
									   .arg(minutes, 2, 10, QLatin1Char('0'))
									   .arg(remaining_seconds, 2, 10, QLatin1Char('0')));
	}

	QString	  m_message;
	QDateTime m_deadline;
	QLabel*	  m_message_label	= nullptr;
	QLabel*	  m_remaining_label = nullptr;
	bool	  m_allow_close		= false;
};
}  // namespace

BreakReminder::BreakReminder(QObject* parent)
	: QObject(parent) {
	connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
		if(m_active)
			addWindow(screen);
	});
	connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen* screen) {
		for(auto it = m_windows.begin(); it != m_windows.end();) {
			auto* widget = it->data();
			if(widget && widget->screen() == screen) {
				auto* window = static_cast<BreakWindow*>(widget);
				window->dismiss();
				delete window;
				it = m_windows.erase(it);
			} else {
				++it;
			}
		}
	});
}

BreakReminder::~BreakReminder() { close_break(); }

void BreakReminder::show_break(int break_minutes) {
	show_break_with_message(break_minutes, QStringLiteral("请暂停工作，眺望远方并活动身体。"));
}

void BreakReminder::show_break_with_message(int break_minutes, const QString& message) {
	show_break_on_screens(break_minutes, message, QGuiApplication::screens());
}

void BreakReminder::show_break_on_screens(int					 break_minutes,
										  const QString&		 message,
										  const QList<QScreen*>& screens) {
	close_break();
	m_message =
		message.trimmed().isEmpty() ? QStringLiteral("请暂停工作，眺望远方并活动身体。") : message.trimmed();
	m_deadline = QDateTime::currentDateTime().addSecs(std::max(1, break_minutes) * 60);
	m_active   = true;

	for(auto* screen : screens)
		addWindow(screen);
}

void BreakReminder::addWindow(QScreen* screen) {
	if(!screen)
		return;

	auto* window = new BreakWindow(screen, m_message, m_deadline);
	m_windows.append(window);
	window->showFullScreen();
	window->raise();
}

void BreakReminder::close_break() {
	m_active = false;
	for(const auto& guarded_window : m_windows) {
		if(auto* widget = guarded_window.data()) {
			static_cast<BreakWindow*>(widget)->dismiss();
			delete widget;
		}
	}
	m_windows.clear();
}

int BreakReminder::visible_window_count() const {
	return static_cast<int>(std::count_if(m_windows.cbegin(), m_windows.cend(), [](const auto& window) {
		return window && window->isVisible();
	}));
}

QStringList BreakReminder::visible_messages() const {
	QStringList messages;
	for(const auto& guarded_window : m_windows) {
		if(const auto* widget = guarded_window.data()) {
			if(widget->isVisible())
				messages.append(static_cast<const BreakWindow*>(widget)->message());
		}
	}
	return messages;
}
