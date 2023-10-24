#pragma once

#include "Chirps.hpp"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

#define PI acos(-1)

// #define WIN

using namespace std::string_literals;

#ifndef WIN
#define NOTEBOOK_DIR "/Users/dfpmts/Desktop/JUCE_Demos/NewProject/Extras/"s
#else
#define NOTEBOOK_DIR "D:/fa23/Athernet/Extras/"s
#endif

namespace Athernet {

// * For tag dispatch
template <typename T> struct Tag { };

// * the scale of pre-calculated carrier & preamble
constexpr int SEND_FLOAT_INT_SCALE = 1000;

// * Scale from floating point [-1.0, 1.0] to int
constexpr int RECV_FLOAT_INT_SCALE = 1'000;

// * If dump received
constexpr int DUMP_RECEIVED = 1;

// put preambles, ring buffer size ... etc inside.
// Singleton
class Config {
	using Carriers = std::vector<std::vector<std::vector<float>>>;
	using CarriersInt = std::vector<std::vector<std::vector<int>>>;

public:
	Config()
	{
		// * Bit Rate
		bit_rate = 2000;

		// * Sample Rate
		sample_rate = 48'000;
		// Preamble
		{
			// * Preamble (chirp) Parameters
			preamble_f1 = 6'000;
			preamble_f2 = 22'000;
			preamble_length = 1000;
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

			preamble_energy = 0;
			for (const auto& x : preamble) {
				preamble_energy += x * x;
			}
		}

		// Carrier Wave
		{
			int samples_per_bit = sample_rate / bit_rate;
			int min_f = 6000, max_f = 22000;
			// int min_f = 6000, max_f = 8400;
			int band_width = 400;
			for (int start_f = min_f, end_f = min_f + band_width; end_f <= max_f;
				 start_f += band_width, end_f += band_width) {
				carriers.push_back(
					{ chirp(start_f, (start_f + end_f) / 2, 500), chirp((start_f + end_f) / 2, end_f, 500) });
			}
		}

		{
			std::ofstream fout(NOTEBOOK_DIR + "preamble.txt"s);
			for (auto x : preamble)
				fout << x << " ";
		}

		{
			std::ofstream fout(NOTEBOOK_DIR + "carrier_0.txt"s);
			for (auto x : carriers[0][0])
				fout << x << " ";
		}
		{
			std::ofstream fout(NOTEBOOK_DIR + "carrier_1.txt"s);
			for (auto x : carriers[0][1])
				fout << x << " ";
		}
		{
			std::ofstream fout(NOTEBOOK_DIR + "packet_length.txt"s);
			fout << phy_frame_payload_symbol_limit;
		}
	}

	// disable copy constructor / copy assignment operator
	Config(const Config&) = delete;
	Config(Config&&) = delete;
	Config& operator=(const Config&) = delete;
	Config& operator=(Config&&) = delete;

	~Config() = default;

	// Singleton
	static Config& get_instance()
	{
		static Config instance;
		return instance;
	}

	// get buffer size for physical layer
	int get_physical_buffer_size() const { return physical_buffer_size; }

	int get_phy_frame_payload_symbol_limit() const { return phy_frame_payload_symbol_limit; }

	int get_phy_frame_length_num_bits() const { return phy_frame_length_num_bits; }

	int get_preamble_length() const { return preamble_length; }

	int get_crc_length() const { return static_cast<int>(crc.size()); }
	int get_crc_residual_length() const { return static_cast<int>(crc.size()) - 1; }

	int get_phy_coding_overhead() const { return phy_coding_overhead; }

	// * Tag dispatch
	const std::vector<float>& get_preamble(Tag<float>) const { return preamble; }

	double get_preamble_energy(Tag<float>) const { return preamble_energy; }

	const Carriers& get_carriers(Tag<float>) const { return carriers; }

	int get_num_carriers() const { return static_cast<int>(carriers.size()); }

	int get_symbol_length() const { return static_cast<int>(carriers[0][0].size()); }

	int get_silence_length() const { return silence_length; }

	const std::vector<int>& get_crc() const { return crc; }

private:
	int bit_rate;

	int sample_rate;

	int preamble_length;
	int preamble_f1;
	int preamble_f2;
	std::vector<float> preamble;
	double preamble_energy;

	int phy_frame_payload_symbol_limit = 400;
	int phy_frame_length_num_bits = 8;

	// 7 for windows start position, 19 for window itself
	int phy_coding_overhead = 7 + 19;

	int silence_length = 100;

	std::vector<std::vector<std::vector<float>>> carriers;

	// ! REVERSED for simplicity
	std::vector<int> crc = { 1, 1, 1, 0, 1, 0, 1, 0, 1 }; // CRC8

	int physical_buffer_size = 200'0000;
};

}