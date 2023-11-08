#pragma once

#include "Config.hpp"
#include <vector>

namespace Athernet {

using Frame = std::vector<int>;

struct MacFrame {

	MacFrame()
		: from { -1 }
		, to { -1 }
		, seq { -1 }
		, ack { -1 }
		, bad_data { -1 }
	{
	}

	MacFrame(const Frame& frame, int is_bad_data)
		: from { 0 }
		, to { 0 }
		, seq { 0 }
		, ack { 0 }
		, bad_data { is_bad_data }
	{
		assert(frame.size() >= 32);

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

		has_ack = frame[24];
		is_ack = frame[25];
		if (!bad_data) {
			for (int i = 32 + 8; i < frame.size(); ++i) {
				data.push_back(frame[i]);
			}
		}
	}
	int from;
	int to;
	int seq;
	int ack;
	int is_ack;
	int has_ack;
	int bad_data;
	std::vector<int> data;
};

}