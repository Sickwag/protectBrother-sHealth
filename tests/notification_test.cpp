#include "desktop_notifier.h"
#include "phrase_catalog.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSet>
#include <QTimer>

struct NotificationSample {
	QString _levelName;
	int		_score = 0;
	QString _phrase;
};

int main(int argc, char* argv[]) {
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("BroHealth notification test"));

	QString configPath;
	if(argc > 1)
		configPath = QString::fromLocal8Bit(argv[1]);

	PhraseCatalog catalog;
	QString		  error;
	if(!catalog.load(configPath, &error)) {
		qCritical().noquote() << PhraseCatalog::missingConfigMessage() << catalog.configPath();
		qCritical().noquote() << error;
		return 2;
	}

	const QList<QPair<QString, int>> levels{
		{QStringLiteral("危急"),	 20},
		{QStringLiteral("严重"),	 10},
		{QStringLiteral("中等"),	 4 },
		{QStringLiteral("轻微"),	 1 },
		{QStringLiteral("无欠账"), 0 },
	};

	QList<NotificationSample> samples;
	for(const auto& level : levels) {
		QSet<QString> seen;
		for(int attempt = 0; attempt < 100 && seen.size() < 3; ++attempt)
			seen.insert(catalog.phraseForScore(level.second));
		if(seen.size() < 3) {
			qCritical().noquote() << QStringLiteral("层级 %1 至少需要 3 条不同文案，当前只抽到 %2 条。")
										 .arg(level.first)
										 .arg(seen.size());
			return 3;
		}
		for(const auto& phrase : seen)
			samples.append({level.first, level.second, phrase});
	}

	DesktopNotifier notifier(nullptr);
	if(!notifier.notificationServiceAvailable()) {
		qCritical().noquote() << QStringLiteral(
			"当前会话没有 org.freedesktop.Notifications 服务，无法发送系统通知。");
		return 4;
	}

	qInfo().noquote() << QStringLiteral("配置文件：%1").arg(catalog.configPath());
	qInfo().noquote() << QStringLiteral("共发送 %1 条通知，间隔 1 秒。").arg(samples.size());

	int	   nextSample = 0;
	QTimer timer;
	timer.setInterval(1000);
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &app, [&] {
		if(nextSample >= samples.size()) {
			timer.stop();
			app.quit();
			return;
		}

		const auto& sample = samples.at(nextSample++);
		const auto	title  = QStringLiteral("兄弟文案测试：%1（第 %2/%3 条）")
							   .arg(sample._levelName)
							   .arg(nextSample)
							   .arg(samples.size());
		const auto body	  = QStringLiteral("债务分数：%1\n%2").arg(sample._score).arg(sample._phrase);
		const auto result = notifier.notify(title, body, {}, 5000);
		if(!result._shown)
			qWarning().noquote() << QStringLiteral("通知发送失败：%1").arg(title);
		else
			qInfo().noquote() << QStringLiteral("[%1] %2").arg(sample._levelName, sample._phrase);

		if(nextSample == samples.size()) {
			timer.stop();
			QTimer::singleShot(1000, &app, &QCoreApplication::quit);
		} else
			timer.start();
	});
	timer.start();
	return app.exec();
}
