#pragma once

#include "Config.hpp"
#include "RingBuffer.hpp"
#include <bitset>
#include <vector>
namespace Athernet {

template <typename T> class PHY_Sender {
	using Signal = std::vector<T>;

public:
	PHY_Sender()
		: config { Athernet::Config::get_instance() }
	{
	}

	void send_frame(std::vector<int> frame)
	{
		assert(frame.size() <= config.get_symbol_per_phy_frame());
		// frame date is zero-padded to a fixed length
		Signal signal;
		append_preamble(signal);
		append_uint_8(frame.size(), signal);
		for (const auto& bit : frame) {
			assert(bit == 0 || bit == 1);
			if (bit) {
				append_1(signal);
			} else {
				append_0(signal);
			}
		}
		for (int i = static_cast<int>(frame.size()); i < config.get_symbol_per_phy_frame(); ++i) {
			append_0(signal);
		}
		append_crc8(frame, signal);
		append_silence(signal);
		append_silence(signal);
		auto result = m_send_buffer.push(signal);
		assert(result);
	}

	int fetch_stream(float* buffer, int count)
	{
		return m_send_buffer.pop_with_conversion_to_float(buffer, count);
	}

private:
	void append_preamble(Signal& signal) { append_vec(config.get_preamble(Athernet::Tag<T>()), signal); }

	void append_0(Signal& signal) { append_vec(config.get_carrier_0(Athernet::Tag<T>()), signal); }
	void append_1(Signal& signal) { append_vec(config.get_carrier_1(Athernet::Tag<T>()), signal); }

	void append_uint_8(size_t x, Signal& signal)
	{
		assert(x < 256);

		for (int i = 0; i < 8; ++i) {
			if (x & (1ULL << i)) {
				append_1(signal);
			} else {
				append_0(signal);
			}
		}
	}

	void append_crc8(std::vector<int>& frame, Signal& signal)
	{
		std::vector<int> bits(8 + config.get_symbol_per_phy_frame() + 8);
		int size_start = 0;
		int data_start = 8;
		int crc_start = 8 + config.get_symbol_per_phy_frame();

		for (int i = 0; i < 8; ++i) {
			if (frame.size() & (1ULL << i)) {
				bits[size_start + i] = 0;
			}
		}
		for (int i = 0; i < frame.size(); ++i) {
			bits[data_start + i] = frame[i];
		}
		// zero-padded
		for (int i = 0; i < crc_start; ++i) {
			if (bits[i]) {
				for (int offset = 0; offset < 9; ++offset) {
					bits[i + offset] ^= config.get_crc8()[offset];
				}
			}
		}
		for (int i = 0; i < 8; ++i) {
			if (bits[crc_start + i]) {
				append_1(signal);
			} else {
				append_0(signal);
			}
		}
	}

	void append_silence(Signal& signal) { append_vec(silence, signal); }

	void append_vec(const Signal& from, Signal& to)
	{
		std::copy(std::begin(from), std::end(from), std::back_inserter(to));
	}

private:
	Athernet::RingBuffer<T> m_send_buffer;
	Athernet::Config& config;
	Signal silence = Signal(200);
};
}
