#include "Config.hpp"
#include "Protocol_Control.hpp"
#include "RingBuffer.hpp"
#include "SyncQueue.hpp"
#include <atomic>
#include <thread>
#include <vector>

namespace Athernet {
template <typename T> class FrameExtractor {
	using SoftUInt64 = std::pair<uint32_t, uint32_t>;
	using Bits = std::vector<int>;
	using Frame = std::vector<int>;

public:
	FrameExtractor(Athernet::RingBuffer<T>& recv_buffer, Athernet::SyncQueue<MacFrame>& recv_queue,
		Athernet::SyncQueue<Frame>& decoder_queue, Protocol_Control& mac_control)
		: config { Athernet::Config::get_instance() }
		, m_recv_buffer { recv_buffer }
		, m_recv_queue { recv_queue }
		, m_decoder_queue { decoder_queue }
		, control { mac_control }
	{
		running.store(true);
		start = 0;
		worker = std::thread(&FrameExtractor::frame_extract_loop, this);
		std::cerr << "Worker Started\n";
	};

	~FrameExtractor()
	{
		std::cerr << "Called\n";
		running.store(false);
		worker.join();
		std::cerr << "End\n";
		m_recv_buffer.dump("received.txt");
	};

private:
	double to_double(SoftUInt64 x) { return (double)((((unsigned long long)x.first) << 32) + x.second); }

	int LEN = 0;

	void frame_extract_loop()
	{
		T max_val = 0;
		int max_pos = -1;
		int saved_start = 0;
		int symbols_to_collect = 0;
		PhyRecvState next_state = PhyRecvState::INVALID_STATE;
		Bits length;
		Bits bits;
		int received = 0;
		int good = 0;
		while (running.load()) {
			if (state == PhyRecvState::WAIT_HEADER) {
				if (start > m_recv_buffer.size() - config.get_preamble_length()) {
					std::this_thread::yield();
					continue;
				}

				bool confirmed = false;
				for (int i = start, buffer_size = m_recv_buffer.size();
					 i <= buffer_size - config.get_preamble_length(); ++i, ++start) {
					T dot_product = 0;
					T received_energy = 0;
					for (int j = 0; j < config.get_preamble_length(); ++j) {
						dot_product += mul_small(
							m_recv_buffer[i + j], config.get_preamble(Athernet::Tag<T>())[j], Tag<T>());

						received_energy += mul_small(m_recv_buffer[i + j], m_recv_buffer[i + j], Tag<T>());
					}

					if (dot_product < 0)
						continue;

					// now we need to square dot_product, use two int32 to store result
					auto dot_product_square = mul_large(dot_product, dot_product, Tag<T>());

					auto preamble_received_energy_product
						= mul_large(config.get_preamble_energy(Tag<T>()), received_energy, Tag<T>());

					if (greater_than(mul_large_small(dot_product_square, 2, Tag<T>()),
							preamble_received_energy_product, Tag<T>())) {
						if (dot_product > max_val) {
							max_val = dot_product;
							max_pos = i;
							// std::cerr << "Greater:  " << max_val << "\n";
							// std::cerr << preamble_received_energy_product.first << " "
							// 		  << preamble_received_energy_product.second << "\n";
							// auto [h, l]
							// 	= mul_large(config.get_preamble_energy(Tag<T>()), received_energy, Tag<T>());
							// std::cerr << config.get_preamble_energy(Tag<T>()) << " " << received_energy
							// 		  << "\n";
							// std::cerr << h << " " << l << "\n";
							// std::cerr << "--------------------------------------------------------";

							// std::cerr << to_double(dot_product_square) << " "
							// 		  << to_double(preamble_received_energy_product) << "\n";
							// std::cerr << dot_product << " " << received_energy << " "
							// 		  << config.get_preamble_energy(Tag<T>()) << "\n";
						}
					}

					if (max_pos != -1 && i - max_pos > config.get_preamble_length()) {
						confirmed = true;
						break;
					}
				}
				if (confirmed) {
					received++;
					m_recv_buffer.discard(max_pos + config.get_preamble_length());
					// std::cerr << "head>  " << m_recv_buffer.show_head() << "\n";
					start = 0;
					saved_start = 0;
					max_pos = -1;
					max_val = 0;
					state = PhyRecvState::GET_LENGTH;
				} else {
					if (max_pos != -1) {
						// discard everything until max_pos
						m_recv_buffer.discard(max_pos);
						start -= max_pos;
						max_pos = 0;
					} else {
						m_recv_buffer.discard(start);
						start = 0;
					}
				}
			} else if (state == PhyRecvState::GET_LENGTH) {
				bits.clear();
				symbols_to_collect = config.get_phy_frame_length_num_bits();

				state = PhyRecvState::COLLECT_BITS;
				next_state = PhyRecvState::GET_PAYLOAD;
			} else if (state == PhyRecvState::GET_PAYLOAD) {
				static int counter = 0;
				// std::cerr << "Begin Get Data" << (++counter) << "\n";

				// move to length
				std::swap(length, bits);
				int payload_length = 0;
				for (int i = 0; i < length.size(); ++i) {
					if (length[i])
						payload_length += (1 << i);
				}
				// std::cerr << "Length: " << payload_length << "\n";
				// discard bad frame
				if (payload_length > config.get_phy_frame_payload_symbol_limit() || payload_length < 32) {
					state = PhyRecvState::WAIT_HEADER;
					// restore start
					start = saved_start;
					// discard
					std::cerr << "                                    ";
					std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Bad frame!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
					continue;
				}

				bits.clear();
				// collect data and crc residual
				symbols_to_collect
					= payload_length + config.get_crc_residual_length() + config.get_crc_residual_length();
				state = PhyRecvState::COLLECT_BITS;
				next_state = PhyRecvState::CHECK_PAYLOAD;
			} else if (state == PhyRecvState::CHECK_PAYLOAD) {
				if (crc_check(bits, 0, 32 + config.get_crc_residual_length())) {
					// good header
					if (crc_check(bits, 32 + config.get_crc_residual_length(), bits.size())) {
						// good to go
						for (int i = 0; i < config.get_crc_residual_length(); ++i) {
							bits.pop_back();
						}
						good++;

						// dispatch normal frame to recv_queue, and coded frame to decoder_queue
						if (!bits[bits.size() - 1]) {
							bits.pop_back();
							MacFrame frame(bits, 0);
							m_recv_queue.push(std::move(frame));

						} else {
							bits.pop_back();
							m_decoder_queue.push(std::move(bits));
						}
					} else {
						// discard
						std::cerr << "                                    ";
						std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Bad"
									 "frame!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
						MacFrame frame(bits, 1);
						m_recv_queue.push(std::move(frame));
						start = saved_start;
					}
				} else {
					std::cerr << "                                    ";
					std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Bad"
								 "frame!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
					start = saved_start;
				}

				state = PhyRecvState::WAIT_HEADER;
			} else if (state == PhyRecvState::COLLECT_BITS) {
				if (!symbols_to_collect) {
					state = next_state;
				} else {
					symbols_to_collect -= to_bits(symbols_to_collect, bits);
					if (symbols_to_collect) {
						std::this_thread::yield();
					}
				}
			} else if (state == PhyRecvState::INVALID_STATE) {
				std::cerr << "[PHY_FrameExtractor] Invalid state!\n";
				assert(0);
			}
		}

		if constexpr (Athernet::DUMP_RECEIVED) {
			std::cerr << "--------[FrameExtractor]--------\n";
			std::cerr << "     Received:      " << received << "\n";
			std::cerr << "     Bad:           " << received - good << "\n";
		}
	}

	bool crc_check(Bits bits, int start, int end)
	{
		for (int i = start; i < end - config.get_crc_residual_length(); ++i) {
			if (bits[i]) {
				for (int j = 0; j < config.get_crc_length(); ++j) {
					bits[i + j] ^= config.get_crc()[j];
				}
			}
		}

		bool is_zero = true;
		for (int i = end - config.get_crc_residual_length(); i < end; ++i) {
			if (bits[i]) {
				is_zero = false;
			}
		}
		return is_zero;
	}

	int to_bits(int count, Bits& bits)
	{
		int rightmost_pos
			= m_recv_buffer.size() - config.get_symbol_length() - config.get_phy_frame_CP_length() + 1;
		int converted_count = 0;

		for (int i = start; i < rightmost_pos && converted_count < count;
			 i += config.get_phy_frame_CP_length() + config.get_symbol_length(),
				 start += config.get_phy_frame_CP_length() + config.get_symbol_length()) {
			for (const auto& carrier : config.get_carriers(Tag<T>())) {
				T dot_product = 0;
				for (int j = 0; j < config.get_symbol_length(); ++j) {
					dot_product += mul_small(
						m_recv_buffer[i + config.get_phy_frame_CP_length() + j], carrier[0][j], Tag<T>());
				}
				if (dot_product > 0) {
					bits.push_back(0);
				} else {
					bits.push_back(1);
				}
				if (++converted_count >= count)
					break;
			}
		}

		return converted_count;
	}

	int mul_small(int x, int y)
	{

		if (x < y)
			std::swap(x, y);
		int ret = 0;
		if (y < 0) {
			x = -x;
			y = -y;
		}
		while (y) {
			if (y & 1)
				ret += x;
			x += x;
			y >>= 1;
		}
		return ret;
	}

	T mul_small(T x, T y, Tag<int>)
	{

		if (x < y)
			std::swap(x, y);
		T ret = 0;
		if (y < 0) {
			x = -x;
			y = -y;
		}
		while (y) {
			if (y & 1)
				ret += x;
			x += x;
			y >>= 1;
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

	float mul_large(T x, T y, Tag<float>) { return x * y; }

	SoftUInt64 add_large(SoftUInt64 x, SoftUInt64 y, Tag<int>)
	{
		auto [x_upper, x_lower] = x;
		auto [y_upper, y_lower] = y;
		uint32_t upper = 0, lower = 0;

		uint32_t x_lower_upper31 = x_lower >> 1;
		uint32_t y_lower_upper31 = y_lower >> 1;
		uint32_t x_lower_low1 = x_lower & 1;
		uint32_t y_lower_low1 = y_lower & 1;

		lower = x_lower + y_lower;
		upper = x_upper + y_upper + ((x_lower_upper31 + y_lower_upper31) >> 31);
		return std::make_pair(upper, lower);
	}

	T add_large(T x, T y, Tag<float>) { return x + y; }

	bool greater_than(SoftUInt64 x, SoftUInt64 y, Tag<int>)
	{
		return ((x.first > y.first) || ((x.first == y.first) && (x.second >= y.second)));
	}

	bool greater_than(T x, T y, Tag<float>) { return x > y; }

	SoftUInt64 mul_large_small(SoftUInt64 x, int y, Tag<int>)
	{
		SoftUInt64 ret = std::make_pair(0U, 0U);
		while (y) {
			if (y & 1)
				ret = add_large(ret, x, Tag<int>());
			y >>= 1;
			x.first <<= 1;
			x.second <<= 1;
		}
		return ret;
	}

	float mul_large_small(T x, int y, Tag<float>) { return x * y; }

	enum class PhyRecvState {
		WAIT_HEADER,
		GET_LENGTH,
		GET_PAYLOAD,
		CHECK_PAYLOAD,
		COLLECT_BITS,
		INVALID_STATE
	};

	Athernet::Config& config;
	Athernet::RingBuffer<T>& m_recv_buffer;
	Athernet::SyncQueue<MacFrame>& m_recv_queue;
	Athernet::SyncQueue<Frame>& m_decoder_queue;
	Protocol_Control& control;

	std::thread worker;
	std::atomic_bool running;
	int start;
	PhyRecvState state = PhyRecvState::WAIT_HEADER;
};
}