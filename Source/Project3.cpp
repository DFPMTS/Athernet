#include "Config.hpp"
#include "IP_Layer.hpp"
#include "JuceHeader.h"
#include "LT_Encode.hpp"
#include "MAC_Layer.hpp"
#include "PHY_Layer.hpp"
#include <EthLayer.h>
#include <IcmpLayer.h>
#include <Packet.h>
#include <PcapLiveDevice.h>
#include <PcapLiveDeviceList.h>
#include <SystemUtils.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

template <typename T>
void random_test(Athernet::PHY_Layer<T>* physical_layer, int num_packets, int packet_length)
{
	auto sent_fd = fopen((NOTEBOOK_DIR + "sent.txt"s).c_str(), "wc");

	for (int i = 0; i < num_packets; ++i) {
		std::vector<int> a;
		for (int j = 0; j < packet_length; ++j) {
// a.push_back(rand() % 2);
#ifdef WIN
			a.push_back(0);
#else
			a.push_back(1);
#endif
		}
		a.push_back(0);

		physical_layer->send_frame(a);
		for (const auto& x : a)
			fprintf(sent_fd, "%d\n", x);
		fflush(sent_fd);
	}
	fclose(sent_fd);
}

void ping_async(Athernet::IP_Layer* ip_layer, std::string ip, int times, double interval, int length,
	std::atomic_bool& interrupt)
{
	using namespace std::chrono;

	bool ended = false;
	int sent = 0;
	for (int i = 0; i < times; ++i) {
		ip_layer->ping(ip, 0, i);
		++sent;
		auto started_waiting = system_clock::now();
		while (system_clock::now() - started_waiting < milliseconds((int)(interval * 1000))) {
			if (interrupt.load()) {
				ended = true;
				break;
			}
			std::this_thread::sleep_for(10ms);
		}
		if (ended) {
			break;
		}
	}
	// ! this is NOT thread safe, but I am lazy
	std::this_thread::sleep_for(500ms);
	auto RTT_results = std::move(ip_layer->RTTs);

	std::cerr << std::format("Ping statistics for {}:\n", ip);
	std::cerr << std::format("\tPackets: Sent = {}, Received = {}, Lost = {}({} % loss),\n", sent,
		RTT_results.size(), sent - RTT_results.size(), (sent - RTT_results.size()) * 100 / sent);
	if (RTT_results.size()) {
		uint64_t max_RTT = 0, min_RTT = 1000000, aver_RTT = 0;
		for (auto x : RTT_results) {
			max_RTT = std::max(max_RTT, x);
			min_RTT = std::min(min_RTT, x);
			aver_RTT += x;
		}
		aver_RTT /= RTT_results.size();
		std::cerr << "Approximate round trip times in milli-seconds:\n";
		std::cerr << std::format(
			"\tMinimum = {}ms, Maximum = {}ms, Average = {}ms \n", min_RTT, max_RTT, aver_RTT);
	}
}

void* Project2_main_loop(void*)
{
	// Use RAII pattern to take care of initializing/shutting down JUCE
	juce::ScopedJuceInitialiser_GUI init;

	juce::AudioDeviceManager adm;
	adm.initialiseWithDefaultDevices(1, 1);

	auto device_setup = adm.getAudioDeviceSetup();
	device_setup.sampleRate = 48'000;
	device_setup.bufferSize = 64;

	// auto physical_layer = std::make_unique<Athernet::PHY_Layer<float>>();
	auto ip_layer = std::make_unique<Athernet::IP_Layer>();

	auto device_type = adm.getCurrentDeviceTypeObject();

	{
		auto default_input = device_type->getDefaultDeviceIndex(true);
		auto input_devices = device_type->getDeviceNames(true);

		std::cerr << "-------Input-------\n";
		for (int i = 0; i < input_devices.size(); ++i)
			std::cerr << (i == default_input ? "x " : "  ") << input_devices[i] << "\n";
	}

	{
		auto default_output = device_type->getDefaultDeviceIndex(false);
		auto output_devices = device_type->getDeviceNames(false);

		std::cerr << "-------Output------\n";
		for (int i = 0; i < output_devices.size(); ++i)
			std::cerr << (i == default_output ? "x " : "  ") << output_devices[i] << "\n";
		std::cerr << "-------------------\n";
	}

// device_setup.inputDeviceName = "MacBook Pro Microphone";
#ifndef WIN
	device_setup.inputDeviceName = "USB Audio Device";
	device_setup.outputDeviceName = "USB Audio Device";
#endif
	// device_setup.outputDeviceName = "MacBook Pro Speakers";

	adm.setAudioDeviceSetup(device_setup, false);

	std::cerr << "Please configure your ASIO:\n";
	int id = 0;
	{
		auto fin = std::ifstream(NOTEBOOK_DIR "mac_addr.txt");
		if (!fin) {
			std::cerr << "Fail to read mac_addr.txt!\n";
			assert(0);
		}
		fin >> id;
	}
	std::cerr << "MAC Adress:\n";
	std::cerr << id << "\n";
	Athernet::Config::get_instance().set_self_id(id);
	int packet_size = 600;
	auto physical_layer = &ip_layer->mac_layer.phy_layer;
	adm.addAudioCallback(physical_layer);

	std::cerr << "Running...\n";

	// random_test(physical_layer, 1, 500);

	std::string s;
	std::atomic_bool ping_interrupt;
	std::jthread ping_thread;

	ping_interrupt.store(false);
	// ping_async(ip_layer.get(), "1.1.1.1", 5, 1, 10, std::ref(ping_interrupt));
	while (true) {
		std::cin >> s;
		if (s == "r") {
			int num, len;
			std::cin >> num >> len;
			random_test(physical_layer, num, len);
		} else if (s == "s") {
			std::string file;
			std::cin >> file;
			Athernet::LT_Send(physical_layer, file);
		} else if (s == "ping") {
			std::string ip;
			std::cin >> ip;
			if (!ip.size()) {
				std::cerr << "Wrong usage!\n";
				continue;
			}
			std::string options;
			getline(std::cin, options);
			std::istringstream sin(options);
			std::string opt;

			int times = 10;
			double interval = 1;
			int length = 10;

			while (sin >> opt) {
				if (opt == "-n") {
					sin >> times;
				} else if (opt == "-i") {
					sin >> interval;
				} else if (opt == "-l") {
					sin >> length;
				}
			}
			ping_interrupt.store(false);
			ping_thread = std::jthread(
				ping_async, ip_layer.get(), ip, times, interval, length, std::ref(ping_interrupt));
			// ping_async(mac_layer.get(), ip, times, interval, length, ping_interrupt);

		} else if (s == "e") {
			ping_interrupt.store(true);
			break;
		}
	}

	adm.removeAudioCallback(physical_layer);

	return NULL;
}

void display_packet(pcpp::RawPacket* pPacket, pcpp::PcapLiveDevice* pDevice, void* userCookie)
{
	std::cerr << "??????????????\n";
	auto parsed = pcpp::Packet(pPacket);
	if (parsed.isPacketOfType(pcpp::ICMP)) {
		std::cerr << "Got: ICMP\n";
		std::cerr << parsed.toString() << "\n";
		auto icmp_layer = parsed.getLayerOfType<pcpp::IcmpLayer>(false);
		auto data_len = icmp_layer->getDataLen();
		auto data_ptr = icmp_layer->getData();
		auto header_len = icmp_layer->getHeaderLen();
		auto header_ptr = icmp_layer->getIcmpHeader();
		std::cerr << "Header Len: " << header_len << "\n";
		std::cerr << "Type: " << (int)header_ptr->type << "\n";
		std::cerr << "Code: " << (int)header_ptr->code << "\n";
		std::cerr << "Checksum: " << pcpp::netToHost16(header_ptr->checksum) << "\n";
		auto echo_request = icmp_layer->getEchoRequestData();
		auto echo_reply = icmp_layer->getEchoReplyData();
		if (echo_request) {
			std::cerr << "Request:  \n";
			std::cerr << "Id:  " << echo_request->header->id << "\n";
			std::cerr << "Seq: " << echo_request->header->sequence << "\n";
			std::cerr << "Data Len: " << echo_request->dataLength << "\n";
			// for (int i = 0; i < echo_request->dataLength; ++i) {
			// 	std::cerr << (int)echo_request->data[i] << " ";
			// }
			std::cerr << "\n";
		}
		if (echo_reply) {
			std::cerr << "Reply:  \n";
			std::cerr << "Data Len: " << echo_reply->dataLength << "\n";
			// for (int i = 0; i < echo_reply->dataLength; ++i) {
			// 	std::cerr << (int)echo_reply->data[i] << " ";
			// }
			std::cerr << "\n";
		}
	}
}

int main()
{

	auto message_manager = juce::MessageManager::getInstance();
	message_manager->callFunctionOnMessageThread(Project2_main_loop, NULL);

	// should not be called manually -- juce::ScopedJuceInitialiser_GUI will do
	// juce::DeletedAtShutdown::deleteAll();

	juce::MessageManager::deleteInstance();

	return 0;

	auto dev_list = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();
	std::string eth_name;
	std::string wifi_name;
	// std::string loopback_adapter_desc = "TAP-Windows Adapter V9";
	std::string virtual_dev_name;
	for (auto dev : dev_list) {
		std::cerr << dev->getName() << "   MAC: " << dev->getMacAddress()
				  << "   IP: " << dev->getIPv4Address() << "   " << dev->getDesc() << "\n";
		if (dev->getDesc() == "Intel(R) Ethernet Controller (3) I225-V") {
			eth_name = dev->getName();
		}
		if (dev->getDesc() == "Intel(R) Wi-Fi 6E AX210 160MHz") {
			wifi_name = dev->getName();
		}
		if (dev->getDesc() == "TAP-Windows Adapter V9") {
			virtual_dev_name = dev->getName();
		}
		// if (std::mismatch(std::begin(loopback_adapter_desc), std::end(loopback_adapter_desc),
		// 		std::begin(dev->getDesc()))
		// 		.first
		// 	== std::end(loopback_adapter_desc)) {
		// 	virutal_dev_name = dev->getName();
		// }
	}

	auto tap_dev = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDeviceByName(wifi_name);
	if (!tap_dev->open()) {
		std::cerr << "ERROR!!!\n";
	}
	// eth_dev->startCapture(display_packet, NULL);
	pcpp::Packet test_packet;

	auto intel_wifi_mac_addr = pcpp::MacAddress("38:87:d5:7b:77:44");
	auto router_mac = pcpp::MacAddress(0x6a, 0x27, 0x19, 0xa7, 0x58, 0xf4);
	auto broadcast_mac = pcpp::MacAddress(0xff, 0xff, 0xff, 0xff, 0xff, 0xff);

	pcpp::EthLayer eth_layer(intel_wifi_mac_addr, router_mac, PCPP_ETHERTYPE_IP);

	// pcpp::IPv4Layer ipv4_layer(pcpp::IPv4Address("172.18.0.2"), pcpp::IPv4Address("10.0.0.1"));
	pcpp::IPv4Layer ipv4_layer(pcpp::IPv4Address("10.0.0.168"), pcpp::IPv4Address("1.1.1.1"));
	ipv4_layer.getIPv4Header()->timeToLive = 64;

	uint8_t icmp_data[] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
	pcpp::IcmpLayer icmp_layer;
	icmp_layer.setEchoRequestData(1, 200, -1, icmp_data, 16);

	test_packet.addLayer(&eth_layer);
	test_packet.addLayer(&ipv4_layer);
	test_packet.addLayer(&icmp_layer);
	test_packet.computeCalculateFields();

	auto raw_packet = test_packet.getRawPacket();
	auto data_len = raw_packet->getRawDataLen();
	for (int i = 0; i < data_len; ++i) {
		std::cerr << (int)raw_packet->getRawData()[i] << " ";
	}
	std::cerr << "\n";

	tap_dev->sendPacket(&test_packet);

	// auto eth_dev = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDeviceByName(eth_name);
	// if (!eth_dev->open()) {
	// 	abort();
	// }
	// eth_dev->sendPacket(&test_packet);

	// getchar();
	// eth_dev->stopCapture();
}