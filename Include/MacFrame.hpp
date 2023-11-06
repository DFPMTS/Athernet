#pragma once

#include "Config.hpp"
#include <vector>

namespace Athernet {

using Frame = std::vector<int>;

struct MacFrame {

	MacFrame(const Frame& frame)
		: from { 0 }
		, to { 0 }
		, seq { 0 }
		, ack { 0 }
	{
		assert(frame.size() >= 24);

		for (int i = 0; i < 4; ++i) {
			to += frame[i] << i;
		}

		for (int i = 0; i < 4; ++i) {
			from += frame[i + 4] << i;
		}

		for (int i = 0; i < 8; ++i) {
			seq += frame[i + 8] << i;
		}
		for (int i = 0; i < 8; ++i) {
			ack += frame[i + 16] << i;
		}
		is_ack = frame[24];
		std::copy(std::begin(frame) + 32, std::end(frame), std::back_inserter(data));
	}
	int from;
	int to;
	int seq;
	int ack;
	int is_ack;
	std::vector<int> data;
};

}
