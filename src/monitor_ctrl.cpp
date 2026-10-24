#include "monitor_ctrl.h"

#include "color_temp.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>

namespace {
QProcess* waylandRedshiftProcess = nullptr;

bool runProcess(const QString& program, const QStringList& arguments, QString* error) {
	QProcess process;
	process.start(program, arguments);
	if(!process.waitForStarted(3000)) {
		if(error)
			*error = process.errorString();
		return false;
	}
	if(!process.waitForFinished(5000)) {
		process.kill();
		process.waitForFinished();
		if(error)
			*error = QStringLiteral("命令执行超时：%1").arg(program);
		return false;
	}
	if(process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		if(error) {
			const auto details = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
			*error			   = details.isEmpty()
									 ? QStringLiteral("%1 返回错误码 %2").arg(program).arg(process.exitCode())
									 : details;
		}
		return false;
	}
	return true;
}

bool startWaylandAdjustment(const QString& program,
							int			   temperatureKelvin,
							double		   brightness,
							QString*	   error) {
	if(waylandRedshiftProcess) {
		if(waylandRedshiftProcess->state() != QProcess::NotRunning) {
			waylandRedshiftProcess->terminate();
			if(!waylandRedshiftProcess->waitForFinished(500)) {
				waylandRedshiftProcess->kill();
				waylandRedshiftProcess->waitForFinished();
			}
		}
		delete waylandRedshiftProcess;
		waylandRedshiftProcess = nullptr;
	}

	auto* application = QCoreApplication::instance();
	if(!application) {
		if(error)
			*error = QStringLiteral("Wayland 色温调整需要已启动的 Qt 应用程序");
		return false;
	}

	const auto temperature = QString::number(temperatureKelvin);
	const auto level	   = QString::number(brightness, 'f', 2);
	waylandRedshiftProcess = new QProcess(application);
	waylandRedshiftProcess->setProcessChannelMode(QProcess::MergedChannels);
	waylandRedshiftProcess->start(program,
								  {QStringLiteral("-m"),
								   QStringLiteral("wayland"),
								   QStringLiteral("-l"),
								   QStringLiteral("0:0"),
								   QStringLiteral("-t"),
								   temperature + QLatin1Char(':') + temperature,
								   QStringLiteral("-b"),
								   level + QLatin1Char(':') + level,
								   QStringLiteral("-r")});

	if(!waylandRedshiftProcess->waitForStarted(3000)) {
		if(error)
			*error = waylandRedshiftProcess->errorString();
		delete waylandRedshiftProcess;
		waylandRedshiftProcess = nullptr;
		return false;
	}

	if(waylandRedshiftProcess->waitForFinished(250)) {
		if(error) {
			const auto details = QString::fromLocal8Bit(waylandRedshiftProcess->readAll()).trimmed();
			*error = details.isEmpty() ? QStringLiteral("Wayland 色温控制进程提前退出") : details;
		}
		delete waylandRedshiftProcess;
		waylandRedshiftProcess = nullptr;
		return false;
	}
	return true;
}
}  // namespace

bool MonitorController::isWaylandSession() {
	const auto environment = QProcessEnvironment::systemEnvironment();
	const auto sessionType = environment.value(QStringLiteral("XDG_SESSION_TYPE")).toLower();
	if(sessionType == QStringLiteral("wayland"))
		return true;
	if(sessionType == QStringLiteral("x11"))
		return false;
	if(!environment.value(QStringLiteral("WAYLAND_DISPLAY")).isEmpty())
		return true;
	return QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive);
}

QList<MonitorInfo> MonitorController::discover() {
	if(isWaylandSession()) {
		if(QGuiApplication::screens().isEmpty())
			return {};

		MonitorInfo global;
		global._name = QStringLiteral("wayland-global");
		return {global};
	}

	const auto xrandr = QStandardPaths::findExecutable(QStringLiteral("xrandr"));
	if(xrandr.isEmpty())
		return {};

	QProcess process;
	process.start(xrandr, {QStringLiteral("--verbose"), QStringLiteral("--query")});
	if(!process.waitForStarted(3000) || !process.waitForFinished(3000))
		return {};
	if(process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
		return {};

	const auto lines =
		QString::fromLocal8Bit(process.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);

	QList<MonitorInfo>		 monitors;
	MonitorInfo				 current;
	bool					 inMonitor = false;
	const QRegularExpression connectedRe(QStringLiteral(R"(^(\S+)\s+connected(?:\s|$))"));
	const QRegularExpression gammaRe(QStringLiteral(R"(^\s*Gamma:\s+(\S+))"));
	const QRegularExpression brightnessRe(QStringLiteral(R"(^\s*Brightness:\s+(\S+))"));

	for(const auto& line : lines) {
		const auto connectedMatch = connectedRe.match(line);
		if(connectedMatch.hasMatch()) {
			if(inMonitor)
				monitors.append(current);

			current		  = MonitorInfo{};
			current._name = connectedMatch.captured(1);
			inMonitor	  = true;
			continue;
		}

		if(!inMonitor)
			continue;

		const auto gammaMatch = gammaRe.match(line);
		if(gammaMatch.hasMatch()) {
			const auto channels = gammaMatch.captured(1).split(':');
			if(channels.size() == 3) {
				current._gammaR = channels[0].toDouble();
				current._gammaG = channels[1].toDouble();
				current._gammaB = channels[2].toDouble();
			}
			continue;
		}

		const auto brightnessMatch = brightnessRe.match(line);
		if(brightnessMatch.hasMatch())
			current._brightness = brightnessMatch.captured(1).toDouble();
	}

	if(inMonitor)
		monitors.append(current);
	return monitors;
}

bool MonitorController::apply(const QString& output,
							  double		 brightness,
							  int			 temperatureKelvin,
							  QString*		 error) {
	brightness			  = qBound(0.1, brightness, 2.0);
	const int temperature = qBound(1000, temperatureKelvin, 7500);

	if(isWaylandSession()) {
		const auto redshift	 = QStandardPaths::findExecutable(QStringLiteral("redshift"));
		const auto gammastep = QStandardPaths::findExecutable(QStringLiteral("gammastep"));
		if(redshift.isEmpty() && gammastep.isEmpty()) {
			if(error)
				*error = QStringLiteral("Wayland 调色温需要 redshift 或 gammastep");
			return false;
		}

		QString redshiftError;
		if(!redshift.isEmpty() && startWaylandAdjustment(redshift, temperature, brightness, &redshiftError))
			return true;

		if(!gammastep.isEmpty()) {
			QString gammastepError;
			if(startWaylandAdjustment(gammastep, temperature, brightness, &gammastepError))
				return true;
			if(error) {
				*error =
					redshiftError.isEmpty()
						? gammastepError
						: QStringLiteral("Redshift: %1；gammastep: %2").arg(redshiftError, gammastepError);
			}
			return false;
		}

		if(error)
			*error = redshiftError;
		return false;
	}

	const auto xrandr = QStandardPaths::findExecutable(QStringLiteral("xrandr"));
	if(xrandr.isEmpty()) {
		if(error)
			*error = QStringLiteral("未找到 xrandr，无法调整 X11 显示输出");
		return false;
	}

	const auto rgb =
		temperature == 6500 ? RGB{1.0, 1.0, 1.0} : kelvinToRgb(static_cast<uint16_t>(temperature));
	const auto gamma =
		QStringLiteral("%1:%2:%3").arg(rgb._r, 0, 'f', 3).arg(rgb._g, 0, 'f', 3).arg(rgb._b, 0, 'f', 3);
	return runProcess(xrandr,
					  {QStringLiteral("--output"),
					   output,
					   QStringLiteral("--brightness"),
					   QString::number(brightness, 'f', 2),
					   QStringLiteral("--gamma"),
					   gamma},
					  error);
}