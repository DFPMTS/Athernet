#pragma once

#include <mutex>

namespace Athernet {
struct MAC_Control {
	std::atomic_bool collision;
	std::atomic_bool busy;
	std::atomic_int previlege_node;
	std::atomic_int previlege_duration;
};
}
