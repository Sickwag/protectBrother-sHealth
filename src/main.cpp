#include "mainwindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
	QApplication app(argc, argv);
	app.setApplicationName("Protect Brother's Health");
	app.setOrganizationName("ProtectBrothersHealth");
	app.setDesktopFileName("brohealth");

	MainWindow w;
	w.show();

	return app.exec();
}
