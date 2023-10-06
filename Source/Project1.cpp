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
#include <vector>

#define PI acos(-1)

using namespace std::chrono_literals;

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
		int samples_wrote = static_cast<int>(m_send_buffer.pop(outputChannelData[0], numSamples));

		for (int i = samples_wrote; i < numSamples; ++i) {
			outputChannelData[0][i] = 0;
		}

		// for (int i = 0; i < numSamples; ++i) {
		// 	outputChannelData[0][i] += sin(total_samples * 2 * PI * 7000 / 48000);
		// 	++total_samples;
		// }

		if (m_recv_buffer.size() > 50000) {
			return;
		}

		for (int i = 0; i < numSamples; ++i) {
			auto res = m_recv_buffer.push(inputChannelData[0][i]);
			assert(res);
		}
	}

	virtual void audioDeviceStopped() override
	{
		std::fstream fout("D:/fa23/Athernet/Extras/received.txt", std::ios_base::out);
		float buffer[100000];
		auto size = m_recv_buffer.pop(buffer, 100000);
		for (int i = 0; i < size; ++i) {
			fout << buffer[i] << " ";
		}
	}

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

		auto result = m_send_buffer.push(physical_frame);
		assert(result);
	}

	std::vector<std::vector<int>> get_frames() { }

	~PHY_layer() { }

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

	void append_vec(const std::vector<float>& from, std::vector<float>& to)
	{
		std::copy(std::begin(from), std::end(from), std::back_inserter(to));
	}

private:
	Athernet::Config& config;

	// Single Producer, Single Consumer-safe
	// boost::lockfree::spsc_queue<float> m_send_buffer;
	// boost::lockfree::spsc_queue<float> m_recv_buffer;

	Athernet::RingBuffer<float> m_send_buffer;
	Athernet::RingBuffer<float> m_recv_buffer;

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

	for (int i = 0; i < 1000; ++i) {
		if (rand() % 2)
			a.push_back(0);
		else
			a.push_back(1);
	}

	adm.addAudioCallback(physical_layer.get());

	physical_layer->deliver_bits(a);

	std::this_thread::sleep_for(3s);

	// getchar();

	adm.removeAudioCallback(physical_layer.get());

	std::this_thread::sleep_for(500ms);

	std::fstream fout("D:/fa23/Athernet/Extras/sent.txt", std::ios_base::out);

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