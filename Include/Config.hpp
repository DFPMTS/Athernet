#pragma once

#include "Logger.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <ratio>
#include <vector>

using namespace std::string_literals;

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
		// PI
		auto PI = acos(-1);

		// * Bit Rate
		bit_rate = 3000;

		// * Sample Rate
		sample_rate = 48'000;
		// Preamble
		{
			// * Preamble (chirp) Parameters
			preamble_f1 = 3'000;
			preamble_f2 = 15'000;
			preamble_length = 64;
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
				preamble.push_back(static_cast<float>(sin(2 * PI
					* ((preamble_f1 - preamble_f2) * (t * t / T) + (2 * preamble_f2 - preamble_f1) * t
						+ (preamble_f1 - preamble_f2) * T / 2))));
			}

			preamble_energy = 0;
			for (const auto& x : preamble) {
				preamble_energy += x * x;
			}

			for (const auto& x : preamble) {
				preamble_int.push_back(static_cast<int>(x * SEND_FLOAT_INT_SCALE));
			}

			preamble_int_energy = 0;
			for (const auto& x : preamble_int) {
				preamble_int_energy += x * x;
			}
		}

		// Carrier Wave
		{
			int samples_per_bit = sample_rate / bit_rate;
			std::vector<int> carrier_frequencies = { 3000, 6000, 9000, 12000, 15000 };

			for (auto carrier_f : carrier_frequencies) {
				std::vector<float> carrier_0, carrier_1;
				for (int i = 0; i < samples_per_bit; ++i) {
					carrier_0.push_back(static_cast<float>(cos(2 * PI * carrier_f * i / sample_rate)));
					carrier_1.push_back(static_cast<float>(-cos(2 * PI * carrier_f * i / sample_rate)));
				}
				carriers.push_back({ carrier_0, carrier_1 });

				std::vector<int> carrier_0_int, carrier_1_int;
				for (int i = 0; i < samples_per_bit; ++i) {
					carrier_0_int.push_back(static_cast<int>(SEND_FLOAT_INT_SCALE * carrier_0[i]));
					carrier_1_int.push_back(static_cast<int>(SEND_FLOAT_INT_SCALE * carrier_1[i]));
				}
				carriers_int.push_back({ carrier_0_int, carrier_1_int });
			}

			phy_frame_CP_length = samples_per_bit >> 2;
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

		{
			std::ifstream fin(NOTEBOOK_DIR + "ip_addr.txt"s);
			if (!fin) {
				std::cerr << "Fail to read ip_addr.txt!\n";
				assert(0);
			}
			fin >> ip_address;
		}

		ip_to_mac["172.18.0.1"] = 0;
		ip_to_mac["172.18.0.2"] = 1;
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
	const std::vector<int>& get_preamble(Tag<int>) const { return preamble_int; }
	float get_preamble_energy(Tag<float>) const { return preamble_energy; }
	int get_preamble_energy(Tag<int>) const { return preamble_int_energy; }

	const Carriers& get_carriers(Tag<float>) const { return carriers; }
	const CarriersInt& get_carriers(Tag<int>) const { return carriers_int; }

	int get_num_carriers() const { return carriers.size(); }

	int get_symbol_length() const { return static_cast<int>(carriers[0][0].size()); }

	int get_phy_frame_CP_length() const { return phy_frame_CP_length; }

	const std::vector<int>& get_crc() const { return crc; }

	void set_self_id(int addr) { mac_address = addr; }
	int get_self_id() { return mac_address; }

	void set_self_ip(std::string ip) { ip_address = ip; }
	std::string get_self_ip() { return ip_address; }

	std::string get_mac_by_id(int id)
	{
		assert(id >= 0 && id < 16);
		return "02:00:00:00:00:0"s + std::format("{:x}", id);
	}

	int get_mac_by_ip(std::string ip)
	{
		if (ip_to_mac.count(ip)) {
			return ip_to_mac[ip];
		} else {
			return default_gateway;
		}
	}

	// float get_collision_threshold() const { return 0.0002f; }
	float get_collision_threshold() const { return 0.0005; }

	int get_window_size() const { return 3; }

	int get_seq_bits_length() const { return 8; }

	int get_seq_limit() const { return 1 << get_seq_bits_length(); }

	int get_map_4b_5b(int x)
	{
		assert(x >= 0 && x < 16);
		return map_4b_5b[x];
	}

	int get_map_5b_4b(int x)
	{
		assert(x >= 0 && x < 32);
		return map_5b_4b[x];
	}

	void timer_set() { start = std::chrono::system_clock::now(); }

	void timer_get()
	{
		auto time_used = std::chrono::duration_cast<std::chrono::duration<double>>(
			std::chrono::system_clock::now() - start);
		this->log(std::format("Time Used:  {}s", time_used.count()));
		std::cerr << std::format("Time Used:  {}s", time_used.count()) << "\n";
	}

	void log(std::string item) { logger.append_log(std::move(item)); }

private:
	int bit_rate;

	int sample_rate;

	int preamble_length;
	int preamble_f1;
	int preamble_f2;
	std::vector<float> preamble;
	float preamble_energy;
	std::vector<int> preamble_int;
	int preamble_int_energy;

	int phy_frame_CP_length;

	int phy_frame_payload_symbol_limit = 4095;
	int phy_frame_length_num_bits = 12;

	// 7 for windows start position, 19 for window itself
	int phy_coding_overhead = 7 + 19;

	int mac_address = -1;
	std::string ip_address = "";
	int default_gateway = 0;
	std::map<std::string, int> ip_to_mac;

	int map_4b_5b[16] = { 30, 9, 20, 21, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 28, 29 };
	int map_5b_4b[32] = {
		/* 00000 Q */ -1,
		/* 00001 ? */ -1,
		/* 00010 ? */ -1,
		/* 00011 ? */ -1,
		/* 00100 H */ -1,
		/* 00101 ? */ -1,
		/* 00110 L */ -1,
		/* 00111 R */ -1,
		/* 01000 ? */ -1,
		/* 01001 1 */ 1,
		/* 01010 4 */ 4,
		/* 01011 5 */ 5,
		/* 01100 ? */ -1,
		/* 01101 T */ -1,
		/* 01110 6 */ 6,
		/* 01111 7 */ 7,
		/* 10000 ? */ -1,
		/* 10001 K */ -1,
		/* 10010 8 */ 8,
		/* 10011 9 */ 9,
		/* 10100 2 */ 2,
		/* 10101 3 */ 3,
		/* 10110 A */ 10,
		/* 10111 B */ 11,
		/* 11000 J */ -1,
		/* 11001 S */ -1,
		/* 11010 C */ 12,
		/* 11011 D */ 13,
		/* 11100 E */ 14,
		/* 11101 F */ 15,
		/* 11110 0 */ 0,
		/* 11111 I */ -1
	};

	std::vector<std::vector<std::vector<float>>> carriers;
	std::vector<std::vector<std::vector<int>>> carriers_int;

	// ! REVERSED for simplicity
	std::vector<int> crc = { 1, 1, 1, 0, 1, 0, 1, 0, 1 }; // CRC8

	int physical_buffer_size = 200'0000;

	std::chrono::system_clock::time_point start;

	Logger logger;
};

}