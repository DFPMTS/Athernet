#include "JuceHeader.h"
#include <boost/circular_buffer.hpp>
#include <chrono>
#include <deque>
#include <queue>

#define PI acos(-1)

using namespace std::chrono_literals;

class Tester : public juce::AudioIODeviceCallback {
public:
	virtual void audioDeviceAboutToStart(juce::AudioIODevice* device) override
	{
		record_start_time_point = std::chrono::high_resolution_clock::now();
	}

	virtual void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
		int numInputChannels,
		float* const* outputChannelData,
		int numOutputChannels,
		int numSamples,
		const juce::AudioIODeviceCallbackContext& context) override
	{
		if (std::chrono::high_resolution_clock::now() - record_start_time_point < record_length) {
			for (int i = 0; i < numSamples; ++i) {
				recorded.push(inputChannelData[0][i]);
			}
		} else {
			static int set = 0;
			if (!set) {
				set = 1;
				std::cerr << "Playing>\n";
			}

			for (int i = 0; i < numSamples; ++i) {
				if (!recorded.empty()) {
					outputChannelData[0][i] = recorded.front() * 20;
					recorded.pop();
				} else {
					outputChannelData[0][i] = 0;
				}
			}
		}
	}

	virtual void audioDeviceStopped() override
	{
	}

private:
	std::chrono::time_point<std::chrono::high_resolution_clock> record_start_time_point;
	std::chrono::milliseconds record_length { 10'000 };

	std::queue<float> recorded;
};

void* main_loop(void*)
{
	// Use RAII pattern to take care of initializing/shutting down JUCE
	juce::ScopedJuceInitialiser_GUI init;

	juce::AudioDeviceManager adm;
	adm.initialiseWithDefaultDevices(1, 1);

	auto device_setup = adm.getAudioDeviceSetup();
	device_setup.sampleRate = 48'000;

	auto tester = std::make_unique<Tester>();

	auto device_type = adm.getCurrentDeviceTypeObject();

	{
		auto default_input = device_type->getDefaultDeviceIndex(true);
		auto input_devices = device_type->getDeviceNames(true);

		std::cerr << "-------Input-------\n";
		for (int i = 0; i < input_devices.size(); ++i)
			std::cerr << (i == default_input ? "x " : "  ")
					  << input_devices[i] << "\n";
	}

	{
		auto default_output = device_type->getDefaultDeviceIndex(false);
		auto output_devices = device_type->getDeviceNames(false);

		std::cerr << "-------Output------\n";
		for (int i = 0; i < output_devices.size(); ++i)
			std::cerr << (i == default_output ? "x " : "  ")
					  << output_devices[i] << "\n";
		std::cerr << "-------------------\n";
	}

	adm.setAudioDeviceSetup(device_setup, false);

	std::cerr << "Please configure your ASIO:\n";
	getchar();

	adm.addAudioCallback(tester.get());
	std::cerr << "Running...\n";
	getchar();
	adm.removeAudioCallback(tester.get());

	return NULL;
}

int main(int argc, char* argv[])
{

#ifndef NDEBUG
	juce::UnitTestRunner test_runner;
	test_runner.runAllTests();
#endif

	auto message_manager = juce::MessageManager::getInstance();
	message_manager->callFunctionOnMessageThread(main_loop, NULL);

	// should be called manually -- juce::ScopedJuceInitialiser_GUI will do
	// juce::DeletedAtShutdown::deleteAll();

	juce::MessageManager::deleteInstance();
}
