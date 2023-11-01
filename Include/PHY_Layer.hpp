#pragma once

#include "JuceHeader.h"
#include "PHY_Receiver.hpp"
#include "PHY_Sender.hpp"

namespace Athernet {

template <typename T> class PHY_Layer : public juce::AudioIODeviceCallback {
public:
	PHY_Layer()
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

	~PHY_Layer() { }

private:
	Athernet::Config& config;
	Athernet::PHY_Receiver<T> m_receiver;
	Athernet::PHY_Sender<T> m_sender;
};
}