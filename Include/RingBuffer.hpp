#pragma once

#include "Config.hpp"
#include <atomic>
#include <vector>

namespace Athernet {

// SPSC ring buffer
template <typename T> class RingBuffer {

private:
	void increment(int& x)
	{
		if (++x == m_capacity) {
			x = 0;
		}
	}

	void increment_by(int& x, int offset)
	{
		if ((x += offset) == m_capacity) {
			x -= m_capacity;
		}
	}

	int head_add_offset(int x)
	{
		if (head + x >= m_capacity) {
			return head + x - m_capacity;
		} else {
			return head + x;
		}
	}

public:
	RingBuffer()
		: m_capacity(Athernet::Config::get_instance().get_physical_buffer_size())
		, m_size(0)
		, m_data(m_capacity)
		, m_head(0)
		, m_tail(0)

	{
	}

	bool push(const std::vector<T>& vec)
	{
		int vec_size = static_cast<int>(vec.size());
		int size_value = m_size.load(std::memory_order_relaxed);

		if (vec_size > capacity() - size_value) {
			return false;
		}

		for (int i = 0; i < vec_size; ++i) {
			m_data[m_tail] = vec[i];
			increment(m_tail);
		}

		m_size.store(size_value + vec_size, std::memory_order_release);
		return true;
	}

	bool push(const T& val)
	{
		int size_value = m_size.load(std::memory_order_relaxed);

		if (size_value == capacity()) {
			return false;
		}

		m_data[m_tail] = val;
		increment(m_tail);

		m_size.store(size_value + 1, std::memory_order_release);
		return true;
	}

	int pop(T* dest, int count)
	{
		int size_value = m_size.load(std::memory_order_acquire);
		int popped_count = size_value < count ? size_value : count;

		for (int i = 0; i < popped_count; ++i) {
			dest[i] = m_data[m_head];
			increment(m_head);
		}

		m_size.store(size_value - popped_count, std::memory_order_relaxed);

		return popped_count;
	}

	int pop_with_conversion_to_float(float* dest, int count)
	{
		int size_value = m_size.load(std::memory_order_acquire);
		int popped_count = size_value < count ? size_value : count;
		int SCALE = 32768;
		for (int i = 0; i < popped_count; ++i) {
			dest[i] = static_cast<float>(m_data[m_head]) / SCALE;
			increment(m_head);
		}

		m_size.store(size_value - popped_count, std::memory_order_relaxed);

		return popped_count;
	}

	void discard(int count)
	{
		int size_value = m_size.load(std::memory_order_acquire);
		int discard_count = size_value < count ? size_value : count;
		increment_by(m_head, discard_count);
		m_size.store(size_value - discard_count, std::memory_order_relaxed);
	}

	T& operator[](int x)
	{
		// no run time check
		return m_data[head_add_offset(x)];
	}

	int peek_size() const { return m_size.load(std::memory_order_acquire); }

	int capacity() const { return m_capacity; }

private:
	int m_capacity;
	std::atomic<int> m_size;
	int m_head;
	int m_tail;
	std::vector<T> m_data;
};

}
