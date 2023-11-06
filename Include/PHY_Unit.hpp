#pragma once

#include <vector>

namespace Athernet {

struct PHY_Unit {
	PHY_Unit(std::vector<int>&& vec, int seq_num)
		: frame { std::move(vec) }
		, seq { seq_num }
	{
	}

	std::vector<int> frame;
	int seq;
};

}