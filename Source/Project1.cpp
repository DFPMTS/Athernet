#include "Config.hpp"
#include "JuceHeader.h"
#include "RingBuffer.hpp"
#include <algorithm>
#include <boost/circular_buffer.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
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

std::mutex mutex;

int total_filled;

class PHY_layer : public juce::AudioIODeviceCallback {
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
		int samples_wrote = m_send_buffer.pop(outputChannelData[0], numSamples);

		total_filled += samples_wrote;

		for (int i = samples_wrote; i < numSamples; ++i) {
			outputChannelData[0][i] = 0;
		}

		// for (int i = 0; i < numSamples; ++i) {
		// 	outputChannelData[0][i] += sin(total_samples * 2 * PI * 7000 / 48000);
		// 	++total_samples;
		// }

		if (m_recv_buffer.size() > 500000) {
			return;
		}

		for (int i = 0; i < numSamples; ++i) {
			auto res = m_recv_buffer.push(inputChannelData[0][i]);
			assert(res);
		}
	}

	virtual void audioDeviceStopped() override { lock.unlock(); }

	void deliver_bits(const std::vector<int>& bits)
	{
		std::vector<float> physical_frame;
		append_preamble(physical_frame);
		append_uint_16(bits.size(), physical_frame);
		for (const auto& bit : bits) {
			assert(bit == 0 || bit == 1);
			if (bit) {
				append_1(physical_frame);
			} else {
				append_0(physical_frame);
			}
		}

		// append_silence(physical_frame);
		auto result = m_send_buffer.push(physical_frame);
		assert(result);
	}

	void send_nonsense(int size)
	{
		std::vector<float> nonsense;
		for (int i = 0; i < size; ++i) {
			if (rand() & 1)
				append_1(nonsense);
			else
				append_0(nonsense);
		}
		m_send_buffer.push(nonsense);
	}

	std::vector<std::vector<int>> get_frames() { }

	~PHY_layer() { }

	Athernet::RingBuffer<float> m_recv_buffer;

private:
	const std::vector<float>& get_preamble() { return config.get_preamble(); }

	const std::vector<float>& get_0() { return config.get_carrier_0(); }

	const std::vector<float>& get_1() { return config.get_carrier_1(); }

	void append_preamble(std::vector<float>& signal) { append_vec(get_preamble(), signal); }

	void append_0(std::vector<float>& signal) { append_vec(get_0(), signal); }
	void append_1(std::vector<float>& signal) { append_vec(get_1(), signal); }

	void append_uint_16(size_t x, std::vector<float>& signal)
	{
		assert(x < 65536);

		for (int i = 0; i < 16; ++i) {
			if (x & (1ULL << i)) {
				append_1(signal);
			} else {
				append_0(signal);
			}
		}
	}

	void append_silence(std::vector<float>& signal) { append_vec(silence, signal); }

	void append_vec(const std::vector<float>& from, std::vector<float>& to)
	{
		std::copy(std::begin(from), std::end(from), std::back_inserter(to));
	}

private:
	std::unique_lock<std::mutex> lock { mutex };

	Athernet::Config& config;

	// Single Producer, Single Consumer-safe
	// boost::lockfree::spsc_queue<float> m_send_buffer;
	// boost::lockfree::spsc_queue<float> m_recv_buffer;

	Athernet::RingBuffer<float> m_send_buffer;

	// not thread safe
	boost::circular_buffer<float> m_frame_buffer;

	std::vector<float> silence = std::vector<float>(200);

	int total_samples = 0;
};

void* Project1_main_loop(void*)
{
	// Use RAII pattern to take care of initializing/shutting down JUCE
	juce::ScopedJuceInitialiser_GUI init;

	juce::AudioDeviceManager adm;
	adm.initialiseWithDefaultDevices(1, 1);

	auto device_setup = adm.getAudioDeviceSetup();
	device_setup.sampleRate = 48'000;
	device_setup.bufferSize = 64;

	auto physical_layer = std::make_unique<PHY_layer>();

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
	device_setup.outputDeviceName = "USB Audio Device";

	// device_setup.outputDeviceName = "MacBook Pro Speakers";

	device_setup.outputDeviceName = "USB Audio Device";

	adm.setAudioDeviceSetup(device_setup, false);

	std::cerr << "Please configure your ASIO:\n";

	std::this_thread::sleep_for(100ms);
	std::cerr << "Running...\n";

	std::ifstream fin("INPUT.bin");

	srand(time(0));

	std::fstream fout(NOTEBOOK_DIR + "sent.txt"s, std::ios_base::out);

	adm.addAudioCallback(physical_layer.get());

	for (int i = 0; i < 70; ++i) {
		std::vector<int> a;
		for (int i = 0; i < 100; ++i) {
			if (rand() % 2)
				a.push_back(1);
			else
				a.push_back(0);
		}

		physical_layer->deliver_bits(a);
		if ((i + 1) % 6 == 0)
			std::this_thread::sleep_for(500ms);
		for (const auto& x : a)
			fout << x << " ";
	}

	// physical_layer->send_nonsense(500);

	std::this_thread::sleep_for(9s);

	// getchar();

	adm.removeAudioCallback(physical_layer.get());

	std::lock_guard<std::mutex> lock_guard { mutex };

	static float recv[1000000];

	auto size = physical_layer->m_recv_buffer.pop(recv, 1000000);

	{
		std::ofstream fout(NOTEBOOK_DIR + "received.txt");

		for (int i = 0; i < size; ++i) {
			fout << recv[i] << " ";
		}
	}

	std::cerr << "Total filled: " << total_filled << "\n";

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