#pragma once

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
	using MI_Signal = std::vector<Signal>;
	void send_loop()
	{
		state = PhySendState::PROCESS_FRAME;
		Signal data_signal;
		Signal signal;
		std::vector<int> frame;
		MI_Signal mi_signal;
		int tx1_sent = 0, tx2_sent = 0;
		while (running.load()) {
			if (state == PhySendState::PROCESS_FRAME) {
				if (!m_send_queue.pop(frame)) {
					std::this_thread::yield();
					continue;
				}
				assert(frame.size() <= config.get_phy_frame_payload_symbol_limit());
				data_signal.clear();
				assert(frame.size() < (1ULL << config.get_phy_frame_length_num_bits()));
				Frame length;
				for (int i = 0; i < config.get_phy_frame_length_num_bits(); ++i) {
					if (frame.size() & (1ULL << i)) {
						length.push_back(1);
					} else {
						length.push_back(0);
					}
				}
				modulate_vec(length, data_signal);
				append_crc8(frame);
				modulate_vec(frame, data_signal);

				for (int i = 0; i < 2; ++i) {
					signal.clear();
					if (i == 0) {
						append_preamble(signal);
						modulate_vec({ 0, 1 }, signal);
					} else {
						append_silence(config.get_preamble_length(), signal);
						modulate_vec({ 0, 0 }, signal);
					}
					append_vec(data_signal, signal);
					append_silence(200, signal);
					mi_signal.push_back(std::move(signal));
				}
				tx1_sent = 0;
				tx2_sent = 0;
				state = PhySendState::SEND_SIGNAL;
			} else if (state == PhySendState::SEND_SIGNAL) {
				if (!tx1_sent) {
					if (!m_Tx1_buffer.push(mi_signal[0])) {
						std::this_thread::yield();
						continue;
					} else {
						tx1_sent = true;
					}
				}
				if (!tx2_sent) {
					if (!m_Tx2_buffer.push(mi_signal[1])) {
						std::this_thread::yield();
						continue;
					} else {
						tx2_sent = true;
					}
				}
				state = PhySendState::PROCESS_FRAME;
			} else if (state == PhySendState::INVALID_STATE) {
				std::cerr << "Invalid state!\n";
				assert(0);
			}
		}
	}

	int Tx1_pop_stream(float* buffer, int count)
	{
		return m_Tx1_buffer.pop_with_conversion_to_float(buffer, count);
	}

	int Tx2_pop_stream(float* buffer, int count)
	{
		return m_Tx2_buffer.pop_with_conversion_to_float(buffer, count);
	}

private:
	void append_preamble(Signal& signal) { append_vec(config.get_preamble(Athernet::Tag<T>()), signal); }

	void modulate_vec(const Frame& frame, Signal& signal)
	{
		for (int i = 0; i < frame.size(); i += config.get_num_carriers()) {
			Signal this_piece(config.get_phy_frame_CP_length() + config.get_symbol_length());
			for (int j = 0; j < config.get_num_carriers(); ++j) {
				if ((i + j < frame.size()) ? frame[i + j] : 0) {
					add_carrier(j, 1, this_piece);
				} else {
					add_carrier(j, 0, this_piece);
				}
			}
			add_CP(this_piece);
			append_vec(this_piece, signal);
		}
	}

	void add_carrier(int carrier_id, int carrier_type, Signal& signal)
	{
		for (int i = 0; i < config.get_symbol_length(); ++i) {
			signal[config.get_phy_frame_CP_length() + i]
				+= config.get_carriers(Tag<T>())[carrier_id][carrier_type][i];
		}
	}

	void add_CP(Signal& signal)
	{
		for (int i = 0; i < config.get_phy_frame_CP_length(); ++i) {
			signal[i] = signal[config.get_symbol_length() + i];
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
		for (int i = 0; i < count; ++i)
			signal.push_back(0);
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
	Athernet::RingBuffer<T> m_Tx1_buffer;
	Athernet::RingBuffer<T> m_Tx2_buffer;
	Athernet::Config& config;
};
}
