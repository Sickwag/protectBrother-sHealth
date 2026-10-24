#pragma once
#include <QList>
#include <QString>

struct MonitorInfo {
	QString _name;
	double	_brightness = 1.0;	// 0.1 - 1.0
	double	_gammaR		= 1.0;
	double	_gammaG		= 1.0;
	double	_gammaB		= 1.0;
};

class MonitorController {
  public:
	static bool isWaylandSession();

	// Query the current display server and return its controllable outputs.
	static QList<MonitorInfo> discover();

	// X11 changes one xrandr output; Wayland uses Redshift's compositor backend.
	static bool
	apply(const QString& output, double brightness, int temperatureKelvin, QString* error = nullptr);
};
