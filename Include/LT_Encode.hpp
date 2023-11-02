#pragma once

#include "Config.hpp"
#include "PHY_Layer.hpp"
#include <mutex>
#include <random>
#include <vector>

namespace Athernet {

template <typename T> void LT_Send(PHY_Layer<T>* physical_layer, std::string file)
{
	static int group_flag = 0;
	static std::mutex mutex;

	std::scoped_lock lock { mutex };

	group_flag ^= 1;
	std::vector<int> text;
	int c;

	file = NOTEBOOK_DIR + file;
	auto fd = fopen(file.c_str(), "r");
	if (!fd) {
		std::cerr << "File not found!\n";
		return;
	}
	while ((c = fgetc(fd)) != EOF) {
		text.push_back(int(c - '0'));
	}
	fclose(fd);

	int file_len = (int)text.size();
	std::vector<int> length;
	for (int i = 0; i < 16; ++i) {
		if (file_len & (1 << i)) {
			length.push_back(1);
		} else {
			length.push_back(0);
		}
	}

	int num_packets = 100;
	int packet_len = (int)(text.size() + length.size() - 1) / num_packets + 1;

	std::random_device seeder;
	const auto seed = seeder.entropy() ? seeder() : time(nullptr);
	std::mt19937 engine { static_cast<std::mt19937::result_type>(seed) };
	std::uniform_int_distribution<int> window_start_distribution { 0, num_packets - 1 };
	std::uniform_int_distribution<int> is_one_distribution { 1, 100 };

	std::vector<std::vector<int>> mat;
	std::vector<int> a;

	for (auto x : length) {
		a.push_back(x);
		if (a.size() == packet_len) {
			mat.push_back(std::move(a));
			a.clear();
		}
	}
	for (auto x : text) {
		a.push_back(x);
		if (a.size() == packet_len) {
			mat.push_back(std::move(a));
			a.clear();
		}
	}

	if (a.size()) {
		for (int i = (int)a.size(); i < packet_len; ++i) {
			a.push_back(0);
		}
		mat.push_back(std::move(a));
	}

	double packet_fail_rate = 0.9;
	int packets_to_send = static_cast<int>((num_packets + 25) / packet_fail_rate);

	while (packets_to_send--) {
		std::vector<int> start;
		int start_point = window_start_distribution(engine);
		for (int i = 0; i < 7; ++i) {
			if (start_point & (1 << i)) {
				start.push_back(1);
			} else {
				start.push_back(0);
			}
		}
		std::vector<int> bit_map(19);
		std::vector<int> frame = mat[start_point];
		for (int i = 0; i < 19; ++i) {
			bit_map[i] = (is_one_distribution(engine) < 60) ? 1 : 0;
			if (bit_map[i]) {
				int j = start_point + i + 1;
				if (j >= 100)
					j -= 100;
				for (int k = 0; k < packet_len; ++k) {
					frame[k] ^= mat[j][k];
				}
			}
		}
		std::vector<int> actual_frame;
		for (auto x : start)
			actual_frame.push_back(x);
		for (auto x : bit_map)
			actual_frame.push_back(x);
		for (auto x : frame)
			actual_frame.push_back(x);

		actual_frame.push_back(group_flag);
		actual_frame.push_back(1);

		physical_layer->send_frame(actual_frame);
	}
}

}