#pragma once

#include <vector>

namespace Athernet {

struct PHY_Unit {
	PHY_Unit(std::vector<float>&& vec, int seq_num)
		: signal { std::move(vec) }
		, length { static_cast<int>(signal.size()) }
		, seq { seq_num }
	{
	}

	std::vector<float> signal;
	int length;
	int seq;
};

}