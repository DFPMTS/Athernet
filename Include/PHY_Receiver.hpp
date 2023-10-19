#pragma once
#include "Config.hpp"
#include "PHY_FrameExtractor.hpp"
#include "RingBuffer.hpp"
#include "SyncQueue.hpp"
#include <atomic>
#include <thread>
#include <vector>

namespace Athernet {

template <typename T> class PHY_Receiver {
	using Frame = std::vector<int>;

public:
	PHY_Receiver()
		: config { Athernet::Config::get_instance() }
		, frame_extractor(m_recv_buffer, m_recv_queue)
	{
		running.store(true);
		worker = std::thread(&PHY_Receiver::display_frame, this);
	}

	~PHY_Receiver()
	{
		running.store(false);
		worker.join();
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
		// ! May change to busy waiting
		assert(result);
	}

	std::vector<Frame> pop_frame() { }

	void display_frame()
	{
		Frame frame;
		while (running.load()) {
			if (!m_recv_queue.pop(frame)) {
				std::this_thread::yield();
				continue;
			}
			if (frame.size() % 8 == 0) {
				std::cerr << "------------------------------------------------------------\n";
				std::cerr << "Interpreting as ASCII characters......\n";
				for (int i = 0; i < frame.size(); i += 8) {
					char c = 0;
					for (int j = 0; j < 8; ++j) {
						c += (frame[i + j] << j);
					}
					putchar(c);
				}
				std::cerr << "\n";
				std::cerr << "------------------------------------------------------------\n";
			} else {
				std::cerr << "Output as raw binary string:\n";
				for (const auto& x : frame) {
					std::cerr << x;
				}
				std::cerr << "\n";
			}
		}
	}

private:
	Athernet::Config& config;
	Athernet::RingBuffer<T> m_recv_buffer;
	Athernet::FrameExtractor<T> frame_extractor;
	Athernet::SyncQueue<Frame> m_recv_queue;

	std::atomic_bool running;
	std::thread worker;
};
}