#pragma once

#include "Config.hpp"
#include "MAC_Control.hpp"
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
	PHY_Sender(MAC_Control& mac_control)
		: config { Athernet::Config::get_instance() }
		, control { mac_control }
	{
		running.store(true);
		worker = std::thread(&PHY_Sender::send_loop, this);
		start = 0;
		packet_size = 0;
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
				// frame date is zero-padded to a fixed length
				signal.clear();
				append_preamble(signal);

				assert(frame.size() < (1ULL << config.get_phy_frame_length_num_bits()));
				Frame length;
				for (int i = 0; i < config.get_phy_frame_length_num_bits(); ++i) {
					if (frame.size() & (1ULL << i)) {
						length.push_back(1);
					} else {
						length.push_back(0);
					}
				}
				modulate_vec(length, signal);

				append_crc8(frame);
				modulate_vec(frame, signal);

				append_silence(signal);
				state = PhySendState::SEND_SIGNAL;

				m_packet_size.push(signal.size());
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
		static int counter = 0;

		static int slice_limit = 5;
		static int previledge_node = 0;
		static int sending = 0;
		static int slice_count = 0;
		static int jammed = 0;

		if (previledge_node == config.get_self_id()) {
			if (!packet_size) {
				bool succ = m_packet_size.pop(&packet_size, 1);
				if (!succ)
					return 0;
			}
			if (!sending) {
				if (control.busy.load()) {
					// waiting
					return 0;
				} else {
					// new slice founded
					++slice_count;
					jammed = 0;
					if (slice_count > slice_limit) {
						// transfer to another node
						previledge_node ^= 1;
						slice_count = 0;
						return 0;
					} else {
						// another slice begins
						sending = 1;
					}
				}
				return 0;
			} else {
				if (control.collision.load()) {
					// halt and jam
					sending = 0;
					start = 0;
					if (!jammed) {
						for (int i = 0; i < count; ++i) {
							buffer[i] = (float)(50 + rand() % 50) / 100;
						}
						jammed = 1;
						return count;
					} else {
						return 0;
					}
				} else {
					// continue transimitting
					int size = m_send_buffer.size();
					int index = 0;
					for (int i = start; i < size && index < count && i < packet_size; ++i, ++start) {
						buffer[index++] = m_send_buffer[i];
					}
					if (start >= packet_size) {
						m_send_buffer.discard(packet_size);
						start = 0;
						packet_size = 0;
					}
					return index;
				}
			}
		} else {
			if (control.previlege_node == -1)
				return 0;
			// shut up and get ready to take over
			if (!sending) {
				if (control.busy.load()) {
					// waiting
					return 0;
				} else {
					// new slice founded
					++slice_count;
					if (slice_count > slice_limit) {
						// transfer to another node
						previledge_node ^= 1;
						slice_count = 0;
					} else {
						// another slice begins
					}
				}
			} else {
				if (control.collision.load()) {
					// halt and jam
					sending = 0;
				}
			}
			return 0;
		}
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

	void append_silence(Signal& signal) { append_vec(m_silence, signal); }

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
	RingBuffer<int> m_packet_size;
	Athernet::Config& config;

	MAC_Control& control;

	Signal m_silence = Signal(10);
	int start;
	int packet_size;
};
}
