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
					continue;
				}
				assert(frame.size() <= config.get_phy_frame_payload_symbol_limit());

				int seq_num = m_sender_window.get_next_seq();
				phy_unit = std::make_shared<PHY_Unit>(std::move(frame), seq_num);

				state = PhySendState::SEND_SIGNAL;
			} else if (state == PhySendState::SEND_SIGNAL) {
				if (!m_sender_window.try_push(phy_unit)) {
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

		static int hold_channel = 0;
		static int trying_channel = 0;
		static int jammed = 0;
		static int slot = 16;
		static int backoff = 1;
		static int ack_timeout = 0;
		static int ack_timeout_times = 0;
		static int last_ack = -1;
		static int cur_ack = -1;
		static int syn_issued = 0;
		static int syn_sent = 0;
		static int registered_ack = -1;

		if (control.transmission_start.load()) {
			control.clock.fetch_add(1);
		}

		int ack_value = control.ack.load();
		if (ack_value != registered_ack) {
			ack_timeout_times = 0;
			registered_ack = ack_value;
		}

		if (ack_timeout_times > 10) {
			// no more messages / link dead
			return 0;
		}

		if (!packet) {
			if (!control.transmission_start.load()) {
				if (config.get_self_id() == 0) {
					if (!syn_issued) {
						gen_syn();
						syn_issued = 1;
					}
					if (syn_sent) {
						return 0;
					}
				} else {
					return 0;
				}
			} else {
				srand(config.get_self_id() + rand());
				if (ack_timeout > slot * 10) {
					m_sender_window.reset();
					last_ack = -1;
					++ack_timeout_times;
				}

				bool succ = m_sender_window.consume_one(packet);
				if (!succ) {
					if (!control.busy.load())
						++ack_timeout;
					if (control.collision.load())
						ack_timeout = 0;
					if (control.ack.load() != last_ack && control.ack.load() != cur_ack) {
						cur_ack = control.ack.load();
						gen_ack(cur_ack);
					}
					config.log(std::format(
						"cur_ack:  {},  last_ack:  {},  ack:  {}", cur_ack, last_ack, control.ack.load()));
					if (control.ack.load() == last_ack) {
						hold_channel = 0;
						return 0;
					}
				} else {
					ack_timeout = 0;
					cur_ack = control.ack.load();
					modulate(packet->frame, packet->seq, cur_ack);
				}
			}
		} else {
			if (!hold_channel && control.ack.load() != cur_ack) {
				cur_ack = control.ack.load();
				modulate(packet->frame, packet->seq, cur_ack);
			}
		}

		// race begin
		if (!hold_channel) {
			if (!control.busy.load()) {

				--counter;

				// race!
				if (counter < 0) {
					config.log(std::format("+++++RETRY+++++ happened at {} ", control.clock.load()));
					// if (control.previlege_node != config.get_self_id()) {
					// 	m_sender_window.reset();
					// 	if (m_sender_window.consume_one(packet)) {
					// 		modulate(packet->frame, packet->seq, control.ack.load());
					// 	}
					// }

					// shoot!
					hold_channel = 1;
					jammed = 0;
					start = 0;
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
				config.log(std::format("*****CLASH***** happened at {} ", control.clock.load()));
				// collide!
				hold_channel = 0;
				backoff <<= 1;
				if (backoff > 6)
					backoff = 6;
				counter = (rand() % backoff) * slot;

				config.log(std::format("Counter set to {} ", counter));
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
					config.log(std::format("^^^^^SENT^^^^^ at {}", control.clock.load()));
					start = 0;
					packet.reset();
					last_ack = cur_ack;
					counter = slot >> 1;
					backoff = 1;
					if (syn_issued && !control.transmission_start.load()) {
						syn_sent = 1;
					}
				}
				return index;
			}
		}
	}

private:
	void append_num(int num, int num_bits, Frame& frame)
	{
		for (int i = 0; i < num_bits; ++i) {
			frame.push_back((num >> i) & 1);
		}
	}

	void modulate(Frame frame, int seq_num, int ack_num)
	{
		signal.clear();
		append_preamble(signal);

		assert(frame.size() < (1ULL << config.get_phy_frame_length_num_bits()));
		Frame length;
		append_num(frame.size() + 32, config.get_phy_frame_length_num_bits(), length);
		modulate_vec_4b5b_nrzi(length, signal);

		Frame mac_frame;
		// to
		append_num(config.get_self_id() ^ 1, 4, mac_frame);
		// from
		append_num(config.get_self_id(), 4, mac_frame);
		// seq
		append_num(seq_num, 8, mac_frame);
		// control_section
		Frame control_section = { 0, 0, 0, 0, 0, 0, 0, 0 };
		// ack
		if (ack_num != -1) {
			append_num(ack_num, 8, mac_frame);
			control_section[0] = 1;
		} else {
			append_num(0, 8, mac_frame);
		}
		// add control section
		append_vec(control_section, mac_frame);
		// crc for mac header
		append_crc8(mac_frame);
		// crc for payload
		append_crc8(frame);
		// add payload
		append_vec(frame, mac_frame);

		// modulate_vec(mac_frame, signal);
		modulate_vec_4b5b_nrzi(mac_frame, signal);
	}

	void gen_ack(int ack_num)
	{
		signal.clear();
		append_preamble(signal);

		Frame frame = std::vector<int>(100);
		Frame length;
		append_num(frame.size() + 32, config.get_phy_frame_length_num_bits(), length);
		modulate_vec_4b5b_nrzi(length, signal);

		Frame mac_frame;
		// to
		append_num(config.get_self_id() ^ 1, 4, mac_frame);
		// from
		append_num(config.get_self_id(), 4, mac_frame);
		// seq
		append_num(0, 8, mac_frame);
		// control_section
		Frame control_section = { 0, 0, 0, 0, 0, 0, 0, 0 };
		// is ack
		control_section[1] = 1;
		// ack
		if (ack_num != -1) {
			append_num(ack_num, 8, mac_frame);
			control_section[0] = 1;
		} else {
			append_num(0, 8, mac_frame);
		}
		// add control section
		append_vec(control_section, mac_frame);
		// crc for mac header
		append_crc8(mac_frame);
		// crc for payload
		append_crc8(frame);
		// add payload
		append_vec(frame, mac_frame);

		// modulate_vec(mac_frame, signal);
		// modulate_vec(mac_frame, signal);
		// modulate_vec(mac_frame, signal);
		// modulate_vec(mac_frame, signal);
		modulate_vec_4b5b_nrzi(mac_frame, signal);
		int signal_size = signal.size();
		signal.resize(signal_size * 2);
		std::copy(std::begin(signal), std::begin(signal) + signal_size, std::begin(signal) + signal_size);
	}

	void gen_syn()
	{
		signal.clear();
		append_preamble(signal);

		Frame frame = std::vector<int>(300);
		Frame length;
		append_num(frame.size() + 32, config.get_phy_frame_length_num_bits(), length);
		modulate_vec_4b5b_nrzi(length, signal);

		Frame mac_frame;
		// broad cast
		append_num((1 << 4) - 1, 4, mac_frame);
		// from
		append_num(config.get_self_id(), 4, mac_frame);
		// seq
		append_num(0, 8, mac_frame);
		// ack
		append_num(0, 8, mac_frame);
		// control_section
		Frame control_section = { 0, 0, 1, 0, 0, 0, 0, 0 };

		// add control section
		append_vec(control_section, mac_frame);
		// crc for mac header
		append_crc8(mac_frame);
		// crc for payload
		append_crc8(frame);
		// add payload
		append_vec(frame, mac_frame);

		modulate_vec_4b5b_nrzi(mac_frame, signal);
	}

	void append_preamble(Signal& signal) { append_vec(config.get_preamble(Athernet::Tag<T>()), signal); }

	Frame encode_4b5b(const Frame& frame)
	{
		Frame ret;
		for (int i = 0; i < frame.size(); i += 4) {
			int x = 0;
			for (int j = 0; j < 4; ++j) {
				int bit = (i + j < frame.size()) ? frame[i + j] : 0;
				x += bit << j;
			}
			int y = config.get_map_4b_5b(x);
			for (int j = 0; j < 5; ++j) {
				ret.push_back((y >> j) & 1);
			}
		}
		return ret;
	}

	void modulate_vec_4b5b_nrzi(const Frame& frame, Signal& signal)
	{
		float last = 1.0f;
		signal.push_back(last);
		signal.push_back(last);
		auto encoded_4b5b = encode_4b5b(frame);
		// std::cerr << frame.size() << "mapped to " << encoded_4b5b.size() << "\n";
		for (auto x : encoded_4b5b) {
			if (x == 1) {
				last = -last;
			}
			signal.push_back(last);
			signal.push_back(last);
		}
	}

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

	template <typename U> void append_vec(const std::vector<U>& from, std::vector<U>& to)
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
