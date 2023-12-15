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

class MAC_Layer {
	using Frame = std::vector<int>;

public:
	MAC_Layer()
		: config(Config::get_instance())
		, m_sender(control, m_sender_window)
		, m_receiver(control, m_recv_queue, m_sender_window, m_receiver_window)
		, phy_layer(control, m_sender, m_receiver)
	{
		running.store(true);
		worker = std::thread(&MAC_Layer::flow_control, this);
	}

	void ping(std::string dest_ip, int id, int seq, bool reply = false, uint64_t timestamp = 0,
		std::vector<uint8_t> icmp_data = std::vector<uint8_t>(10))
	{
		using namespace std::chrono;

		pcpp::Packet ping_packet;

		if (dest_ip == config.get_self_ip()) {
			std::cerr << "You are pinging yourself!";
			return;
		}

		pcpp::IPv4Layer ipv4_layer(config.get_self_ip(), dest_ip);
		ipv4_layer.getIPv4Header()->timeToLive = 64;

		pcpp::IcmpLayer icmp_layer;

		ping_packet.addLayer(&ipv4_layer);
		ping_packet.addLayer(&icmp_layer);

		if (!reply) {
			// request
			timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
			icmp_layer.setEchoRequestData(id, seq, timestamp, icmp_data.data(), icmp_data.size());
		} else {
			// reply
			icmp_layer.setEchoReplyData(id, seq, timestamp, icmp_data.data(), icmp_data.size());
		}
		ping_packet.computeCalculateFields();

		auto raw_ping = ping_packet.getRawPacket();
		auto bits = bytes_to_bits(raw_ping->getRawData(), raw_ping->getRawDataLen());

		// for (int i = 0; i < raw_ping->getRawDataLen(); ++i) {
		// 	std::cerr << (int)raw_ping->getRawData()[i] << " ";
		// }
		// std::cerr << "\n";

		auto payload = bits;
		std::vector<uint8_t> bytes;
		for (int i = 0; i < payload.size(); i += 8) {
			int x = 0;
			for (int j = 0; j < 8; ++j) {
				x += (payload[i + j] << j);
			}
			bytes.push_back(x);
		}

		// indicate that this packet has to be processed directly
		bits.push_back(0);
		m_sender.push_frame(bits);
	}

	void flow_control()
	{
		using namespace std::chrono;
		while (running.load()) {
			// * load from recv queue
			Frame payload;
			while (m_recv_queue.pop(payload)) {
				// bits to bytes
				if (payload.size() & 8) {
					std::cerr << "Not bytes! Skip.\n";
					continue;
				}
				std::vector<uint8_t> bytes;
				for (int i = 0; i < payload.size(); i += 8) {
					int x = 0;
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

				timeval ts;
				gettimeofday(&ts, NULL);
				auto raw = pcpp::RawPacket(bytes.data(), bytes.size(), ts, false);
				auto packet = pcpp::Packet(&raw);
				std::cerr << packet.toString() << "\n";

				if (packet.isPacketOfType(pcpp::ICMP)) {
					auto ip_layer = packet.getLayerOfType<pcpp::IPv4Layer>();
					auto src_ip = ip_layer->getSrcIPv4Address();

					auto icmp_layer = packet.getLayerOfType<pcpp::IcmpLayer>();
					auto icmp_header = icmp_layer->getIcmpHeader();

					auto data_len = icmp_layer->getDataLen();
					auto data_ptr = icmp_layer->getData();
					auto header_len = icmp_layer->getHeaderLen();
					auto header_ptr = icmp_layer->getIcmpHeader();

					// std::cerr << "Header Len: " << header_len << "\n";
					// std::cerr << "Type: " << (int)header_ptr->type << "\n";
					// std::cerr << "Code: " << (int)header_ptr->code << "\n";
					// std::cerr << "Checksum: " << pcpp::netToHost16(header_ptr->checksum) << "\n";

					if (header_ptr->type == pcpp::ICMP_ECHO_REQUEST) {
						std::cerr
							<< "-------------------------------REQUEST-------------------------------\n";
						auto echo_request = icmp_layer->getEchoRequestData();

						int id = pcpp::netToHost16(echo_request->header->id);
						int seq = pcpp::netToHost16(echo_request->header->sequence);
						uint64_t timestamp = echo_request->header->timestamp;

						std::cerr << "Request:  \n";
						std::cerr << "Id:  " << id << "\n";
						std::cerr << "Seq: " << seq << "\n";
						std::cerr << "Data Len: " << echo_request->dataLength << "\n";

						std::vector<uint8_t> req_data;
						for (int i = 0; i < echo_request->dataLength; ++i) {
							req_data.push_back(echo_request->data[i]);
						}

						ping(src_ip.toString(), id, seq, true, timestamp, req_data);
						std::cerr
							<< "---------------------------------------------------------------------\n";
					} else if (header_ptr->type == pcpp::ICMP_ECHO_REPLY) {
						std::cerr
							<< "--------------------------------REPLY--------------------------------\n";
						auto echo_reply = icmp_layer->getEchoReplyData();

						int id = pcpp::netToHost16(echo_reply->header->id);
						int seq = pcpp::netToHost16(echo_reply->header->sequence);
						uint64_t timestamp = echo_reply->header->timestamp;

						std::cerr << "Reply:  \n";
						std::cerr << "Id:  " << id << "\n";
						std::cerr << "Seq: " << seq << "\n";
						std::cerr << "Time: "
								  << duration_cast<milliseconds>(system_clock::now().time_since_epoch())
								- milliseconds(timestamp)
								  << "\n";
						std::cerr << "Data Len: " << echo_reply->dataLength << "\n";
						std::cerr
							<< "---------------------------------------------------------------------\n";
					}
				}
			}
		}
	}

	~MAC_Layer()
	{
		running.store(false);
		worker.join();
	}

	std::vector<int> bytes_to_bits(const uint8_t* a, int len)
	{
		std::vector<int> bits;
		for (int i = 0; i < len; ++i) {
			int x = a[i];
			for (int i = 0; i < 8; ++i) {
				bits.push_back((x >> i) & 1);
			}
		}
		return bits;
	}

	Config& config;
	Protocol_Control control;
	SyncQueue<Frame> m_recv_queue;

	MAC_Sender<float> m_sender;
	MAC_Receiver<float> m_receiver;
	PHY_Layer<float> phy_layer;

	SenderSlidingWindow m_sender_window;
	ReceiverSlidingWindow m_receiver_window;

	std::atomic_bool running;
	std::thread worker;
};
}