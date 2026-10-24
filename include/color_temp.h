#pragma once
#include <cstdint>

struct RGB {
	double _r, _g, _b;
};

// Convert color temperature (Kelvin, 1000-7500) to
// R:G:B gamma multipliers for xrandr.
// Uses the Tanner Helland algorithm (same family as redshift/f.lux).
RGB kelvinToRgb(uint16_t kelvin);

// Estimate color temperature from gamma R:G:B values.
// Returns 6500 (neutral) if within 5% of 1:1:1.
uint16_t estimateKelvin(double r, double g, double b);
