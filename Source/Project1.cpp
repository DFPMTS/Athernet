#include "Config.hpp"
#include "JuceHeader.h"
#include <boost/circular_buffer.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
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

class PHY_layer : public juce::AudioIODeviceCallback {
public:
	PHY_layer()
		: config { Athernet::Config::get_instance() }
		, m_send_buffer { config.get_physical_buffer_size() }
		, m_recv_buffer { config.get_physical_buffer_size() }
	{
	}

	virtual void audioDeviceAboutToStart([[maybe_unused]] juce::AudioIODevice* device) override { }

	virtual void audioDeviceIOCallbackWithContext([[maybe_unused]] const float* const* inputChannelData,
		[[maybe_unused]] int numInputChannels, [[maybe_unused]] float* const* outputChannelData,
		[[maybe_unused]] int numOutputChannels, [[maybe_unused]] int numSamples,
		[[maybe_unused]] const juce::AudioIODeviceCallbackContext& context) override
	{
		int samples_wrote = static_cast<int>(m_send_buffer.pop(outputChannelData[0], numSamples));

		for (int i = samples_wrote; i < numSamples; ++i) {
			outputChannelData[0][i] = 0;
		}

		// for (int i = 0; i < numSamples; ++i) {
		// 	outputChannelData[0][i] += sin(total_samples * 2 * PI * 7000 / 48000);
		// 	++total_samples;
		// }

		if (m_recv_buffer.write_available() < 50000) {
			return;
		}

		for (int i = 0; i < numSamples; ++i) {
			m_recv_buffer.push(inputChannelData[0][i]);
		}
	}

	virtual void audioDeviceStopped() override
	{
		std::fstream fout(NOTEBOOK_DIR + "received.txt", std::ios_base::out);
		m_recv_buffer.consume_all([&](float x) { fout << x << " "; });
	}

	void deliver_bits(const std::vector<int>& bits)
	{
		send_1();
		send_0();
		send_1();
		send_0();
		send_1();
		send_1();
		send_0();
		send_1();
		send_0();
		send_1();
		send_0();
		send_1();
		send_1();
		send_0();
		send_preamble();
		send_uint_16(bits.size());
		for (const auto& bit : bits) {
			assert(bit == 0 || bit == 1);
			if (bit) {
				send_1();
			} else {
				send_0();
			}
		}
		send_1();
		send_0();
		send_1();
		send_0();
		send_1();
		send_1();
		send_0();
		send_1();
		send_0();
		send_1();
		send_0();
		send_1();
		send_1();
		send_0();
	}

	std::vector<std::vector<int>> get_frames() { }

	~PHY_layer() { }

private:
	void send_preamble() { send_vec(config.get_preamble()); }

	void send_0() { send_vec(config.get_carrier_0()); }

	void send_1() { send_vec(config.get_carrier_1()); }

	void send_uint_16(size_t n)
	{
		assert(n < 65536);
		for (int i = 0; i < 16; ++i) {
			if (n & (1LL << i)) {
				send_1();
			} else {
				send_0();
			}
		}
	}

	void send_vec(const std::vector<float>& vec)
	{
		auto it = m_send_buffer.push(std::begin(vec), std::end(vec));
		assert(it == std::end(vec));
	}

private:
	Athernet::Config& config;

	// Single Producer, Single Consumer-safe
	boost::lockfree::spsc_queue<float> m_send_buffer;
	boost::lockfree::spsc_queue<float> m_recv_buffer;

	// not thread safe
	boost::circular_buffer<float> m_frame_buffer;

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
	device_setup.bufferSize = 128;

	auto physical_layer = std::make_unique<PHY_layer>();

	adm.setAudioDeviceSetup(device_setup, false);

	std::cerr << "Please configure your ASIO:\n";

	std::this_thread::sleep_for(100ms);
	std::cerr << "Running...\n";

	std::vector<int> a;
	srand(time(0));

	for (int i = 0; i < 500; ++i) {
		if (rand() % 2)
			a.push_back(0);
		else
			a.push_back(1);
	}

	physical_layer->deliver_bits(a);

	adm.addAudioCallback(physical_layer.get());

	std::this_thread::sleep_for(3s);

	// getchar();

	adm.removeAudioCallback(physical_layer.get());

	std::this_thread::sleep_for(500ms);

	std::fstream fout(NOTEBOOK_DIR + "sent.txt"s, std::ios_base::out);

	for (auto x : a) {
		fout << x << " ";
	}

	return NULL;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
	auto message_manager = juce::MessageManager::getInstance();
	message_manager->callFunctionOnMessageThread(Project1_main_loop, NULL);

	// should be called manually -- juce::ScopedJuceInitialiser_GUI will do
	// juce::DeletedAtShutdown::deleteAll();

	juce::MessageManager::deleteInstance();
}
