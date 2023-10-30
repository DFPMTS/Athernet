#pragma once

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

#define WIN

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
constexpr int RECV_FLOAT_INT_SCALE = 1000;

// * If dump received
constexpr int DUMP_RECEIVED = 1;

// put preambles, ring buffer size ... etc inside.
// Singleton
class Config {
	using CarriersInt = std::vector<std::vector<std::vector<int>>>;

public:
	Config()
	{
		// PI
		auto PI = acos(-1);

		// * Bit Rate
		bit_rate = 2000;

		// * Sample Rate
		sample_rate = 48'000;
		// Preamble
		{
			// * Preamble (chirp) Parameters
			preamble_f1 = 6'000;
			preamble_f2 = 12'000;
			preamble_length = 200;
			// * -------------------

			assert(preamble_length % 2 == 0);

			preamble_int = {
				0,
				709,
				999,
				681,
				-62,
				-773,
				-990,
				-558,
				248,
				892,
				923,
				305,
				-535,
				-992,
				-718,
				98,
				844,
				939,
				294,
				-590,
				-1000,
				-584,
				323,
				961,
				770,
				-98,
				-883,
				-874,
				-62,
				811,
				923,
				152,
				-770,
				-939,
				-171,
				773,
				929,
				121,
				-818,
				-889,
				0,
				892,
				799,
				-191,
				-968,
				-634,
				439,
				999,
				368,
				-709,
				-923,
				3,
				929,
				681,
				-439,
				-995,
				-248,
				829,
				799,
				-312,
				-1000,
				-305,
				818,
				787,
				-368,
				-995,
				-171,
				906,
				637,
				-590,
				-923,
				160,
				998,
				275,
				-883,
				-634,
				637,
				874,
				-323,
				-987,
				0,
				988,
				294,
				-906,
				-535,
				773,
				718,
				-616,
				-844,
				457,
				923,
				-312,
				-968,
				191,
				990,
				-98,
				-998,
				35,
				999,
				-3,
				-1000,
				-3,
				999,
				35,
				-998,
				-98,
				990,
				191,
				-968,
				-312,
				923,
				457,
				-844,
				-616,
				718,
				773,
				-535,
				-906,
				294,
				988,
				0,
				-987,
				-323,
				874,
				637,
				-634,
				-883,
				275,
				998,
				160,
				-923,
				-590,
				637,
				906,
				-171,
				-995,
				-368,
				787,
				818,
				-305,
				-1000,
				-312,
				799,
				829,
				-248,
				-995,
				-439,
				681,
				929,
				3,
				-923,
				-709,
				368,
				999,
				439,
				-634,
				-968,
				-191,
				799,
				892,
				0,
				-889,
				-818,
				121,
				929,
				773,
				-171,
				-939,
				-770,
				152,
				923,
				811,
				-62,
				-874,
				-883,
				-98,
				770,
				961,
				323,
				-584,
				-1000,
				-590,
				294,
				939,
				844,
				98,
				-718,
				-992,
				-535,
				305,
				923,
				892,
				248,
				-558,
				-990,
				-773,
				-62,
				681,
				999,
				709,
			};

			preamble_int_energy = 99877494;
		}

		// Carrier Wave
		{
			int samples_per_bit = 24;

			carriers_int.push_back({ { 1000, 707, 0, -707, -1000, -707, 0, 707, 1000, 707, 0, -707, -1000,
										 -707, 0, 707, 1000, 707, 0, -707, -1000, -707, 0, 707 },
				{ -1000, -707, 0, 707, 1000, 707, 0, -707, -1000, -707, 0, 707, 1000, 707, 0, -707, -1000,
					-707, 0, 707, 1000, 707, 0, -707 } });

			carriers_int.push_back(
				{ { 1000, 500, -500, -1000, -500, 500, 1000, 500, -500, -1000, -500, 500, 1000, 500, -500,
					  -1000, -500, 500, 1000, 500, -500, -1000, -500, 500 },
					{ -1000, -500, 500, 1000, 500, -500, -1000, -500, 500, 1000, 500, -500, -1000, -500, 500,
						1000, 500, -500, -1000, -500, 500, 1000, 500, -500 } });

			phy_frame_CP_length = samples_per_bit >> 2;
		}

		{
			std::ofstream fout(NOTEBOOK_DIR + "preamble.txt"s);
			for (auto x : preamble_int)
				fout << x << ", ";
		}

		{
			std::ofstream fout(NOTEBOOK_DIR + "carrier_0.txt"s);
			for (auto x : carriers_int[0][0])
				fout << x << " ";
			fout << "\n";
			for (auto x : carriers_int[1][0])
				fout << x << " ";
			fout << "\n";
		}
		{
			std::ofstream fout(NOTEBOOK_DIR + "carrier_1.txt"s);
			for (auto x : carriers_int[0][1])
				fout << x << " ";
			fout << "\n";
			for (auto x : carriers_int[1][1])
				fout << x << " ";
			fout << "\n";
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
	const std::vector<int>& get_preamble(Tag<int>) const { return preamble_int; }

	int get_preamble_energy(Tag<int>) const { return preamble_int_energy; }

	const CarriersInt& get_carriers(Tag<int>) const { return carriers_int; }

	int get_num_carriers() const { return carriers_int.size(); }

	int get_symbol_length() const { return static_cast<int>(carriers_int[0][0].size()); }

	int get_phy_frame_CP_length() const { return phy_frame_CP_length; }

	const std::vector<int>& get_crc() const { return crc; }

private:
	int bit_rate;

	int sample_rate;

	int preamble_length;
	int preamble_f1;
	int preamble_f2;

	std::vector<int> preamble_int;
	int preamble_int_energy;

	int phy_frame_CP_length;

	int phy_frame_payload_symbol_limit = 400;
	int phy_frame_length_num_bits = 8;

	// 7 for windows start position, 19 for window itself
	int phy_coding_overhead = 7 + 19;

	std::vector<std::vector<std::vector<int>>> carriers_int;

	// ! REVERSED for simplicity
	std::vector<int> crc = { 1, 1, 1, 0, 1, 0, 1, 0, 1 }; // CRC8

	int physical_buffer_size = 10'0000;
};
}