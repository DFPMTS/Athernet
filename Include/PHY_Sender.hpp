#pragma once

#include "Chirps.hpp"
#include "Config.hpp"
#include "RingBuffer.hpp"
#include "SyncQueue.hpp"
#include <mutex>
#include <thread>
#include <vector>
namespace Athernet {

template <typename T> class PHY_Sender {
	using Signal = std::vector<T>;
	using Frame = std::vector<int>;

public:
	PHY_Sender()
		: config { Athernet::Config::get_instance() }
	{
		remove((NOTEBOOK_DIR + "sent.txt"s).c_str());
		running.store(true);
		worker = std::thread(&PHY_Sender::send_loop, this);
	}
	~PHY_Sender()
	{
		running.store(false);
		worker.join();
	}

	void push_frame(const Frame& frame) { m_send_queue.push(frame); }
	void push_frame(Frame&& frame) { m_send_queue.push(std::move(frame)); }

	void send_loop()
	{
		state = PhySendState::PROCESS_FRAME;
		Signal signal;
		std::vector<int> frame;
		while (running.load()) {
			if (state == PhySendState::PROCESS_FRAME) {
				if (!m_send_queue.pop(frame)) {
					std::this_thread::yield();
					continue;
				}
				assert(frame.size() <= config.get_phy_frame_payload_symbol_limit());

				signal.clear();
				append_preamble(signal);
				append_silence(config.get_silence_length(), signal);

				// Frame frame_0(frame.begin(), frame.begin() + 100 + 26);
				// Frame frame_1(frame.begin() + 100 + 26, frame.end());
				// append_crc8(frame_0);
				// append_crc8(frame_1);

				modulate_vec(frame, signal);
				auto recv_fd = fopen((NOTEBOOK_DIR + "sent.txt"s).c_str(), "a");
				for (int i = 0; i < 250; ++i) {
					fprintf(recv_fd, "%d", frame[i]);
				}

				fprintf(recv_fd, "\n");
				fflush(recv_fd);

				fclose(recv_fd);

				// modulate_vec(frame_1, signal);

				append_silence(500, signal);

				state = PhySendState::SEND_SIGNAL;
			} else if (state == PhySendState::SEND_SIGNAL) {
				if (!m_send_buffer.push(signal)) {
					std::this_thread::yield();
					continue;
				} else {
					static int num = 0;
					std::cerr << "SEND  " << ++num << "\n";
					state = PhySendState::PROCESS_FRAME;
				}
			} else if (state == PhySendState::INVALID_STATE) {
				std::cerr << "Invalid state!\n";
				assert(0);
			}
		}
	}

	int pop_stream(float* buffer, int count)
	{
		return m_send_buffer.pop_with_conversion_to_float(buffer, count);
	}

private:
	void append_preamble(Signal& signal) { append_vec(config.get_preamble(Athernet::Tag<T>()), signal); }

	void modulate_vec(const Frame& frame, Signal& signal)
	{
		for (int i = 0; i < frame.size(); i += config.get_num_carriers()) {
			Signal this_piece(config.get_symbol_length());
			for (int j = 0; j < config.get_num_carriers(); ++j) {
				if ((i + j < frame.size()) ? frame[i + j] : 0) {
					add_carrier(j, 1, this_piece);
				} else {
					add_carrier(j, 0, this_piece);
				}
			}
			append_vec(this_piece, signal);
			append_silence(config.get_silence_length(), signal);
		}
	}

	void add_carrier(int carrier_id, int carrier_type, Signal& signal)
	{
		for (int i = 0; i < config.get_symbol_length(); ++i) {
			signal[i] += config.get_carriers(Tag<T>())[carrier_id][carrier_type][i];
		}
	}

	void append_crc8(Frame& frame)
	{
		Frame saved_frame { frame };
		for (int i = 0; i < config.get_crc_residual_length(); ++i)
			frame.push_back(0);

		for (int i = 0; i < frame.size() - config.get_crc_residual_length(); ++i) {
			if (frame[i]) {
				for (int offset = 0; offset < config.get_crc_length(); ++offset) {
					frame[i + offset] ^= config.get_crc()[offset];
				}
			}
		}
		for (int i = frame.size() - config.get_crc_residual_length(); i < frame.size(); ++i)
			saved_frame.push_back(frame[i]);
		frame = std::move(saved_frame);
	}

	void append_silence(int count, Signal& signal)
	{
		for (int i = 0; i < count; ++i) {
			signal.push_back(0);
		}
	}

	void append_vec(const Signal& from, Signal& to)
	{
		std::copy(std::begin(from), std::end(from), std::back_inserter(to));
	}

private:
	enum class PhySendState { PROCESS_FRAME, SEND_SIGNAL, INVALID_STATE };
	PhySendState state;
	Athernet::SyncQueue<Frame> m_send_queue;
	std::thread worker;
	std::atomic_bool running;
	Athernet::RingBuffer<T> m_send_buffer;
	Athernet::Config& config;
};
}
