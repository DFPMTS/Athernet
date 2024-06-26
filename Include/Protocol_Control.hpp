#pragma once

#include <mutex>
#include <vector>

namespace Athernet {
struct Protocol_Control {
	std::atomic_bool collision = false;
	std::atomic_bool busy = false;
	std::atomic_int previlege_node = -1;
	std::atomic_int previlege_duration = 0;
	std::atomic_int ack = -1;
	std::atomic_bool transmission_start = false;
	std::atomic_int clock = 0;
};
}
