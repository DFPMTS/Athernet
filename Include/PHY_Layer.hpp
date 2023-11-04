#pragma once

#include "JuceHeader.h"
#include "MAC_Control.hpp"
#include "PHY_Receiver.hpp"
#include "PHY_Sender.hpp"
#include <atomic>

namespace Athernet {

template <typename T> class PHY_Layer : public juce::AudioIODeviceCallback {
public:
	PHY_Layer(MAC_Control& mac_control)
		: config { Athernet::Config::get_instance() }
		, control { mac_control }
		, m_sender(mac_control)
		, m_receiver(mac_control)
	{
		control.collision.store(false);
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

		float sum = 0;
		for (int i = 0; i < windows_size && i < numSamples; ++i) {
			sum += inputChannelData[0][i] * inputChannelData[0][i] * inputChannelData[0][i]
				* inputChannelData[0][i];
		}
		control.busy.store(false);
		control.collision.store(false);
		for (int i = windows_size; i < numSamples; ++i) {
			sum += inputChannelData[0][i] * inputChannelData[0][i] * inputChannelData[0][i]
				* inputChannelData[0][i];
			sum -= inputChannelData[0][i - windows_size] * inputChannelData[0][i - windows_size]
				* inputChannelData[0][i - windows_size] * inputChannelData[0][i - windows_size];
			if (sum / windows_size > 0.001) {
				control.busy.store(true);
				if (sum / windows_size > 2) {
					control.collision.store(true);
					break;
				}
			}
		}
		if (!control.collision.load())
			m_receiver.push_stream(inputChannelData[0], numSamples);
	}

	virtual void audioDeviceStopped() override { }

	void send_frame(const std::vector<int>& frame) { m_sender.push_frame(frame); }

	std::vector<std::vector<int>> get_frames() { }

	~PHY_Layer() { }

private:
	Athernet::Config& config;

	MAC_Control& control;

	Athernet::PHY_Receiver<T> m_receiver;
	Athernet::PHY_Sender<T> m_sender;

	int windows_size = 32;
};
}