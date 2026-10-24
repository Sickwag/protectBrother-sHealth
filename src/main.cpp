#include "mainwindow.h"
#include "platform_tray.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>

int main(int argc, char* argv[]) {
	QCoreApplication::setApplicationName("Protect Brother's Health");
	QCoreApplication::setOrganizationName("ProtectBrothersHealth");
	QGuiApplication::setDesktopFileName("brohealth");

	PlatformTray::preparePlatformTray();
	QApplication app(argc, argv);
	PlatformTray::restoreKdePalette();

	MainWindow w;
	w.show();

	return app.exec();
}
