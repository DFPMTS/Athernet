#pragma once

#include "JuceHeader.h"
#include "MAC_Control.hpp"
#include "PHY_Layer.hpp"
#include <atomic>

namespace Athernet {

class MAC_Layer {
public:
	MAC_Layer()
		: phy_layer(control)
	{
		running.store(true);
		worker = std::thread(&MAC_Layer::work, this);
	}

	void work()
	{
		// while (running.load()) {
		// if (stall.load()) {
		// 	stall.store(false);
		// 	std::cerr << "--------------Collision--------------\n";
		// }
		// }
	}

	~MAC_Layer()
	{
		running.store(false);
		worker.join();
	}

	MAC_Control control;

	PHY_Layer<float> phy_layer;

	std::atomic_bool running;
	std::thread worker;
};
}