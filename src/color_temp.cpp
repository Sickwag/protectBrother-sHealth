#include "color_temp.h"
#include <algorithm>
#include <cmath>

static double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

RGB kelvinToRgb(uint16_t kelvin) {
	kelvin	 = std::clamp<uint16_t>(kelvin, 1000, 7500);
	double t = kelvin / 100.0;

	double r, g, b;
	if(t <= 66.0) {
		r = 255.0;
	} else {
		r = 329.698727446 * std::pow(t - 60.0, -0.1332047592);
	}

	if(t <= 66.0) {
		g = 99.4708025861 * std::log(t) - 161.1195681661;
	} else {
		g = 288.1221695283 * std::pow(t - 60.0, -0.0755148492);
	}

	if(t >= 66.0) {
		b = 255.0;
	} else if(t <= 19.0) {
		b = 0.0;
	} else {
		b = 138.5177312231 * std::log(t - 10.0) - 305.0447927307;
	}

	return {clamp01(r / 255.0), clamp01(g / 255.0), clamp01(b / 255.0)};
}

uint16_t estimateKelvin(double r, double g, double b) {
	// Neutral check: if all channels within 5% of 1.0
	if(std::abs(r - 1.0) < 0.05 && std::abs(g - 1.0) < 0.05 && std::abs(b - 1.0) < 0.05) {
		return 6500;
	}

	// Binary search: blue channel is the most temperature-sensitive.
	// We find the kelvin value whose B-output best matches the observed B.
	uint16_t lo = 1000, hi = 7500;
	while(hi - lo > 50) {
		uint16_t mid = (lo + hi) / 2;
		auto	 rgb = kelvinToRgb(mid);
		if(rgb._b < b)
			lo = mid;
		else
			hi = mid;
	}
	return (lo + hi) / 2;
}
