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
		if (m_head + x >= m_capacity) {
			return m_head + x - m_capacity;
		} else {
			return m_head + x;
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

		// * [msvc-only] "c" mode option for WRITE THROUGH
		/*	Microsoft C/C++ version 7.0 introduces the "c" mode option for the fopen()
			function. When an application opens a file and specifies the "c" mode, the
			run-time library writes the contents of the file buffer to disk when the
			application calls the fflush() or _flushall() function. The "c" mode option is a
			Microsoft extension and is not part of the ANSI standard for fopen().
			* ----https://jeffpar.github.io/kbarchive/kb/066/Q66052/
		*/
		// if constexpr (Athernet::DUMP_RECEIVED) {
		receive_fd = fopen((NOTEBOOK_DIR + "received.txt"s).c_str(), "wc");
		if (!receive_fd) {
			std::cerr << "Unable to open received.txt!\n";
			assert(0);
		}
		// }
	}
	~RingBuffer()
	{
		// if constexpr (Athernet::DUMP_RECEIVED) {
		fclose(receive_fd);
		// }
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

		for (int i = 0; i < popped_count; ++i) {
			if constexpr (std::is_floating_point<T>::value) {
				dest[i] = m_data[m_head];
			} else {
				dest[i] = static_cast<float>(m_data[m_head]) / Athernet::SEND_FLOAT_INT_SCALE;
			}

			increment(m_head);
		}

		m_size.store(size_value - popped_count, std::memory_order_relaxed);

		return popped_count;
	}

	int discard(int count)
	{
		if (!count)
			return 0;
		int size_value = m_size.load(std::memory_order_acquire);
		int discard_count = size_value < count ? size_value : count;

		// if constexpr (Athernet::DUMP_RECEIVED) {
		// for (int i = 0; i < discard_count; ++i) {
		// 	if constexpr (std::is_floating_point<T>::value)
		// 		fprintf(receive_fd, "%f ", (*this)[i]);
		// 	else
		// 		fprintf(receive_fd, "%f ", ((double)(*this)[i]) / Athernet::RECV_FLOAT_INT_SCALE);
		// }
		// fflush(receive_fd);
		// }

		increment_by(m_head, discard_count);

		m_size.store(size_value - discard_count, std::memory_order_relaxed);

		return discard_count;
	}

	T& operator[](int x)
	{
		// no run time check
		return m_data[head_add_offset(x)];
	}

	int size() const { return m_size.load(std::memory_order_acquire); }

	int capacity() const { return m_capacity; }

	int show_head() const { return m_head; }

private:
	FILE* receive_fd = nullptr;
	int m_capacity;
	std::atomic<int> m_size;
	int m_head;
	int m_tail;
	std::vector<T> m_data;
};
}
