#include "Config.hpp"
#include "JuceHeader.h"
#include "LT_Encode.hpp"
#include "MAC_Layer.hpp"
#include "PHY_Layer.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using namespace std::string_literals;

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
	auto mac_layer = std::make_unique<Athernet::MAC_Layer>();

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

	std::cerr << "Running...\n";

	srand(static_cast<unsigned int>(time(0)));

	auto physical_layer = &mac_layer->phy_layer;

	adm.addAudioCallback(physical_layer);

	// random_test(physical_layer, 1, 500);

	// for (int i = 0; i < 16; ++i) {
	// 	int y = Athernet::Config::get_instance().get_map_4b_5b(i);
	// 	std::cerr << i << " -> " << y << " -> " << Athernet::Config::get_instance().get_map_5b_4b(y) << "\n";
	// }

	std::string s;
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
		} else if (s == "e") {
			break;
		}
	}

	adm.removeAudioCallback(physical_layer);

	return NULL;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
	// std::string file = "INPUT.txt";
	// file = NOTEBOOK_DIR + file;
	// auto fd = freopen(file.c_str(), "w", stdout);
	// for (int i = 0; i < 10000; ++i) {
	// 	if (rand() % 2) {
	// 		putchar('1');
	// 	} else {
	// 		putchar('0');
	// 	}
	// }
	// fclose(fd);
	// return 0;
	auto message_manager = juce::MessageManager::getInstance();
	message_manager->callFunctionOnMessageThread(Project2_main_loop, NULL);

	// should not be called manually -- juce::ScopedJuceInitialiser_GUI will do
	// juce::DeletedAtShutdown::deleteAll();

	juce::MessageManager::deleteInstance();
}