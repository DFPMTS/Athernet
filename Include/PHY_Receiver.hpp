#pragma once
#include "Config.hpp"
#include "LT_Decode.hpp"
#include "PHY_FrameExtractor.hpp"
#include "RingBuffer.hpp"
#include "SyncQueue.hpp"
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

namespace Athernet {

template <typename T> class PHY_Receiver {
	using Frame = std::vector<int>;

public:
	PHY_Receiver()
		: config { Athernet::Config::get_instance() }
		, frame_extractor(m_recv_buffer, m_recv_queue, m_decoder_queue)
		, decoder(m_decoder_queue, m_recv_queue)
	{
		display_running.store(true);
		display_worker = std::thread(&PHY_Receiver::display_frame, this);
	}

	~PHY_Receiver()
	{
		display_running.store(false);
		display_worker.join();
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
		int file_id = 0;
		while (display_running.load()) {
			if (!m_recv_queue.pop(frame)) {
				std::this_thread::yield();
				continue;
			}
			if (frame.size() > 1000) {
				int file_len = 0;
				for (int i = 0; i < 16; ++i) {
					file_len += (frame[i] << i);
				}

				++file_id;
				std::string file_name = NOTEBOOK_DIR + std::to_string(file_id) + ".txt";
				auto fd = fopen(file_name.c_str(), "wc");
				int data_start = 16;
				for (int i = data_start; i < data_start + file_len; ++i) {
					fprintf(fd, "%d", frame[i]);
				}
				fflush(fd);
				fclose(fd);
			} else if (frame.size() % 8 == 0) {
				std::cerr << "------------------------------------------------------------\n";
				std::cerr << "Interpreting as ASCII characters......\n";
				for (int i = 0; i < frame.size(); i += 8) {
					char c = 0;
					for (int j = 0; j < 8; ++j) {
						c += (char)((int)frame[i + j] << j);
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
	Athernet::SyncQueue<Frame> m_decoder_queue;

	LT_Decode decoder;

	std::atomic_bool display_running;
	std::thread display_worker;
};
}