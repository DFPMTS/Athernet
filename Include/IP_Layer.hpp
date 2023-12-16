#pragma once

#include "MAC_Layer.hpp"
#include <EthLayer.h>
#include <IcmpLayer.h>
#include <Packet.h>
#include <PcapLiveDevice.h>
#include <PcapLiveDeviceList.h>
#include <SystemUtils.h>
#include <map>

namespace Athernet {
using Bytes = std::vector<uint8_t>;

inline void wlan_loop(pcpp::RawPacket* pPacket, pcpp::PcapLiveDevice* pDevice, void* ip_layer_void);
inline void hotspot_loop(pcpp::RawPacket* pPacket, pcpp::PcapLiveDevice* pDevice, void* ip_layer_void);

class IP_Layer {
	using IpAndId = std::pair<std::string, int>;

public:
	IP_Layer()
		: config(Config::get_instance())
		, mac_layer(m_packets)
		, athernet_network("10.0.0.0/24")
		, wlan_network("10.0.0.0/24")
		, hotspot_network("10.0.0.0/24")
	{
		{
			std::string addr = config.get_self_ip();
			athernet_addr = pcpp::IPv4Address(addr);
			std::string mask = "255.255.255.0";
			athernet_network = pcpp ::IPv4Network(addr + "/"s + mask);
			athernet_running.store(true);
			athernet_thead = std::thread(&IP_Layer::athernet_loop, this);
		}

		auto dev_list = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();
		std::string wifi_name, hotspot_name;
		for (auto dev : dev_list) {
			std::cerr << dev->getName() << "   MAC: " << dev->getMacAddress()
					  << "   IP: " << dev->getIPv4Address() << "   " << dev->getDesc() << "\n";
			if (dev->getDesc() == "Microsoft Wi-Fi Direct Virtual Adapter #2") {
				hotspot_name = dev->getName();
			}
			if (dev->getDesc() == "Intel(R) Wi-Fi 6E AX210 160MHz") {
				wifi_name = dev->getName();
			}
			// if (dev->getDesc() == "TAP-Windows Adapter V9") {
			// 	virtual_dev_name = dev->getName();
			// }
			// if (std::mismatch(std::begin(loopback_adapter_desc), std::end(loopback_adapter_desc),
			// 		std::begin(dev->getDesc()))
			// 		.first
			// 	== std::end(loopback_adapter_desc)) {
			// 	virutal_dev_name = dev->getName();
			// }
		}

		if (config.is_router()) {
			std::cerr << "\n\nIAM ROUTER\n\n";
			{
				wlan_dev = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDeviceByName(wifi_name);
				if (!wlan_dev->open()) {
					std::cerr << "Cannot open WLAN !\n";
				} else {
					wlan_on = true;
					wlan_addr = wlan_dev->getIPv4Address();
					std::cerr << wlan_addr << "\n";
					wlan_network = pcpp::IPv4Network(wlan_addr.toString() + "/20"s);
					wlan_mac = wlan_dev->getMacAddress();
					wlan_geteway_mac = pcpp::MacAddress("00:00:5e:00:01:01");

					auto incoming_filter = pcpp::IPFilter(wlan_addr.toString(), pcpp::Direction::DST);
					wlan_dev->setFilter(incoming_filter);
					wlan_dev->startCapture(wlan_loop, this);
				}
			}
			{
				hotspot_dev = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDeviceByName(hotspot_name);
				if (!hotspot_dev->open()) {
					std::cerr << "Cannot open HOTSPOT !\n";
				} else {
					hotspot_on = true;
					hotspot_addr = hotspot_dev->getIPv4Address();
					std::cerr << hotspot_addr << "\n";
					hotspot_network = pcpp::IPv4Network(hotspot_addr.toString() + "/24"s);
					hotspot_mac = hotspot_dev->getMacAddress();
					hotspot_peer_mac = pcpp::MacAddress("e4:a4:71:63:1c:26");

					auto incoming_filter = pcpp::IPFilter(athernet_addr.toString(), pcpp::Direction::DST, 24);
					hotspot_dev->setFilter(incoming_filter);
					hotspot_dev->startCapture(hotspot_loop, this);
				}
			}
		}
	}

	~IP_Layer()
	{
		athernet_running.store(false);
		athernet_thead.join();
		if (wlan_on) {
			wlan_dev->stopCapture();
		}
		if (hotspot_on) {
			hotspot_dev->stopCapture();
		}
	}

	void ping(std::string dest_ip, int id, int seq, bool reply = false, uint64_t timestamp = 0,
		std::vector<uint8_t> icmp_data = std::vector<uint8_t>(10))
	{
		using namespace std::chrono;

		pcpp::Packet ping_packet(1500);

		if (dest_ip == config.get_self_ip()) {
			std::cerr << "You are pinging yourself!";
			return;
		}

		pcpp::IPv4Layer ipv4_layer(config.get_self_ip(), dest_ip);
		ipv4_layer.getIPv4Header()->timeToLive = 64;

		pcpp::IcmpLayer icmp_layer;

		// * this layer will be rewritten if the packet goes out of athernet
		// * if not, the destMac and srcMac are correct
		pcpp::EthLayer eth_layer(config.get_mac_by_id(config.get_self_id() ^ 1),
			config.get_mac_by_id(config.get_self_id()), PCPP_ETHERTYPE_IP);

		ping_packet.addLayer(&eth_layer);
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

		Bytes bytes;
		for (int i = 0; i < raw_ping->getRawDataLen(); ++i) {
			bytes.push_back(raw_ping->getRawData()[i]);
		}
		route(dest_ip, bytes);
	}

	Bytes remove_eth_layer(Bytes bytes)
	{
		timeval ts;
		gettimeofday(&ts, NULL);
		auto raw_packet = pcpp::RawPacket(bytes.data(), bytes.size(), ts, false);
		auto packet = pcpp::Packet(&raw_packet);
		auto eth_layer = packet.getLayerOfType<pcpp::EthLayer>();
		Bytes eth_payload;
		for (int i = 0; i < eth_layer->getLayerPayloadSize(); ++i) {
			eth_payload.push_back(eth_layer->getLayerPayload()[i]);
		}
		return eth_payload;
	}

	void route(std::string dest_ip, Bytes ip_packet)
	{
		pcpp::IPv4Address dest_ip_addr(dest_ip);
		std::cerr << dest_ip << "\n";
		if (!config.is_router()) {
			// * host
			if (dest_ip == config.get_self_ip()) {
				// destination reached
				process_packet(ip_packet);
			} else {
				// to gateway
				auto payload = remove_eth_layer(ip_packet);
				auto bits = bytes_to_bits(payload);
				bits.push_back(0);
				mac_layer.m_sender.push_frame(bits);
			}
		} else {
			// * router
			if (dest_ip == config.get_self_ip()) {
				process_packet(ip_packet);
			} else if (dest_ip_addr.matchNetwork(athernet_network)) {
				// * send it through athernet
				auto payload = remove_eth_layer(ip_packet);
				auto bits = bytes_to_bits(payload);
				bits.push_back(0);
				mac_layer.m_sender.push_frame(bits);
			} else if (dest_ip_addr.matchNetwork(hotspot_network)) {
				send_to_hotspot(ip_packet);
			} else {
				// * NAT : ip -> icmp echo id
				timeval ts;
				gettimeofday(&ts, NULL);
				auto raw = pcpp::RawPacket(ip_packet.data(), ip_packet.size(), ts, false);
				auto packet = pcpp::Packet(&raw);
				std::cerr << packet.toString() << "\n";
				if (packet.isPacketOfType(pcpp::ICMP)) {
					auto ip_layer = packet.getLayerOfType<pcpp::IPv4Layer>();
					auto icmp_layer = packet.getLayerOfType<pcpp::IcmpLayer>();
					if (icmp_layer->getIcmpHeader()->type != pcpp::ICMP_ECHO_REQUEST) {
						return;
					}

					auto echo_request = icmp_layer->getEchoRequestData();
					int id = pcpp::netToHost16(echo_request->header->id);
					int seq = pcpp::netToHost16(echo_request->header->sequence);
					uint64_t timestamp = echo_request->header->timestamp;

					Bytes req_data;
					for (int i = 0; i < echo_request->dataLength; ++i) {
						req_data.push_back(echo_request->data[i]);
					}

					auto src_ip = ip_layer->getSrcIPv4Address().toString();
					auto ip_and_id = std::make_pair(src_ip, id);
					if (!ip_and_id_to_echo_id[ip_and_id]) {
						++cur_echo_id;
						ip_and_id_to_echo_id[ip_and_id] = cur_echo_id;
						echo_id_to_ip_and_id[cur_echo_id] = ip_and_id;
					}
					auto echo_id = ip_and_id_to_echo_id[ip_and_id];

					ip_layer->setSrcIPv4Address(wlan_addr);
					icmp_layer->setEchoRequestData(echo_id, seq, timestamp, req_data.data(), req_data.size());

					// * throw it to WLAN interface
					auto raw_packet = packet.getRawPacket();

					Bytes bytes;
					for (int i = 0; i < raw_packet->getRawDataLen(); ++i) {
						bytes.push_back(raw_packet->getRawData()[i]);
					}
					send_to_wlan(bytes);
				}
			}
		}
	}

	void send_to_wlan(Bytes ip_packet)
	{
		timeval ts;
		gettimeofday(&ts, NULL);
		auto raw = pcpp::RawPacket(ip_packet.data(), ip_packet.size(), ts, false);
		auto packet = pcpp::Packet(&raw);

		// * modify
		auto eth_layer = packet.getLayerOfType<pcpp::EthLayer>();
		eth_layer->setSourceMac(wlan_mac);
		eth_layer->setDestMac(wlan_geteway_mac);
		packet.computeCalculateFields();
		std::cerr << packet.toString() << "\n";

		wlan_dev->sendPacket(&packet);
	}

	void send_to_hotspot(Bytes ip_packet)
	{
		timeval ts;
		gettimeofday(&ts, NULL);
		auto raw = pcpp::RawPacket(ip_packet.data(), ip_packet.size(), ts, false);
		auto packet = pcpp::Packet(&raw);

		// * modify
		auto eth_layer = packet.getLayerOfType<pcpp::EthLayer>();
		eth_layer->setSourceMac(hotspot_mac);
		eth_layer->setDestMac(hotspot_peer_mac);
		packet.computeCalculateFields();
		std::cerr << packet.toString() << "\n";

		hotspot_dev->sendPacket(&packet);
	}

	// void athernet_loop()
	// {
	// 	//	add header
	// 	pcpp::EthLayer eth_layer(config.get_mac_by_id(config.get_self_id() ^ 1),
	// 		config.get_mac_by_id(config.get_self_id()), PCPP_ETHERTYPE_IP);

	// 	pcpp::Packet mac_header;
	// 	mac_header.addLayer(&eth_layer);
	// 	std::vector<uint8_t> mac_header_bytes;
	// 	for (int i = 0; i < mac_header.getRawPacket()->getRawDataLen(); ++i) {
	// 		mac_header_bytes.push_back(mac_header.getRawPacket()->getRawData()[i]);
	// 	}
	// 	for (auto x : bytes) {
	// 		mac_header_bytes.push_back(x);
	// 	}
	// 	std::swap(bytes, mac_header_bytes);
	// }

	void process_packet(Bytes bytes)
	{
		using namespace std::chrono;

		// ! NAT

		timeval ts;
		gettimeofday(&ts, NULL);
		auto raw = pcpp::RawPacket(bytes.data(), bytes.size(), ts, false);
		auto packet = pcpp::Packet(&raw);

		if (packet.isPacketOfType(pcpp::ICMP)) {
			auto ip_layer = packet.getLayerOfType<pcpp::IPv4Layer>();
			auto src_ip = ip_layer->getSrcIPv4Address();

			auto icmp_layer = packet.getLayerOfType<pcpp::IcmpLayer>();
			auto header_ptr = icmp_layer->getIcmpHeader();

			if (config.is_router() && !src_ip.matchNetwork(athernet_network)
				&& !src_ip.matchNetwork(hotspot_network)) {
				// * NAT : icmp echo id -> ip
				if (header_ptr->type == pcpp::ICMP_ECHO_REPLY) {
					auto echo_reply = icmp_layer->getEchoReplyData();
					int id = pcpp::netToHost16(echo_reply->header->id);
					int seq = pcpp::netToHost16(echo_reply->header->sequence);
					uint64_t timestamp = echo_reply->header->timestamp;
					Bytes reply_data;
					for (int i = 0; i < echo_reply->dataLength; ++i) {
						reply_data.push_back(echo_reply->data[i]);
					}

					if (!echo_id_to_ip_and_id.count(id)) {
						// discard
						return;
					}

					auto [real_ip, real_id] = echo_id_to_ip_and_id[id];
					std::cerr << real_ip << "   " << real_id << "\n";

					icmp_layer->setEchoReplyData(
						real_id, seq, timestamp, reply_data.data(), reply_data.size());
					ip_layer->setDstIPv4Address(pcpp::IPv4Address(real_ip));
					packet.computeCalculateFields();

					auto raw_real_repy = packet.getRawPacket();
					std::cerr << raw_real_repy->getRawDataLen() << "\n";
					for (int i = 0; i < raw_real_repy->getRawDataLen(); ++i) {
						std::cerr << (int)raw_real_repy->getRawData()[i] << " ";
					}
					std::cerr << "\n";
					std::cerr << bytes.size() << "\n";
					for (int i = 0; i < bytes.size(); ++i) {
						std::cerr << (int)bytes[i] << " ";
					}
					std::cerr << "\n";

					if (real_ip == config.get_self_ip()) {
						// * Yes, it is for you
						process_icmp(bytes);
					} else {
						// * Throw it to where it should belong
						route(real_ip, bytes);
					}
				}
			} else {
				process_icmp(bytes);
			}
		}
	}

	void process_icmp(Bytes bytes)
	{
		std::cerr << "process icmp\n";
		using namespace std::chrono;

		timeval ts;
		gettimeofday(&ts, NULL);
		auto raw = pcpp::RawPacket(bytes.data(), bytes.size(), ts, false);
		auto packet = pcpp::Packet(&raw);

		assert(packet.isPacketOfType(pcpp::ICMP));
		auto ip_layer = packet.getLayerOfType<pcpp::IPv4Layer>();
		auto src_ip = ip_layer->getSrcIPv4Address();

		auto icmp_layer = packet.getLayerOfType<pcpp::IcmpLayer>();
		auto header_ptr = icmp_layer->getIcmpHeader();

		// * just reply
		if (header_ptr->type == pcpp::ICMP_ECHO_REQUEST) {
			std::cerr << "-------------------------------REQUEST-------------------------------\n";
			std::cerr << packet.toString() << "\n";

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
			std::cerr << "---------------------------------------------------------------------\n";
		} else if (header_ptr->type == pcpp::ICMP_ECHO_REPLY) {
			std::cerr << "--------------------------------REPLY--------------------------------\n";
			std::cerr << packet.toString() << "\n";

			auto echo_reply = icmp_layer->getEchoReplyData();

			int id = pcpp::netToHost16(echo_reply->header->id);
			int seq = pcpp::netToHost16(echo_reply->header->sequence);
			uint64_t timestamp = echo_reply->header->timestamp;
			auto RTT = (duration_cast<milliseconds>(system_clock::now().time_since_epoch())
				- milliseconds(timestamp))
						   .count();
			RTTs.push_back(RTT);
			std::cerr << "Reply:  \n";
			std::cerr << "Id:  " << id << "\n";
			std::cerr << "Seq: " << seq << "\n";
			std::cerr << "RTT: " << RTT << "ms"
					  << "\n";
			std::cerr << "Data Len: " << echo_reply->dataLength << "\n";
			std::cerr << "---------------------------------------------------------------------\n";
		}
	}

	void athernet_loop()
	{
		Bytes bytes;
		while (athernet_running.load()) {
			if (!m_packets.pop(bytes)) {
				continue;
			}
			timeval ts;
			gettimeofday(&ts, NULL);
			auto raw = pcpp::RawPacket(bytes.data(), bytes.size(), ts, false);
			auto packet = pcpp::Packet(&raw);
			auto dest_ip = packet.getLayerOfType<pcpp::IPLayer>()->getDstIPAddress().toString();
			std::cerr << packet.toString() << "\n";
			route(dest_ip, bytes);
		}
	}

	std::vector<int> bytes_to_bits(const std::vector<uint8_t>& bytes)
	{
		std::vector<int> bits;
		for (auto x : bytes) {
			for (int i = 0; i < 8; ++i) {
				bits.push_back((x >> i) & 1);
			}
		}
		return bits;
	}

	Config& config;
	SyncQueue<Bytes> m_packets;
	MAC_Layer mac_layer;

	pcpp::IPv4Address athernet_addr;
	pcpp::IPv4Network athernet_network;
	std::thread athernet_thead;
	std::atomic_bool athernet_running;

	pcpp::IPv4Address wlan_addr;
	pcpp::IPv4Network wlan_network;
	pcpp::MacAddress wlan_mac;
	pcpp::MacAddress wlan_geteway_mac;
	pcpp::PcapLiveDevice* wlan_dev;
	std::atomic_bool wlan_running;
	bool wlan_on = false;

	pcpp::IPv4Address hotspot_addr;
	pcpp::IPv4Network hotspot_network;
	pcpp::MacAddress hotspot_mac;
	pcpp::MacAddress hotspot_peer_mac;
	pcpp::PcapLiveDevice* hotspot_dev;
	bool hotspot_on = false;

	int cur_echo_id = 0;
	std::map<IpAndId, int> ip_and_id_to_echo_id;
	std::map<int, IpAndId> echo_id_to_ip_and_id;

	std::vector<uint64_t> RTTs;
};

inline void wlan_loop(pcpp::RawPacket* pPacket, pcpp::PcapLiveDevice* pDevice, void* ip_layer_void)
{
	auto ip_layer = static_cast<IP_Layer*>(ip_layer_void);

	auto parsed = pcpp::Packet(pPacket);
	if (parsed.isPacketOfType(pcpp::ICMP)) {
		Bytes bytes;
		for (int i = 0; i < pPacket->getRawDataLen(); ++i) {
			bytes.push_back(pPacket->getRawData()[i]);
		}
		ip_layer->process_packet(bytes);
	}
}

inline void hotspot_loop(pcpp::RawPacket* pPacket, pcpp::PcapLiveDevice* pDevice, void* ip_layer_void)
{
	auto ip_layer = static_cast<IP_Layer*>(ip_layer_void);

	auto parsed = pcpp::Packet(pPacket);
	if (parsed.isPacketOfType(pcpp::ICMP)) {
		Bytes bytes;
		for (int i = 0; i < pPacket->getRawDataLen(); ++i) {
			bytes.push_back(pPacket->getRawData()[i]);
		}
		timeval ts;
		gettimeofday(&ts, NULL);
		auto packet = pcpp::Packet(pPacket);
		auto dest_ip = packet.getLayerOfType<pcpp::IPLayer>()->getDstIPAddress().toString();
		std::cerr << packet.toString() << "\n";
		ip_layer->route(dest_ip, bytes);
	}
}
}