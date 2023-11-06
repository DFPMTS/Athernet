#pragma once

#include <mutex>
#include <queue>

using namespace std::chrono_literals;

namespace Athernet {
template <typename T> class SyncQueue {
public:
	void push(const T& item)
	{
		std::scoped_lock lock { mutex };
		m_queue.push(item);
		consumer.notify_one();
	}

	void push(T&& item)
	{
		std::scoped_lock lock { mutex };
		m_queue.push(std::move(item));
		consumer.notify_one();
	}

	bool pop(T& item)
	{
		std::unique_lock lock { mutex };
		// consumer.wait_for(lock, 100ms, [&]() { return !m_queue.empty(); });
		if (!m_queue.empty()) {
			item = std::move(m_queue.front());
			m_queue.pop();
			return true;
		} else {
			return false;
		}
	}

	bool try_pop(T& item)
	{
		if (!m_queue.empty()) {
			item = std::move(m_queue.front());
			m_queue.pop();
			return true;
		} else {
			return false;
		}
	}

	// private:
	std::queue<T> m_queue;
	std::condition_variable consumer;
	std::mutex mutex;
};
}