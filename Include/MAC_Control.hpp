#pragma once

#include <mutex>

namespace Athernet {
struct MAC_Control {
	std::atomic_bool collision = false;
	std::atomic_bool busy = false;
	std::atomic_int previlege_node = -1;
	std::atomic_int previlege_duration = 0;
};
}
