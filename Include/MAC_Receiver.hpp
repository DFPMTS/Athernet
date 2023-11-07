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
		int data_packet = 0;
		while (display_running.load()) {
			if (!m_recv_queue.pop(mac_frame)) {
				std::this_thread::yield();
				continue;
			}
			if (!mac_frame.bad_data)
				std::cerr << "-------------------------------GOT-DATA-------------------------------\n";
			else
				std::cerr << "------------------------------GOT-HEADER------------------------------\n";

			std::cerr << "From: " << mac_frame.from << " --> "
					  << "To: " << mac_frame.to << "\n"
					  << "Seq: " << mac_frame.seq << "    Has ack:" << mac_frame.has_ack
					  << "  Ack: " << mac_frame.ack << "\n";
			std::cerr << "----------------------------------------------------------------------\n";
			if (!mac_frame.bad_data)
				control.previlege_node.store(mac_frame.from);

			if (mac_frame.to != config.get_self_id())
				continue;

			if (mac_frame.has_ack) {
				m_sender_window.remove_acked(mac_frame.ack);
			}
			if (!mac_frame.bad_data) {
				++data_packet;
			}
			if (!mac_frame.is_ack && !mac_frame.bad_data)
				control.ack.store(m_receiver_window.receive_packet(mac_frame.seq));
		}
		std::cerr << "Total data packets:   " << data_packet << "\n";
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