#pragma once

#include "JuceHeader.h"
#include "PHY_Layer.hpp"
#include "Protocol_Control.hpp"
#include "ReceiverSlidingWindow.hpp"
#include "SenderSlidingWindow.hpp"
#include <atomic>

namespace Athernet {

class MAC_Layer {
	using Frame = std::vector<int>;

public:
	MAC_Layer()
		: m_sender(control, m_sender_window)
		, m_receiver(control, m_recv_queue, m_sender_window, m_receiver_window)
		, phy_layer(control, m_sender, m_receiver)
	{
		running.store(true);
		worker = std::thread(&MAC_Layer::flow_control, this);
	}

	void flow_control() { }

	~MAC_Layer()
	{
		running.store(false);
		worker.join();
	}

	Protocol_Control control;
	SyncQueue<MacFrame> m_recv_queue;

	MAC_Sender<float> m_sender;
	MAC_Receiver<float> m_receiver;
	PHY_Layer<float> phy_layer;

	SenderSlidingWindow m_sender_window;
	ReceiverSlidingWindow m_receiver_window;

	std::atomic_bool running;
	std::thread worker;
};
}