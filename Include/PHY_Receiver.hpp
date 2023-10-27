#pragma once
#include "Config.hpp"
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
		, frame_extractor(m_Rx1_buffer, m_Rx2_buffer, m_recv_queue, m_decoder_queue)
	{
		display_running.store(true);
		display_worker = std::thread(&PHY_Receiver::display_frame, this);

		decoder_running.store(true);
		decoder_worker = std::thread(&PHY_Receiver::decoder, this);
	}

	~PHY_Receiver()
	{
		display_running.store(false);
		decoder_running.store(false);
		display_worker.join();
		decoder_worker.join();
	}

	void Rx1_push_stream(const float* buffer, int count)
	{
		bool result = true;
		for (int i = 0; i < count; ++i) {
			if constexpr (std::is_floating_point<T>::value) {
				result = result && m_Rx1_buffer.push(buffer[i]);
			} else {
				result
					= result && m_Rx1_buffer.push(static_cast<T>(buffer[i] * Athernet::RECV_FLOAT_INT_SCALE));
			}
		}
		// ! May change to busy waiting
		assert(result);
	}

	void Rx2_push_stream(const float* buffer, int count)
	{
		bool result = true;
		for (int i = 0; i < count; ++i) {
			if constexpr (std::is_floating_point<T>::value) {
				result = result && m_Rx2_buffer.push(buffer[i]);
			} else {
				result
					= result && m_Rx2_buffer.push(static_cast<T>(buffer[i] * Athernet::RECV_FLOAT_INT_SCALE));
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
			std::cerr << frame.size() << "\n";
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

	void decoder()
	{
		Frame frame;
		std::vector<int> G[100];
		std::vector<int> Y[100];
		int num_ones[100];
		int empty_rows = 100;
		int group_flag = -1;
		int got = 0;
		while (decoder_running.load()) {
			if (!m_decoder_queue.pop(frame)) {
				std::this_thread::yield();
				continue;
			}
			if (frame[frame.size() - 1] != group_flag) {
				group_flag = frame[frame.size() - 1];
				for (int i = 0; i < 100; ++i) {
					G[i] = std::vector<int>(100);
					Y[i] = std::vector<int>(frame.size() - 1 - config.get_phy_coding_overhead());
				}
				memset(num_ones, 0, sizeof(num_ones));
				empty_rows = 100;
			}

			if (!empty_rows)
				continue;

			++got;
			frame.pop_back();

			int start_point = 0;
			for (int i = 0; i < 7; ++i) {
				if (frame[i])
					start_point += (1 << i);
			}
			int bit_map_start = 7;
			std::vector<int> new_eq(100);
			new_eq[start_point] = 1;
			for (int i = 0; i < 19; ++i) {
				int j = (start_point + i + 1);
				if (j >= 100)
					j -= 100;
				new_eq[j] = frame[bit_map_start + i];
			}

			int s = 0;
			while (!new_eq[s]) {
				++s;
			}

			int eq_ones = 0;
			for (int i = s; i < 100; ++i)
				eq_ones += new_eq[i];

			std::vector<int> new_y;
			int packet_start = config.get_phy_coding_overhead();
			std::copy(std::begin(frame) + packet_start, std::end(frame), std::back_inserter(new_y));

			while (eq_ones > 0 && G[s][s]) {
				if (eq_ones >= num_ones[s]) {
					for (int i = 0; i < new_y.size(); ++i)
						new_y[i] ^= Y[s][i];
					for (int i = s; i < new_eq.size(); ++i)
						new_eq[i] ^= G[s][i];
				} else {
					std::swap(new_eq, G[s]);
					std::swap(new_y, Y[s]);
					num_ones[s] = eq_ones;
				}
				while (s < new_eq.size() && !new_eq[s])
					++s;
				eq_ones = 0;
				for (int i = s; i < new_eq.size(); ++i)
					eq_ones += new_eq[i];
			}

			if (eq_ones > 0) {
				G[s] = std::move(new_eq);
				Y[s] = std::move(new_y);
				num_ones[s] = eq_ones;
				--empty_rows;
			}
			std::cerr << "\r     \r" << empty_rows;
			if (!empty_rows) {
				for (int pivot = 99; pivot > 0; --pivot) {
					for (int row = pivot - 1; row >= 0; --row) {
						if (G[row][pivot]) {
							for (int i = 0; i < Y[0].size(); ++i)
								Y[row][i] ^= Y[pivot][i];
							for (int i = 0; i <= pivot; ++i)
								G[row][i] ^= G[pivot][i];
						}
					}
				}

				std::vector<int> huge_frame;
				for (int i = 0; i < 100; ++i)
					std::copy(std::begin(Y[i]), std::end(Y[i]), std::back_inserter(huge_frame));
				m_recv_queue.push(std::move(huge_frame));

				int length = 0;
				for (int i = 0; i < 16; ++i) {
					length += (Y[0][i] << i);
				}
				std::cerr << "\n";
				std::cerr << "------------------------------------------------------------\n";
				std::cerr << "Decoding complete.\n";
				std::cerr << "Length: " << length << "\n";
				std::cerr << got << " packets used.\n";
				std::cerr << "------------------------------------------------------------\n";
				got = 0;
			}
		}
	}

private:
	Athernet::Config& config;
	Athernet::RingBuffer<T> m_Rx1_buffer;
	Athernet::RingBuffer<T> m_Rx2_buffer;
	Athernet::FrameExtractor<T> frame_extractor;
	Athernet::SyncQueue<Frame> m_recv_queue;
	Athernet::SyncQueue<Frame> m_decoder_queue;

	std::atomic_bool display_running;
	std::thread display_worker;

	std::atomic_bool decoder_running;
	std::thread decoder_worker;
};
}