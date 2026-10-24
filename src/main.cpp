#include "mainwindow.h"
#include "phrase_catalog.h"
#include "platform_tray.h"
#include "settings_manager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLockFile>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QTextStream>

#include <cstdio>

namespace {
void printCheckUsage() {
	QTextStream out(stdout);
	out << "用法：brohealth --check-phrases [文件]\n"
		<< "  --phrases <文件>   使用指定的文案文件\n"
		<< "  --hidden           启动后隐藏主窗口（有托盘时）\n";
}
}  // namespace

int main(int argc, char* argv[]) {
	QString phrasesPath;
	bool	hidden		 = false;
	bool	checkPhrases = false;

	for(int i = 1; i < argc; ++i) {
		const QString argument = QString::fromLocal8Bit(argv[i]);
		if(argument == QStringLiteral("--hidden")) {
			hidden = true;
		} else if(argument == QStringLiteral("--check-phrases")) {
			checkPhrases = true;
			if(i + 1 < argc && !QString::fromLocal8Bit(argv[i + 1]).startsWith(QLatin1Char('-')))
				phrasesPath = QString::fromLocal8Bit(argv[++i]);
		} else if(argument == QStringLiteral("--phrases") && i + 1 < argc) {
			phrasesPath = QString::fromLocal8Bit(argv[++i]);
		} else if(argument == QStringLiteral("--help") || argument == QStringLiteral("-h")) {
			printCheckUsage();
			return 0;
		}
	}

	if(checkPhrases) {
		QCoreApplication app(argc, argv);
		QCoreApplication::setApplicationName(QStringLiteral("Protect Brother's Health"));
		QCoreApplication::setOrganizationName(QStringLiteral("ProtectBrothersHealth"));

		const auto	path = phrasesPath.isEmpty() ? PhraseCatalog::installedConfigPath() : phrasesPath;
		QString		report;
		QString		error;
		int			line = 0;
		const bool	ok	 = PhraseCatalog::validateFile(path, &report, &error, &line);
		QTextStream out(stdout);
		if(!report.isEmpty())
			out << report << '\n';
		if(!ok) {
			out << "格式错误";
			if(line > 0)
				out << "（第 " << line << " 行）";
			out << "：" << error << '\n';
			return 1;
		}
		out << "文案格式检查通过。\n";
		return 0;
	}

	QCoreApplication::setApplicationName(QStringLiteral("Protect Brother's Health"));
	QCoreApplication::setOrganizationName(QStringLiteral("ProtectBrothersHealth"));
	QGuiApplication::setDesktopFileName(QStringLiteral("brohealth"));

	if(!phrasesPath.isEmpty())
		qputenv("BROHEALTH_PHRASES_CONF", phrasesPath.toLocal8Bit());

	PlatformTray::preparePlatformTray();
	QApplication app(argc, argv);
	PlatformTray::restoreKdePalette();

	// Must happen before mkpath()/QLockFile: both create the new directory and
	// would otherwise prevent the legacy screen-light-adjust rename.
	SettingsManager::migrateLegacyConfig();

	const auto configDir = SettingsManager::configDir();
	if(!QDir().mkpath(configDir)) {
		QMessageBox::warning(
			nullptr, QStringLiteral("无法启动"), QStringLiteral("无法创建用户配置目录：%1").arg(configDir));
		return 1;
	}

	QLockFile lock(configDir + QStringLiteral("/brohealth.lock"));
	lock.setStaleLockTime(30000);
	if(!lock.tryLock(100)) {
		QMessageBox::information(nullptr,
								 QStringLiteral("兄弟已经在值班"),
								 QStringLiteral("Protect Brother's Health 已经在运行。"));
		return 0;
	}

	MainWindow window(hidden);
	if(!hidden || !QSystemTrayIcon::isSystemTrayAvailable())
		window.show();

	return app.exec();
}
