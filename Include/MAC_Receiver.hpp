#pragma once
#include "Config.hpp"
#include "LT_Decode.hpp"
#include "MacFrame.hpp"
#include "PHY_FrameExtractor.hpp"
#include "Protocol_Control.hpp"
#include "ReceiverSlidingWindow.hpp"
#include "RingBuffer.hpp"
#include "SenderSlidingWindow.hpp"
#include "SyncQueue.hpp"
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

namespace Athernet {

template <typename T> class MAC_Receiver {
	using Frame = std::vector<int>;

public:
	MAC_Receiver(Protocol_Control& mac_control, SyncQueue<MacFrame>& recv_queue,
		SenderSlidingWindow& sender_window, ReceiverSlidingWindow& receiver_window)
		: config { Athernet::Config::get_instance() }
		, control { mac_control }
		, m_recv_queue { recv_queue }
		, m_sender_window { sender_window }
		, m_receiver_window { receiver_window }
		, frame_extractor(m_recv_buffer, m_recv_queue, m_decoder_queue, mac_control)
	// , decoder(m_decoder_queue, m_recv_queue)
	{
		display_running.store(true);
		display_worker = std::thread(&MAC_Receiver::forward_frame, this);
	}

	~MAC_Receiver()
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

	void forward_frame()
	{
		MacFrame mac_frame;
		int finished = 0;
		while (display_running.load()) {
			if (!m_recv_queue.pop(mac_frame)) {
				continue;
			}

			if (!mac_frame.bad_data && !mac_frame.is_ack)
				config.log("-------------------------------GOT-DATA-------------------------------");
			else
				config.log("------------------------------GOT-HEADER------------------------------");
			config.log(std::format(
				"                               {}                               ", control.clock.load()));
			config.log(std::format("From: {} --> To: {}", mac_frame.from, mac_frame.to));
			config.log(std::format(
				"Seq: {}   Has ACK: {}   ACK: {}", mac_frame.seq, mac_frame.has_ack, mac_frame.ack));

			config.log("----------------------------------------------------------------------");

			if (!mac_frame.bad_data && !mac_frame.is_ack) {
				control.previlege_node.store(mac_frame.from);
			}

			// accept point to point / broadcast
			if (mac_frame.to != config.get_self_id() && mac_frame.to != ((1 << 4) - 1))
				continue;

			if (mac_frame.is_syn) {
				control.transmission_start.store(true);
				config.timer_set();
				continue;
			}

			if (mac_frame.has_ack) {
				m_sender_window.remove_acked(mac_frame.ack);
			}

			if (!mac_frame.is_ack && !mac_frame.bad_data) {
				control.ack.store(m_receiver_window.receive_packet(mac_frame.data, mac_frame.seq));
			}
			if (m_receiver_window.get_num_collected() >= 50000) {
				config.log(
					"--------------------------------------------------------------------------------File "
					"Received!----------------------------------------------------------------------------"
					"----");
				config.timer_get();
				std::cerr << "+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+"
							 "o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o+o\n";
				std::vector<int> a;
				m_receiver_window.collect(a);
				std::string file = "MAC_received.txt";
				FILE* receive_fd = fopen((NOTEBOOK_DIR + file).c_str(), "wb");
				if (!receive_fd) {
					std::cerr << "Unable to open " << file << "!\n";
					assert(0);
				}
				for (int i = 0; i < 50000; i += 8) {
					int c = 0;
					for (int j = 0; j < 8; ++j) {
						c += a[i + j] << j;
					}
					fprintf(receive_fd, "%c", c);
					if (i % 10000 == 0) {
						fflush(receive_fd);
					}
				}
				fclose(receive_fd);
				finished = 1;
			}
		}
	}

private:
	Config& config;
	Protocol_Control& control;
	SyncQueue<MacFrame>& m_recv_queue;
	SenderSlidingWindow& m_sender_window;
	ReceiverSlidingWindow& m_receiver_window;

	RingBuffer<T> m_recv_buffer;
	FrameExtractor<T> frame_extractor;
	SyncQueue<Frame> m_decoder_queue;

	// LT_Decode decoder;

	std::atomic_bool display_running;
	std::thread display_worker;
};
}