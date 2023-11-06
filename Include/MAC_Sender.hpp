#pragma once

#include "Config.hpp"
#include "PHY_Unit.hpp"
#include "Protocol_Control.hpp"
#include "RingBuffer.hpp"
#include "SenderSlidingWindow.hpp"
#include "SyncQueue.hpp"
#include <mutex>
#include <thread>
#include <vector>
namespace Athernet {

template <typename T> class MAC_Sender {
	using Signal = std::vector<T>;
	using Frame = std::vector<int>;

public:
	MAC_Sender(Protocol_Control& mac_control, SenderSlidingWindow& sender_window)
		: config { Athernet::Config::get_instance() }
		, control { mac_control }
		, m_sender_window { sender_window }
	{
		running.store(true);
		worker = std::thread(&MAC_Sender::send_loop, this);
		start = 0;
		packet.reset();
	}
	~MAC_Sender()
	{
		running.store(false);
		worker.join();
	}

	void push_frame(const Frame& frame) { m_send_queue.push(frame); }
	void push_frame(Frame&& frame) { m_send_queue.push(std::move(frame)); }

	void send_loop()
	{
		state = PhySendState::PROCESS_FRAME;
		std::vector<int> frame;
		std::shared_ptr<PHY_Unit> phy_unit;
		while (running.load()) {
			if (state == PhySendState::PROCESS_FRAME) {
				if (!m_send_queue.pop(frame)) {
					std::this_thread::yield();
					continue;
				}
				assert(frame.size() <= config.get_phy_frame_payload_symbol_limit());

				int seq_num = m_sender_window.get_next_seq();
				phy_unit = std::make_shared<PHY_Unit>(std::move(frame), seq_num);

				state = PhySendState::SEND_SIGNAL;
			} else if (state == PhySendState::SEND_SIGNAL) {
				if (!m_sender_window.try_push(phy_unit)) {
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

	void modulate(Frame frame, int seq_num, int ack_num)
	{
		signal.clear();
		append_preamble(signal);

		assert(frame.size() < (1ULL << config.get_phy_frame_length_num_bits()));
		Frame length;
		for (int i = 0; i < config.get_phy_frame_length_num_bits(); ++i) {
			if ((frame.size() + 32) & (1ULL << i)) {
				length.push_back(1);
			} else {
				length.push_back(0);
			}
		}
		modulate_vec(length, signal);

		Frame temp;
		Frame to { 1, 0, 0, 0 };
		Frame from { 0, 0, 0, 0 };
		Frame seq;
		Frame control_section = { 0, 0, 0, 0, 0, 0, 0, 0 };
		for (int i = 0; i < config.get_seq_bits_length(); ++i) {
			seq.push_back((seq_num >> i) & 1);
		}

		Frame ack;
		if (ack_num != -1) {
			for (int i = 0; i < config.get_seq_bits_length(); ++i) {
				ack.push_back((ack_num >> i) & 1);
			}
			control_section[0] = 1;
		} else {
			ack = { 0, 0, 0, 0, 0, 0, 0, 0 };
		}

		std::copy(std::begin(to), std::end(to), std::back_inserter(temp));
		std::copy(std::begin(from), std::end(from), std::back_inserter(temp));
		std::copy(std::begin(seq), std::end(seq), std::back_inserter(temp));
		std::copy(std::begin(ack), std::end(ack), std::back_inserter(temp));
		std::copy(std::begin(control_section), std::end(control_section), std::back_inserter(temp));
		std::swap(temp, frame);
		std::copy(std::begin(temp), std::end(temp), std::back_inserter(frame));

		append_crc8(frame);

		modulate_vec(frame, signal);
	}

	void gen_ack(int ack_num)
	{
		signal.clear();
		append_preamble(signal);
		Frame length;
		for (int i = 0; i < config.get_phy_frame_length_num_bits(); ++i) {
			if (32 & (1ULL << i)) {
				length.push_back(1);
			} else {
				length.push_back(0);
			}
		}
		modulate_vec(length, signal);

		Frame temp;
		Frame to { 1, 0, 0, 0 };
		Frame from { 0, 0, 0, 0 };
		Frame seq { 0, 0, 0, 0, 0, 0, 0, 0 };
		Frame control_section = { 0, 0, 0, 0, 0, 0, 0, 0 };
		control_section[1] = 1;
		Frame ack;
		if (ack_num != -1) {
			for (int i = 0; i < config.get_seq_bits_length(); ++i) {
				ack.push_back((ack_num >> i) & 1);
			}
			control_section[0] = 1;
		} else {
			ack = { 0, 0, 0, 0, 0, 0, 0, 0 };
		}

		std::copy(std::begin(to), std::end(to), std::back_inserter(temp));
		std::copy(std::begin(from), std::end(from), std::back_inserter(temp));
		std::copy(std::begin(seq), std::end(seq), std::back_inserter(temp));
		std::copy(std::begin(ack), std::end(ack), std::back_inserter(temp));
		std::copy(std::begin(control_section), std::end(control_section), std::back_inserter(temp));

		append_crc8(temp);

		modulate_vec(temp, signal);
		auto t = signal;
		append_vec(t, signal);
		append_vec(t, signal);
	}

	int pop_stream(float* buffer, int count)
	{
		static int counter = 0;

		static int hold_channel = 0;
		static int trying_channel = 0;
		static int jammed = 0;
		static int slot = 12;
		static int backoff = 1;
		static int has_ack = 0;
		static int sent = 0;
		static int hold_limit = 5;
		static int ack_timeout = 0;
		static int last_ack = -1;
		static int cur_ack = -1;

		if (!packet) {
			if (ack_timeout > slot * 2) {
				m_sender_window.reset();
			}
			bool succ = m_sender_window.consume_one(packet);
			if (!succ) {
				++ack_timeout;
				if (control.ack.load() != last_ack && control.ack.load() != cur_ack) {
					cur_ack = control.ack.load();
					gen_ack(cur_ack);
				} else {
					return 0;
				}
			} else {
				ack_timeout = 0;
				cur_ack = control.ack.load();
				modulate(packet->frame, packet->seq, cur_ack);
			}
		}

		// race begin
		if (!hold_channel) {
			if (!control.busy.load()) {
				// race!
				--counter;
				if (counter < 0) {
					// shoot!
					hold_channel = 1;
					jammed = 0;
					start = 0;
					sent = 0;
					int index = 0;
					for (int i = start; index < count && i < signal.size(); ++i, ++start) {
						buffer[index++] = signal[i];
					}
					return index;
				}
				return 0;
			} else {
				// wait
				return 0;
			}
		} else {
			if (control.collision.load()) {
				// collide!
				hold_channel = 0;
				if (control.previlege_node != -1 && control.previlege_node != config.get_self_id()) {
					// ACK has the highest priority
					counter = 0;
				} else {
					backoff <<= 1;
					counter = (rand() % backoff + 1) * slot;
				}
				if (!jammed) {
					for (int i = 0; i < count; ++i) {
						buffer[i] = (float)(rand() % 50 + 50) / 100;
					}
					jammed = 1;
					return count;
				}
				return 0;
			} else {
				int index = 0;
				for (int i = start; index < count && i < signal.size(); ++i, ++start) {
					buffer[index++] = signal[i];
				}
				if (start >= signal.size()) {
					start = 0;
					packet.reset();
					last_ack = cur_ack;
					if (++sent >= hold_limit) {
						// yield
						counter = (rand() % backoff + 2) * slot;
						hold_channel = 0;
					}
					backoff = 1;
				}
				return index;
			}
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
	Config& config;
	SenderSlidingWindow& m_sender_window;
	Protocol_Control& control;

	enum class PhySendState { PROCESS_FRAME, SEND_SIGNAL, INVALID_STATE };
	PhySendState state;
	SyncQueue<Frame> m_send_queue;
	std::thread worker;
	std::atomic_bool running;
	RingBuffer<std::shared_ptr<PHY_Unit>> m_send_buffer;
	RingBuffer<std::shared_ptr<PHY_Unit>> m_resend_buffer;

	Signal m_silence = Signal(10);
	int start;
	std::shared_ptr<PHY_Unit> packet;
	Signal signal;
};
}
