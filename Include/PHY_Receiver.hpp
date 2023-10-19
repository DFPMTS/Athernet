#pragma once
#include "Config.hpp"
#include "PHY_FrameExtractor.hpp"
#include "RingBuffer.hpp"
#include <atomic>
#include <thread>
#include <vector>

namespace Athernet {

template <typename T> class PHY_Receiver {
public:
	PHY_Receiver()
		: config { Athernet::Config::get_instance() }
		, frame_extractor(m_recv_buffer)
	{
	}

	void push_stream(const float* buffer, int count)
	{
		bool result = true;
		for (int i = 0; i < count; ++i) {
			if constexpr (std::is_floating_point<T>::value) {
				result = result && m_recv_buffer.push(buffer[i]);
			} else {
				result = result
					&& m_recv_buffer.push(static_cast<T>(buffer[i] * Athernet::RECV_FLOAT_INT_SCALE));
			}
		}
		assert(result);
	}

	std::vector<std::vector<T>> pop_frame() { }

private:
	Athernet::Config& config;
	Athernet::RingBuffer<T> m_recv_buffer;
	FrameExtractor<T> frame_extractor;
};
}