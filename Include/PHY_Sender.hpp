#pragma once

#include "Config.hpp"
#include "RingBuffer.hpp"
#include "SyncQueue.hpp"
#include <boost/lockfree/queue.hpp>
#include <mutex>
#include <thread>
#include <vector>
namespace Athernet {

template <typename T> class PHY_Sender {
	using Signal = std::vector<T>;
	using PhyFrame = std::vector<int>;

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

	void push_frame(const PhyFrame& frame) { m_send_queue.push(frame); }
	void push_frame(PhyFrame&& frame) { m_send_queue.push(std::move(frame)); }

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
				// frame date is zero-padded to a fixed length
				signal.clear();
				append_preamble(signal);
				append_length(frame.size(), signal);
				for (const auto& bit : frame) {
					assert(bit == 0 || bit == 1);
					if (bit) {
						append_1(signal);
					} else {
						append_0(signal);
					}
				}
				append_crc8(frame, signal);
				append_silence(signal);
				append_silence(signal);
				state = PhySendState::SEND_SIGNAL;
			} else if (state == PhySendState::SEND_SIGNAL) {
				if (!m_send_buffer.push(signal)) {
					std::this_thread::yield();
					continue;
				} else {
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

	void append_0(Signal& signal) { append_vec(config.get_carrier_0(Athernet::Tag<T>()), signal); }
	void append_1(Signal& signal) { append_vec(config.get_carrier_1(Athernet::Tag<T>()), signal); }

	void append_length(size_t x, Signal& signal)
	{
		assert(x < (1ULL << config.get_phy_frame_length_num_bits()));

		for (int i = 0; i < config.get_phy_frame_length_num_bits(); ++i) {
			if (x & (1ULL << i)) {
				append_1(signal);
			} else {
				append_0(signal);
			}
		}
	}

	void append_crc8(const std::vector<int>& frame, Signal& signal)
	{
		std::vector<int> bits(config.get_phy_frame_length_num_bits() + frame.size() + 8);
		int size_start = 0;
		int data_start = config.get_phy_frame_length_num_bits();
		int crc_start = data_start + static_cast<int>(frame.size());

		for (int i = 0; i < config.get_phy_frame_length_num_bits(); ++i) {
			if (frame.size() & (1ULL << i)) {
				bits[size_start + i] = 1;
			}
		}
		for (int i = 0; i < frame.size(); ++i) {
			bits[data_start + i] = frame[i];
		}

		for (int i = 0; i < crc_start; ++i) {
			if (bits[i]) {
				for (int offset = 0; offset < 9; ++offset) {
					bits[i + offset] ^= config.get_crc()[offset];
				}
			}
		}

		for (int i = 0; i < 8; ++i) {
			if (bits[crc_start + i]) {
				append_1(signal);
			} else {
				append_0(signal);
			}
		}
	}

	void append_silence(Signal& signal) { append_vec(m_silence, signal); }

	void append_vec(const Signal& from, Signal& to)
	{
		std::copy(std::begin(from), std::end(from), std::back_inserter(to));
	}

private:
	enum class PhySendState { PROCESS_FRAME, SEND_SIGNAL, INVALID_STATE };
	PhySendState state;
	Athernet::SyncQueue<PhyFrame> m_send_queue;
	std::thread worker;
	std::atomic_bool running;
	Athernet::RingBuffer<T> m_send_buffer;
	Athernet::Config& config;
	Signal m_silence = Signal(200);
};
}
