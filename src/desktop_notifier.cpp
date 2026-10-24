#include "desktop_notifier.h"

#include <QDebug>
#include <QSystemTrayIcon>
#include <QVariantMap>

#ifdef BROHEALTH_HAVE_DBUS
#	include <QDBusConnection>
#	include <QDBusConnectionInterface>
#	include <QDBusMessage>
#	include <QDBusReply>
#endif

namespace {
constexpr auto K_NOTIFICATION_SERVICE	= "org.freedesktop.Notifications";
constexpr auto K_NOTIFICATION_PATH		= "/org/freedesktop/Notifications";
constexpr auto K_NOTIFICATION_INTERFACE = "org.freedesktop.Notifications";
}  // namespace

DesktopNotifier::DesktopNotifier(QSystemTrayIcon* trayIcon, QObject* parent)
	: QObject(parent)
	, _mTrayIcon(trayIcon) {
#ifdef BROHEALTH_HAVE_DBUS
	auto bus = QDBusConnection::sessionBus();
	bus.connect(K_NOTIFICATION_SERVICE,
				K_NOTIFICATION_PATH,
				K_NOTIFICATION_INTERFACE,
				"ActionInvoked",
				this,
				SLOT(onActionInvoked(uint, QString)));
	bus.connect(K_NOTIFICATION_SERVICE,
				K_NOTIFICATION_PATH,
				K_NOTIFICATION_INTERFACE,
				"NotificationClosed",
				this,
				SLOT(onNotificationClosed(uint, uint)));
	bus.connect(QStringLiteral("org.freedesktop.DBus"),
				QStringLiteral("/org/freedesktop/DBus"),
				QStringLiteral("org.freedesktop.DBus"),
				QStringLiteral("NameOwnerChanged"),
				this,
				SLOT(onNameOwnerChanged(QString, QString, QString)));
#endif
}

bool DesktopNotifier::actionsSupported() const {
#ifdef BROHEALTH_HAVE_DBUS
	if(_mActionsCapability == 0)
		return false;
	if(_mActionsCapability == 1)
		return true;

	auto bus = QDBusConnection::sessionBus();
	if(!bus.isConnected())
		return false;

	QDBusMessage message = QDBusMessage::createMethodCall(K_NOTIFICATION_SERVICE,
														  K_NOTIFICATION_PATH,
														  K_NOTIFICATION_INTERFACE,
														  QStringLiteral("GetCapabilities"));
	const auto	 reply	 = bus.call(message, QDBus::Block, 800);
	if(reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
		_mActionsCapability = 0;
		return false;
	}
	const auto capabilities = qdbus_cast<QStringList>(reply.arguments().first());
	_mActionsCapability		= capabilities.contains(QStringLiteral("actions")) ? 1 : 0;
	return _mActionsCapability == 1;
#else
	return false;
#endif
}

bool DesktopNotifier::notificationServiceAvailable() const {
#ifdef BROHEALTH_HAVE_DBUS
	const auto bus = QDBusConnection::sessionBus();
	if(!bus.isConnected() || !bus.interface())
		return false;
	const auto reply = bus.interface()->isServiceRegistered(QString::fromLatin1(K_NOTIFICATION_SERVICE));
	return reply.isValid() && reply.value();
#else
	return false;
#endif
}

DesktopNotificationResult DesktopNotifier::notify(const QString&	 summary,
												  const QString&	 body,
												  const QStringList& actions,
												  int				 timeoutMilliseconds,
												  uint				 replacesId) {
	DesktopNotificationResult result;
#ifdef BROHEALTH_HAVE_DBUS
	auto bus = QDBusConnection::sessionBus();
	if(bus.isConnected()) {
		result._service				= true;
		result._actions				= !actions.isEmpty() && actionsSupported();
		const auto supportedActions = result._actions ? actions : QStringList{};

		QDBusMessage message		= QDBusMessage::createMethodCall(
			   K_NOTIFICATION_SERVICE, K_NOTIFICATION_PATH, K_NOTIFICATION_INTERFACE, QStringLiteral("Notify"));

		QVariantMap hints;
		hints.insert(QStringLiteral("category"), QStringLiteral("reminder"));
		hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("brohealth"));
		hints.insert(QStringLiteral("urgency"), 1);

		message << QStringLiteral("Protect Brother's Health") << replacesId << QStringLiteral("brohealth")
				<< summary << body << supportedActions << hints << timeoutMilliseconds;
		const auto reply = bus.call(message, QDBus::Block, 1200);
		if(reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
			result._id	  = reply.arguments().first().toUInt();
			result._shown = true;
			return result;
		}
		qWarning().noquote() << QStringLiteral("系统通知发送失败：%1").arg(summary);
	}
#endif

	if(_mTrayIcon && QSystemTrayIcon::isSystemTrayAvailable()) {
		_mTrayIcon->showMessage(summary, body, QSystemTrayIcon::Information, timeoutMilliseconds);
		result._shown		 = true;
		result._trayFallback = true;
	}
	return result;
}

void DesktopNotifier::close(uint notificationId) {
#ifdef BROHEALTH_HAVE_DBUS
	auto bus = QDBusConnection::sessionBus();
	if(!bus.isConnected())
		return;
	QDBusMessage message = QDBusMessage::createMethodCall(K_NOTIFICATION_SERVICE,
														  K_NOTIFICATION_PATH,
														  K_NOTIFICATION_INTERFACE,
														  QStringLiteral("CloseNotification"));
	message << notificationId;
	bus.call(message, QDBus::Block, 300);
#else
	Q_UNUSED(notificationId);
#endif
}

void DesktopNotifier::onActionInvoked(uint notificationId, const QString& action) {
	emit actionInvoked(notificationId, action);
}

void DesktopNotifier::onNotificationClosed(uint notificationId, uint reason) {
	emit notificationClosed(notificationId, reason);
}

void DesktopNotifier::onNameOwnerChanged(const QString& service,
										 const QString& oldOwner,
										 const QString& newOwner) {
	Q_UNUSED(oldOwner);
	if(service == QString::fromLatin1(K_NOTIFICATION_SERVICE) && !newOwner.isEmpty())
		_mActionsCapability = -1;
}
