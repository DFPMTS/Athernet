#pragma once

#include "JuceHeader.h"
#include "PHY_Layer.hpp"
#include "Protocol_Control.hpp"
#include "ReceiverSlidingWindow.hpp"
#include "SenderSlidingWindow.hpp"
#include <EthLayer.h>
#include <IcmpLayer.h>
#include <Packet.h>
#include <PcapLiveDevice.h>
#include <PcapLiveDeviceList.h>
#include <SystemUtils.h>
#include <atomic>

namespace Athernet {
using Bytes = std::vector<uint8_t>;

class MAC_Layer {
	using Frame = std::vector<int>;

public:
	MAC_Layer(SyncQueue<Bytes>& packets)
		: config(Config::get_instance())
		, m_packets(packets)
		, m_sender(control, m_sender_window)
		, m_receiver(control, m_recv_queue, m_sender_window, m_receiver_window)
		, phy_layer(control, m_sender, m_receiver)
	{
		running.store(true);
		worker = std::thread(&MAC_Layer::process_pay_load, this);
	}

	~MAC_Layer()
	{
		running.store(false);
		worker.join();
	}

	void process_pay_load()
	{
		Frame payload;
		while (running.load()) {
			if (!m_recv_queue.pop(payload)) {
				continue;
			}

			assert(payload.size() % 8 == 0);

			Bytes bytes;
			for (int i = 0; i < payload.size(); i += 8) {
				uint8_t x = 0;
				for (int j = 0; j < 8; ++j) {
					x += (payload[i + j] << j);
				}
				bytes.push_back(x);
			}

			pcpp::EthLayer eth_layer(config.get_mac_by_id(config.get_self_id() ^ 1),
				config.get_mac_by_id(config.get_self_id()), PCPP_ETHERTYPE_IP);

			pcpp::Packet mac_header;
			mac_header.addLayer(&eth_layer);
			std::vector<uint8_t> mac_header_bytes;
			for (int i = 0; i < mac_header.getRawPacket()->getRawDataLen(); ++i) {
				mac_header_bytes.push_back(mac_header.getRawPacket()->getRawData()[i]);
			}
			for (auto x : bytes) {
				mac_header_bytes.push_back(x);
			}
			std::swap(bytes, mac_header_bytes);
			m_packets.push(bytes);
		}
	}

	Config& config;
	Protocol_Control control;
	SyncQueue<Frame> m_recv_queue;
	SyncQueue<Bytes>& m_packets;

	std::vector<uint64_t> RTTs;

	MAC_Sender<float> m_sender;
	MAC_Receiver<float> m_receiver;
	PHY_Layer<float> phy_layer;

	SenderSlidingWindow m_sender_window;
	ReceiverSlidingWindow m_receiver_window;

	std::atomic_bool running;
	std::thread worker;
};
}