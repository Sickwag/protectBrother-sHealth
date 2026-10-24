#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QScreen;
class QWidget;

class BreakReminder : public QObject {
	Q_OBJECT
  public:
	explicit BreakReminder(QObject* parent = nullptr);
	~BreakReminder() override;

	void showBreak(int breakMinutes);
	void showBreakWithMessage(int breakMinutes, const QString& message);
	void showBreakOnScreens(int breakMinutes, const QString& message, const QList<QScreen*>& screens);
	void closeBreak();

	int			visibleWindowCount() const;
	QStringList visibleMessages() const;

  signals:
	void skipConfirmed();

  private:
	void addWindow(QScreen* screen);
	void showSkipConfirmation(QWidget* parentWindow);

	QList<QPointer<QWidget>> _mWindows;
	QString					 _mMessage;
	QDateTime				 _mDeadline;
	bool					 _mActive				= false;
	bool					 _mSkipConfirmationOpen = false;
};
