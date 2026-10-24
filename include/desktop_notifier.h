#pragma once

#include <QObject>
#include <QStringList>

class QSystemTrayIcon;

struct DesktopNotificationResult {
	uint _id		   = 0;
	bool _shown		   = false;
	bool _service	   = false;
	bool _actions	   = false;
	bool _trayFallback = false;
};

class DesktopNotifier : public QObject {
	Q_OBJECT
  public:
	explicit DesktopNotifier(QSystemTrayIcon* trayIcon, QObject* parent = nullptr);

	bool notificationServiceAvailable() const;
	bool actionsSupported() const;

	DesktopNotificationResult notify(const QString&		summary,
									 const QString&		body,
									 const QStringList& actions				= {},
									 int				timeoutMilliseconds = 10000,
									 uint				replacesId			= 0);

	// Removes a notification that has already been answered, so stale action
	// buttons cannot be clicked after the reminder was resolved.
	void close(uint notificationId);

  signals:
	void actionInvoked(uint notificationId, const QString& action);
	void notificationClosed(uint notificationId, uint reason);

  private slots:
	void onActionInvoked(uint notificationId, const QString& action);
	void onNotificationClosed(uint notificationId, uint reason);
	void onNameOwnerChanged(const QString& service, const QString& oldOwner, const QString& newOwner);

  private:
	QSystemTrayIcon* _mTrayIcon		= nullptr;

	// -1 unknown, 0 unsupported, 1 supported. The capability list is stable for
	// the lifetime of the notification daemon in practice; caching it avoids a
	// blocking D-Bus round-trip for every reminder.
	mutable int _mActionsCapability = -1;
};
