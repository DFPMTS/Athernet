#include "Config.hpp"
#include "RingBuffer.hpp"
#include <atomic>
#include <thread>
#include <vector>

namespace Athernet {
template <typename T> class FrameExtractor {
public:
	FrameExtractor(Athernet::RingBuffer<T>& recv_buffer)
		: config { Athernet::Config::get_instance() }
		, m_recv_buffer { recv_buffer }
	{
		running.store(true);
		worker = std::thread(&FrameExtractor::frame_extract_loop, this);
	};

	~FrameExtractor()
	{
		running.store(false);
		worker.join();
	};

private:
	using SoftUInt64 = std::pair<uint32_t, uint32_t>;
	using Bits = std::vector<int>;

	void frame_extract_loop()
	{
		T max_val = 0;
		int max_pos = -1;
		bool confirmed = false;
		int saved_start = 0;

		while (running.load()) {
			if (state == PhyRecvState::WAIT_HEADER) {
				for (int i = start, buffer_size = m_recv_buffer.size();
					 i < buffer_size - config.get_preamble_length(); ++i, ++start) {
					T dot_product = 0;
					T received_energy = 0;
					for (int j = 0; j < config.get_preamble_length(); ++j) {
						dot_product += mul_small(
							m_recv_buffer[j], config.get_preamble(Athernet::Tag<T>())[j], Tag<T>());
						received_energy += mul_small(m_recv_buffer[j], m_recv_buffer[j], Tag<T>());
					}
					if (dot_product < 0)
						continue;
					// now we need to square dot_product, use two int32 to store result

					auto dot_product_square = mul_large(dot_product, dot_product, Tag<T>());

					auto preamble_received_energy_product
						= mul_large(config.get_preamble_energy(Tag<T>()), received_energy, Tag<T>());

					if (greater_than(mul_4(dot_product_square, Tag<T>()), preamble_received_energy_product,
							Tag<T>())) {
						if (dot_product > max_val) {
							max_val = dot_product;
							max_pos = i;
						}
					}

					if (max_pos != -1 && i - max_pos > config.get_preamble_length()) {
						confirmed = true;
						break;
					}
				}

				if (confirmed) {
					m_recv_buffer.discard(max_pos + config.get_preamble_length());
					start = 0;
					saved_start = 0;
					state = PhyRecvState::GET_DATA;
				} else {
					if (max_pos != -1) {
						// discard everything until max_pos
						m_recv_buffer.discard(max_pos);
						max_pos = 0;
						start -= max_pos;
					} else {
						m_recv_buffer.discard(start);
						start = 0;
					}
				}

			} else if (state == PhyRecvState::GET_DATA) {
				Bits length, bits;

				// first bits is the "adviced" payload length
				// collect payload length
				collect_bits(config.get_phy_frame_length_num_bits(), length);

				int payload_length = 0;
				for (int i = 0; i < length.size(); ++i) {
					if (bits[i])
						payload_length += (1 << i);
				}
				std::cerr << "Length: " << payload_length << "\n";
				// discard bad frame
				if (payload_length > config.get_phy_frame_payload_symbol_limit()) {
					state = PhyRecvState::WAIT_HEADER;
					// restore start
					start = saved_start;
					continue;
				}

				// collect data and crc residual
				collect_bits(payload_length + config.get_crc_length(), bits);

				if (crc_check(length, bits)) {
					// good to go
					for (int i = 0; i < config.get_crc_length(); ++i) {
						bits.pop_back();
					}
					// ! Just show it
					for (const auto& x : bits) {
						std::cerr << x;
					}
					std::cerr << "\n";
				} else {
					// discard
					start = saved_start;
				}

				state = PhyRecvState::WAIT_HEADER;
			}
		}
	}

	bool crc_check(Bits length, Bits bits)
	{
		for (int i = 0; i < config.get_crc_length(); ++i) {
			length.push_back(0);
		}
		for (int i = 0; i < length.size(); ++i) {
			if (length[i])
				for (int j = 0; j < config.get_crc_length(); ++j) {
					length[i + j] ^= config.get_crc()[j];
				}
		}

		for (int i = 0; i < config.get_crc_length(); ++i) {
			bits[i] ^= length[length.size() - config.get_crc_length() + i];
		}

		for (int i = 0; i < bits.size() - config.get_crc_length(); ++i) {
			if (bits[i]) {
				for (int j = 0; j < config.get_crc_length(); ++j) {
					bits[i + j] ^= config.get_crc()[j];
				}
			}
		}
		for (int i = static_cast<int>(bits.size()) - config.get_crc_length();
			 i < static_cast<int>(bits.size()); ++i) {
			if (bits[i]) {
				return false;
			}
		}
		return true;
	}

	void collect_bits(int symbols_to_collect, Bits& bits)
	{
		symbols_to_collect -= to_bits(symbols_to_collect, bits);
		while (symbols_to_collect) {
			std::this_thread::yield();
			symbols_to_collect -= to_bits(symbols_to_collect, bits);
		}
	}

	int to_bits(int count, Bits& bits)
	{
		int rightmost_pos = std::min(m_recv_buffer.size() - config.get_symbol_length(),
			start + mul_small(config.get_symbol_length(), count, Tag<int>()));
		int converted_count = 0;
		for (int i = start; i < rightmost_pos;
			 i += config.get_symbol_length(), start += config.get_symbol_length()) {

			T dot_product = 0;
			for (int j = 0; j < config.get_symbol_length(); ++j) {
				dot_product
					+= mul_small(m_recv_buffer[j], config.get_carrier_0(Athernet::Tag<T>())[j], Tag<T>());
			}
			if (dot_product > 0) {
				bits.push_back(0);
			} else {
				bits.push_back(1);
			}

			++converted_count;
		}
		return converted_count;
	}

	T mul_small(T x, T y, Tag<int>)
	{

		if (x < y)
			std::swap(x, y);
		T ret = 0;
		while (y) {
			if (y & 1)
				ret += x;
			x <<= 1;
		}
		return ret;
	}

	T mul_small(T x, T y, Tag<float>) { return x * y; }

	SoftUInt64 mul_large(T signed_x, T signed_y, Tag<int>)
	{
		static constexpr uint32_t LOW_16_MASK = (1 << 16) - 1;

		uint32_t x = static_cast<uint32_t>(signed_x), y = static_cast<uint32_t>(signed_y);
		uint32_t upper = 0, lower = 0;

		/*
					a        b
			x  |--------|--------|
				   16       16

					c        d
			y  |--------|--------|
				   16       16
		*/

		uint32_t a = x >> 16;
		uint32_t b = x & ((1 << 16) - 1);
		uint32_t c = y >> 16;
		uint32_t d = y & ((1 << 16) - 1);

		uint32_t ac = mul_small(a, c, Tag<T>());
		uint32_t bd = mul_small(b, d, Tag<T>());
		uint32_t bc = mul_small(b, c, Tag<T>());
		uint32_t ad = mul_small(a, d, Tag<T>());

		uint32_t bc_upper30 = bc >> 2;
		uint32_t ad_upper30 = ad >> 2;
		uint32_t bd_low16_upper30 = (bd >> 16) >> 2;

		uint32_t bc_low2 = bc & 3;
		uint32_t ad_low2 = ad & 3;
		uint32_t bd_low16_low2 = (bd >> 16) & 3;

		upper += ac;
		upper += ((bc_upper30 + ad_upper30 + bd_low16_upper30) + (((bc_low2 + ad_low2 + bd_low16_low2) >> 2)))
			>> 14;

		lower += bd & LOW_16_MASK;
		lower += (bc + ad + ((bd) >> 16)) << 16;

		return std::make_pair(upper, lower);
	}

	SoftUInt64 mul_large(T x, T y, Tag<float>) { return x * y; }

	SoftUInt64 add_large(SoftUInt64 x, SoftUInt64 y, Tag<int>)
	{
		auto [x_upper, x_lower] = x;
		auto [y_upper, y_lower] = y;
		uint32_t upper = 0, lower = 0;
		lower = x_lower + y_lower;
		uint32_t x_lower_upper31 = x_lower >> 1;
		uint32_t y_lower_upper31 = y_lower >> 1;
		uint32_t x_lower_low1 = x_lower & 1;
		uint32_t y_lower_low1 = y_lower & 1;

		upper = x_upper + y_upper + ((x_lower_upper31 + y_lower_upper31) >> 31);
	}

	T add_large(T x, T y, Tag<float>) { return x + y; }

	bool greater_than(SoftUInt64 x, SoftUInt64 y, Tag<int>)
	{
		return ((x.first > y.first) || ((x.first == y.first) && (x.second >= y.second)));
	}

	bool greater_than(T x, T y, Tag<float>) { return x > y; }

	SoftUInt64 mul_4(SoftUInt64 x, Tag<int>) { return std::make_pair(x.first << 2, x.second << 2); }

	SoftUInt64 mul_4(T x, Tag<float>) { return x * 4; }

	enum class PhyRecvState { WAIT_HEADER, GET_DATA };

	Athernet::Config& config;
	Athernet::RingBuffer<T>& m_recv_buffer;
	std::thread worker;
	std::atomic_bool running;
	int start;
	PhyRecvState state = PhyRecvState::WAIT_HEADER;
};
}