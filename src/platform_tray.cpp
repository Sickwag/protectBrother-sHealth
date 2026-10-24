#include "platform_tray.h"

#include <QApplication>
#include <QColor>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QtGlobal>

#if defined(BROHEALTH_HAVE_DBUS)
#	include <QDBusConnection>
#	include <QDBusMessage>
#	include <QDBusVariant>
#endif

namespace {

bool gRestoreKdePalette = false;

#if defined(BROHEALTH_HAVE_DBUS)
bool isX11Session() {
	const QString platform = qEnvironmentVariable("QT_QPA_PLATFORM");
	if(platform.startsWith(QLatin1String("xcb")))
		return true;
	if(platform.contains(QLatin1String("wayland"), Qt::CaseInsensitive))
		return false;

	const QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE").toLower();
	if(sessionType == QLatin1String("wayland"))
		return false;
	if(sessionType == QLatin1String("x11"))
		return true;
	if(!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
		return false;
	return !qEnvironmentVariableIsEmpty("DISPLAY");
}

bool usesKdePlatformTheme() {
	const QString theme = qEnvironmentVariable("QT_QPA_PLATFORMTHEME");
	if(!theme.isEmpty())
		return theme == QLatin1String("kde");
	return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"), Qt::CaseInsensitive);
}

bool kdeThemePluginAvailable() {
	const QString directory =
		QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/platformthemes/");
	return QFileInfo::exists(directory + QStringLiteral("KDEPlasmaPlatformTheme6.so"))
		   || QFileInfo::exists(directory + QStringLiteral("KDEPlasmaPlatformTheme.so"));
}

bool gtk3ThemeAvailable() {
	const QString directory =
		QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/platformthemes/");
	return QFileInfo::exists(directory + QStringLiteral("libqgtk3.so"));
}

bool breezeStyleAvailable() {
	const QString directory = QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/styles/");
	return QFileInfo::exists(directory + QStringLiteral("breeze6.so"))
		   || QFileInfo::exists(directory + QStringLiteral("breeze.so"));
}

bool statusNotifierHostRegistered() {
	if(qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS"))
		return false;

	const QDBusConnection bus = QDBusConnection::sessionBus();
	if(!bus.isConnected())
		return false;

	auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.StatusNotifierWatcher"),
												  QStringLiteral("/StatusNotifierWatcher"),
												  QStringLiteral("org.freedesktop.DBus.Properties"),
												  QStringLiteral("Get"));
	message << QStringLiteral("org.kde.StatusNotifierWatcher")
			<< QStringLiteral("IsStatusNotifierHostRegistered");

	const auto reply = bus.call(message, QDBus::Block, 300);
	if(reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
		return false;
	return reply.arguments().first().value<QDBusVariant>().variant().toBool();
}
#endif

}  // namespace

void PlatformTray::preparePlatformTray() {
#if defined(BROHEALTH_HAVE_DBUS)
	if(!isX11Session() || !usesKdePlatformTheme() || !kdeThemePluginAvailable()
	   || statusNotifierHostRegistered())
		return;

	// KDE's platform theme only publishes StatusNotifierItems; i3bar needs XEmbed.
	if(gtk3ThemeAvailable()) {
		qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
		if(qEnvironmentVariableIsEmpty("QT_STYLE_OVERRIDE") && breezeStyleAvailable())
			qputenv("QT_STYLE_OVERRIDE", "Breeze");
	} else {
		qputenv("QT_QPA_PLATFORMTHEME", "generic");
	}
	gRestoreKdePalette = true;
#endif
}

void PlatformTray::restoreKdePalette() {
	if(!gRestoreKdePalette)
		return;

	const QString path =
		QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
	if(path.isEmpty())
		return;

	const QSettings settings(path, QSettings::IniFormat);

	struct PaletteEntry {
		const char*			_section;
		const char*			_key;
		QPalette::ColorRole _role;
	};

	static const PaletteEntry entries[] = {
		{"Colors:Window",	  "BackgroundNormal",	  QPalette::Window		  },
		{"Colors:Window",	  "ForegroundNormal",	  QPalette::WindowText	  },
		{"Colors:View",		"BackgroundNormal",	QPalette::Base			  },
		{"Colors:View",		"BackgroundAlternate", QPalette::AlternateBase  },
		{"Colors:View",		"ForegroundNormal",	QPalette::Text			  },
		{"Colors:Button",	  "BackgroundNormal",	  QPalette::Button		  },
		{"Colors:Button",	  "ForegroundNormal",	  QPalette::ButtonText	  },
		{"Colors:Selection", "BackgroundNormal",	 QPalette::Highlight		},
		{"Colors:Selection", "ForegroundNormal",	 QPalette::HighlightedText},
	};

	QPalette palette = QApplication::palette();
	for(const auto& entry : entries) {
		const QString key =
			QString::fromLatin1(entry._section) + QLatin1Char('/') + QString::fromLatin1(entry._key);
		const QStringList rgb = settings.value(key).toStringList();
		if(rgb.size() < 3)
			continue;

		const QColor color(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt());
		palette.setColor(QPalette::Active, entry._role, color);
		palette.setColor(QPalette::Inactive, entry._role, color);
	}
	QApplication::setPalette(palette);
}
