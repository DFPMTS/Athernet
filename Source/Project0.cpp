#include "JuceHeader.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <queue>

#define PI acos(-1)

using namespace std::chrono_literals;

class Tester : public juce::AudioIODeviceCallback {
public:
	Tester()
		: recorded(4800'000)
	{
	}

	virtual void audioDeviceAboutToStart([[maybe_unused]] juce::AudioIODevice* device) override
	{
		record_start_time_point = std::chrono::high_resolution_clock::now();
		sample_rate = device->getCurrentSampleRate();
		assert(sample_rate == 48'000);
		sound_f = 7000;
	}

	virtual void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
		[[maybe_unused]] int numInputChannels, float* const* outputChannelData,
		[[maybe_unused]] int numOutputChannels, int numSamples,
		[[maybe_unused]] const juce::AudioIODeviceCallbackContext& context) override
	{
		if (total_samples > sample_rate * 100) {
			total_samples %= sample_rate;
		}

		if (std::chrono::high_resolution_clock::now() - record_start_time_point < record_length) {
			for (int i = 0; i < numSamples; ++i) {
				recorded[total_samples++] = inputChannelData[0][i];
				if (play_sound) {
					outputChannelData[0][i] = sin(total_samples * 2 * PI * sound_f / sample_rate);
				}
			}
		} else {
			static int set = 0;
			if (!set) {
				set = 1;
				std::cerr << "Playing>\n";
			}

			for (int i = 0; i < numSamples; ++i) {
				if (read_samples < total_samples) {
					outputChannelData[0][i] = recorded[read_samples++] * 20;
				} else {
					outputChannelData[0][i] = 0;
				}
			}
		}
	}

	virtual void audioDeviceStopped() override { }

	void set_play_sound(bool v) { play_sound = v; }

private:
	std::chrono::time_point<std::chrono::high_resolution_clock> record_start_time_point;
	std::chrono::milliseconds record_length { 10'000 };

	bool play_sound = false;
	int total_samples = 0;
	int read_samples = 0;
	int sound_f = 7000;
	int sample_rate = 48000;

	// This implementaion is BROKEN since there would memory allocation in the callback
	std::vector<float> recorded;
};

void* Project0_main_loop(void*)
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

	device_setup.bufferSize = 128;

	adm.setAudioDeviceSetup(device_setup, false);

	std::cerr << "Please configure your ASIO:\n";
	getchar();

	std::cerr << "Play sound? [y/n]\n";
	{
		std::string s;

		do {

			std::getline(std::cin, s);

			if (s == "y") {
				tester->set_play_sound(true);
				break;
			} else if (s == "n") {
				tester->set_play_sound(false);
				break;
			}
		} while (true);
	}

	adm.addAudioCallback(tester.get());
	std::cerr << "Running...\n";
	std::this_thread::sleep_for(25s);
	adm.removeAudioCallback(tester.get());

	return NULL;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
	auto message_manager = juce::MessageManager::getInstance();
	message_manager->callFunctionOnMessageThread(Project0_main_loop, NULL);

	// should be called manually -- juce::ScopedJuceInitialiser_GUI will do
	// juce::DeletedAtShutdown::deleteAll();

	juce::MessageManager::deleteInstance();
}
