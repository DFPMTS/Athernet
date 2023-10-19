#pragma once

#include <queue>

namespace Athernet {
template <typename T> class SyncQueue {
public:
		void push(const T& item)
	{
		std::scoped_lock lock { mutex };
		m_queue.push(item);
	}

	void push(T&& item)
	{
		std::scoped_lock lock { mutex };
		m_queue.push(std::move(item));
	}

	bool pop(T& item)
	{
		std::scoped_lock lock { mutex };
		if (!m_queue.empty()) {
			item = std::move(m_queue.front());
			m_queue.pop();
			return true;
		} else {
			return false;
		}
	}

private:
	std::queue<T> m_queue;
	std::mutex mutex;
};
}