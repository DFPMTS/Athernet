#pragma once

#include <cmath>
#include <vector>

#define PI acos(-1)

namespace Athernet {
using Signal = std::vector<float>;

Signal chirp(int f1, int f2, int len)
{
	static int sample_rate = 48000;
	Signal signal;
	double T = (double)len / sample_rate;
	for (int i = 0; i < len; ++i) {
		double t = (double)i / sample_rate;
		signal.push_back(sin(2 * PI * ((f2 - f1) * (t * t / T) + f1 * t)));
	}
	return signal;
}

}