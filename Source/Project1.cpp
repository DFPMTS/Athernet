#include "Config.hpp"
#include "JuceHeader.h"
#include "PHY_Receiver.hpp"
#include "PHY_Sender.hpp"
#include "RingBuffer.hpp"
#include <algorithm>
#include <boost/circular_buffer.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define PI acos(-1)

// #define WIN

#ifndef WIN
#define NOTEBOOK_DIR "/Users/dfpmts/Desktop/JUCE_Demos/NewProject/Extras/"s
#else
#define NOTEBOOK_DIR "D:/fa23/Athernet/Extras/"s
#endif

using namespace std::chrono_literals;
using namespace std::string_literals;

template <typename T> class PHY_layer : public juce::AudioIODeviceCallback {
public:
	PHY_layer()
		: config { Athernet::Config::get_instance() }
	{
	}

	virtual void audioDeviceAboutToStart([[maybe_unused]] juce::AudioIODevice* device) override { }

	virtual void audioDeviceIOCallbackWithContext([[maybe_unused]] const float* const* inputChannelData,
		[[maybe_unused]] int numInputChannels, [[maybe_unused]] float* const* outputChannelData,
		[[maybe_unused]] int numOutputChannels, [[maybe_unused]] int numSamples,
		[[maybe_unused]] const juce::AudioIODeviceCallbackContext& context) override
	{
		int samples_wrote = m_sender.pop_stream(outputChannelData[0], numSamples);

		for (int i = samples_wrote; i < numSamples; ++i) {
			outputChannelData[0][i] = 0;
		}

		m_receiver.push_stream(inputChannelData[0], numSamples);
	}

	virtual void audioDeviceStopped() override { }

	void send_frame(const std::vector<int>& frame) { m_sender.push_frame(frame); }

	std::vector<std::vector<int>> get_frames() { }

	~PHY_layer() { }

private:
	Athernet::Config& config;
	Athernet::PHY_Receiver<T> m_receiver;
	Athernet::PHY_Sender<T> m_sender;
};

template <typename T> void random_test(PHY_layer<T>* physical_layer, int num_packets, int packet_length)
{
	auto sent_fd = fopen((NOTEBOOK_DIR + "sent.txt"s).c_str(), "wc");

	for (int i = 0; i < num_packets; ++i) {
		std::vector<int> a = {};
		for (int j = 0; j < packet_length; ++j) {
			if (rand() % 2)
				a.push_back(1);
			else
				a.push_back(0);
		}

		physical_layer->send_frame(a);
		for (const auto& x : a)
			fprintf(sent_fd, "%d\n", x);
		fflush(sent_fd);
	}
	fclose(sent_fd);
}

void* Project1_main_loop(void*)
{
	// Use RAII pattern to take care of initializing/shutting down JUCE
	juce::ScopedJuceInitialiser_GUI init;

	juce::AudioDeviceManager adm;
	adm.initialiseWithDefaultDevices(1, 1);

	auto device_setup = adm.getAudioDeviceSetup();
	device_setup.sampleRate = 48'000;
	device_setup.bufferSize = 256;

	auto physical_layer = std::make_unique<PHY_layer<int>>();

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
	device_setup.inputDeviceName = "USB Audio Device";
	device_setup.outputDeviceName = "USB Audio Device";

	// device_setup.outputDeviceName = "MacBook Pro Speakers";

	// device_setup.outputDeviceName = "USB Audio Device";

	adm.setAudioDeviceSetup(device_setup, false);

	std::cerr << "Please configure your ASIO:\n";

	std::this_thread::sleep_for(100ms);
	std::cerr << "Running...\n";

	std::ifstream fin("INPUT.bin");

	srand(static_cast<unsigned int>(time(0)));

	adm.addAudioCallback(physical_layer.get());

	std::string s;
	while (std::cin >> s) {
		if (s == "s") {
			std::cin.ignore();
			std::cout << "Please type your message:\n";
			std::string data_s;
			std::getline(std::cin, data_s);
			std::vector<int> a;
			for (const auto& c : data_s) {
				int x = c;
				for (int i = 0; i < 8; ++i, x >>= 1) {
					a.push_back(x & 1);
				}
			}
			physical_layer->send_frame(a);
		} else if (s == "r") {
			int num, len;
			std::cin >> num >> len;
			random_test<int>(physical_layer.get(), num, len);
		} else if (s == "exit") {
			break;
		}
	}

	adm.removeAudioCallback(physical_layer.get());

	return NULL;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
	std::ios::sync_with_stdio(false);

	auto message_manager = juce::MessageManager::getInstance();
	message_manager->callFunctionOnMessageThread(Project1_main_loop, NULL);

	// should be called manually -- juce::ScopedJuceInitialiser_GUI will do
	// juce::DeletedAtShutdown::deleteAll();

	juce::MessageManager::deleteInstance();
}