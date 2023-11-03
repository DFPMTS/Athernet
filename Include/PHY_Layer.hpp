#pragma once

#include "JuceHeader.h"
#include "PHY_Receiver.hpp"
#include "PHY_Sender.hpp"
#include <atomic>

namespace Athernet {

template <typename T> class PHY_Layer : public juce::AudioIODeviceCallback {
public:
	PHY_Layer(std::atomic_bool& collision, std::atomic_bool& busy)
		: config { Athernet::Config::get_instance() }
		, m_collision { collision }
		, m_busy { busy }
		, m_sender(collision, busy)
	{
		collision.store(false);
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
			sum += inputChannelData[0][i] * inputChannelData[0][i];
		}
		m_busy.store(false);
		for (int i = windows_size; i < numSamples; ++i) {
			sum += inputChannelData[0][i] * inputChannelData[0][i];
			sum -= inputChannelData[0][i - windows_size] * inputChannelData[0][i - windows_size];
			if (sum / windows_size > 0.001) {
				m_busy.store(true);
				if (sum / windows_size > 0.01) {
					m_collision.store(true);
					break;
				}
			}
		}
		if (!m_collision.load())
			m_receiver.push_stream(inputChannelData[0], numSamples);
	}

	virtual void audioDeviceStopped() override { }

	void send_frame(const std::vector<int>& frame) { m_sender.push_frame(frame); }

	std::vector<std::vector<int>> get_frames() { }

	~PHY_Layer() { }

private:
	Athernet::Config& config;
	std::atomic_bool& m_collision;
	std::atomic_bool& m_busy;

	Athernet::PHY_Receiver<T> m_receiver;
	Athernet::PHY_Sender<T> m_sender;

	int windows_size = 32;
};
}