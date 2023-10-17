#pragma once
#include "Config.hpp"
#include "RingBuffer.hpp"
#include <boost/>
#include <vector>

namespace Athernet {

template <typename T> class PHY_Receiver {
	using Signal = std::vector<T>;

	PHY_Receiver()
		: config { Athernet::Config::get_instance() }
	{
	}

	void push_stream(float* buffer, int count)
	{
		bool result = true;
		for (int i = 0; i < count; ++i) {
			if constexpr (std::is_floating_point<T>::value) {
				m_recv_buffer.push(buffer[i]);
			} else {
				result = result and m_recv_buffer.push(static_cast<T>(buffer[i] * Athernet::FLOAT_INT_SCALE));
			}
		}
		assert(result);
	}

	std::vector<std::vector<T>> pop_frame()
	{
		if (state == PhyRecvState::WAIT_HEADER) {
			for (int i = 0; i < m_recv_buffer.size() - config.get_preamble_length(); ++i) { }
		} else if (state == PhyRecvState::GET_DATA) {
		}
	}

private:
	enum class PhyRecvState { WAIT_HEADER, GET_DATA };

	class FrameExtractor {
		FrameExtractor() {};
		~FrameExtractor() {};
	};

	Athernet::Config& config;
	Athernet::RingBuffer<T> m_recv_buffer;
	FrameExtractor frame_extractor;
	PhyRecvState state = PhyRecvState::WAIT_HEADER;
};

}