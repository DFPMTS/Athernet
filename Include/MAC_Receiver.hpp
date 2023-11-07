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
	MAC_Receiver(Protocol_Control& mac_control, SyncQueue<Frame>& recv_queue,
		SenderSlidingWindow& sender_window, ReceiverSlidingWindow& receiver_window)
		: config { Athernet::Config::get_instance() }
		, control { mac_control }
		, m_recv_queue { recv_queue }
		, m_sender_window { sender_window }
		, m_receiver_window { receiver_window }
		, frame_extractor(m_recv_buffer, m_recv_queue, m_decoder_queue, mac_control)
		, decoder(m_decoder_queue, m_recv_queue)
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
		Frame frame;

		while (display_running.load()) {
			if (!m_recv_queue.pop(frame)) {
				std::this_thread::yield();
				continue;
			}
			std::cerr << "------------------------------GOT_---------------------------\n";
			std::cerr << m_recv_queue.m_queue.size() << "\n";
			MacFrame mac_frame(frame);

			std::cerr << mac_frame.from << " --> " << mac_frame.to << "   " << mac_frame.seq << "  "
					  << mac_frame.ack << "    " << mac_frame.is_ack << "\n";

			if (mac_frame.to != config.get_self_id())
				continue;
			if (mac_frame.is_ack)
				std::cerr
					<< "-------------------------------------ACK---------------------------------------\n";
			if (mac_frame.has_ack) {
				m_sender_window.remove_acked(mac_frame.ack);
			}
			if (!mac_frame.is_ack)
				control.ack.store(m_receiver_window.receive_packet(mac_frame.seq));
		}
	}

private:
	Config& config;
	Protocol_Control& control;
	SyncQueue<Frame>& m_recv_queue;
	SenderSlidingWindow& m_sender_window;
	ReceiverSlidingWindow& m_receiver_window;

	RingBuffer<T> m_recv_buffer;
	FrameExtractor<T> frame_extractor;
	SyncQueue<Frame> m_decoder_queue;

	LT_Decode decoder;

	std::atomic_bool display_running;
	std::thread display_worker;
};
}