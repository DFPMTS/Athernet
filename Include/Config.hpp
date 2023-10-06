#pragma once

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

// #define WIN

using namespace std::string_literals;

#ifndef WIN
#define NOTEBOOK_DIR "/Users/dfpmts/Desktop/JUCE_Demos/NewProject/Extras/"s
#else
#define NOTEBOOK_DIR "D:/fa23/Athernet/Extras/"s
#endif

namespace Athernet {

// put preambles, ring buffer size ... etc inside.
// Singleton
class Config {
public:
	Config()
	{
		// PI
		auto PI = acos(-1);

		// * Bit Rate
		bit_rate = 4000;

		// * Sample Rate
		sample_rate = 48'000;

		// Preamble
		{
			// * Preamble (chirp) Parameters
			preamble_f1 = 6'000;
			preamble_f2 = 10'000;
			preamble_length = 200;
			// * -------------------

			assert(preamble_length % 2 == 0);

			std::vector<float> linspace;

			// preamble time
			double T = (double)preamble_length / sample_rate;

			// linspace
			for (size_t i = 0; i < preamble_length; ++i) {
				linspace.push_back((float)i / sample_rate);
			}

			// preamble
			for (int i = 0; i < preamble_length / 2; ++i) {
				auto t = linspace[i];
				preamble.push_back(static_cast<float>(
					sin(2 * PI * ((preamble_f2 - preamble_f1) * (t * t / T) + preamble_f1 * t))));
			}
			for (int i = preamble_length / 2; i < preamble_length; ++i) {
				auto t = linspace[i];
				preamble.push_back(static_cast<float>(cos(2 * PI
					* ((preamble_f1 - preamble_f2) * (t * t / T) + (2 * preamble_f2 - preamble_f1) * t
						+ (preamble_f1 - preamble_f2) * T / 2))));
			}
		}

		// Carrier Wave
		{
			carrier_f = 3'000;

			int samples_per_bit = sample_rate / bit_rate;

			for (int i = 0; i < samples_per_bit; ++i) {
				carrier_0.push_back(static_cast<float>(cos(2 * PI * carrier_f * i / sample_rate)));
				carrier_1.push_back(static_cast<float>(-cos(2 * PI * carrier_f * i / sample_rate)));
			}

			std::fstream fout("carriers.txt", std::ios_base::out);
			for (const auto& x : carrier_0)
				fout << x << ", ";
			fout << "\n";
			for (const auto& x : carrier_1)
				fout << x << ", ";
		}

		{
			std::ofstream fout(NOTEBOOK_DIR + "preamble.txt"s);
			for (auto x : preamble)
				fout << x << " ";
		}

		{
			std::ofstream fout(NOTEBOOK_DIR + "carrier_0.txt"s);
			for (auto x : carrier_0)
				fout << x << " ";
		}
		{
			std::ofstream fout(NOTEBOOK_DIR + "carrier_1.txt"s);
			for (auto x : carrier_1)
				fout << x << " ";
		}
	}

	~Config() = default;

	// Singleton
	static Config& get_instance()
	{
		static Config instance;
		return instance;
	}

	// get buffer size for physical layer
	int get_physical_buffer_size() const { return physical_buffer_size; }

	const std::vector<float>& get_preamble() const { return preamble; }

	const std::vector<float>& get_carrier_0() const { return carrier_0; }

	const std::vector<float>& get_carrier_1() const { return carrier_1; }

private:
	int bit_rate;

	int sample_rate;

	int preamble_length;
	int preamble_f1;
	int preamble_f2;
	std::vector<float> preamble;

	int carrier_f;
	std::vector<float> carrier_0;
	std::vector<float> carrier_1;

	int physical_buffer_size = 100'000;
};

}