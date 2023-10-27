#include "Config.hpp"
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
	FrameExtractor(Athernet::RingBuffer<T>& recv_buffer, Athernet::SyncQueue<Frame>& recv_queue,
		Athernet::SyncQueue<Frame>& decoder_queue)
		: config { Athernet::Config::get_instance() }
		, m_recv_buffer { recv_buffer }
		, m_recv_queue { recv_queue }
		, m_decoder_queue { decoder_queue }
	{
		running.store(true);
		start = 0;
		worker = std::thread(&FrameExtractor::frame_extract_loop, this);
		std::cerr << "Worker Started\n";
		remove((NOTEBOOK_DIR + "recv.txt"s).c_str());
	};

	~FrameExtractor()
	{
		std::cerr << "Called\n";
		running.store(false);
		worker.join();
		std::cerr << "End\n";
		auto recv_fd = fopen((NOTEBOOK_DIR + "recv.txt"s).c_str(), "w");
		Frame frame;
		while (collected.pop(frame)) {
			for (int i = 0; i < 250; ++i) {
				fprintf(recv_fd, "%d", frame[i]);
			}
			// for (int i = 100 + config.get_crc_residual_length();
			// 	 i < frame.size() - config.get_crc_residual_length(); ++i) {
			// 	fprintf(recv_fd, "%d", frame[i]);
			// }
			fprintf(recv_fd, "\n");
			fflush(recv_fd);
		}
		fclose(recv_fd);
		// m_recv_buffer.dump("received.txt");
	};

private:
	double to_double(SoftUInt64 x) { return (double)((((unsigned long long)x.first) << 32) + x.second); }

	int LEN = 0;

	T pow(T a, int b)
	{
		T ret = 1;
		while (b) {
			if (b & 1)
				ret *= a;
			a *= a;
			b >>= 1;
		}
		return ret;
	}

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
		int corrected = 0;
		T normed_corr = 0;
		std::queue<T> norm_window;
		T window_sum = 0;
		int window_size = 50;
		int retry = 0;
		int segments = 0;
		while (running.load()) {
			if (state == PhyRecvState::WAIT_HEADER) {
				if (start > m_recv_buffer.size() - config.get_preamble_length()) {
					std::this_thread::yield();
					continue;
				}

				bool confirmed = false;
				for (int i = start, buffer_size = m_recv_buffer.size();
					 i <= buffer_size - config.get_preamble_length(); ++i, ++start) {
					double dot_product = 0;
					double received_energy = 0;
					for (int j = 0; j < config.get_preamble_length(); ++j) {
						dot_product += mul_small(
							m_recv_buffer[i + j], config.get_preamble(Athernet::Tag<T>())[j], Tag<T>());

						received_energy += mul_small(m_recv_buffer[i + j], m_recv_buffer[i + j], Tag<T>());
					}

					if (dot_product < 0)
						continue;

					// now we need to square dot_product, use two int32 to store result
					auto dot_product_square = dot_product * dot_product;

					auto preamble_received_energy_product
						= config.get_preamble_energy(Tag<T>()) * received_energy;

					auto corr = dot_product_square / preamble_received_energy_product;
					norm_window.push(corr);
					window_sum += corr;
					if (norm_window.size() > window_size) {
						window_sum -= norm_window.front();
						norm_window.pop();
					}
					normed_corr = pow((2 * (corr / window_sum)), 5);

					if (corr > 0.05) {
						if (corr > max_val) {
							max_val = corr;
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
					received += 1;
					m_recv_buffer.discard(max_pos + config.get_preamble_length());
					std::cerr << "head>  " << m_recv_buffer.show_head() << "\n";
					start = 0;
					saved_start = 0;
					max_pos = -1;
					max_val = 0;
					retry = 0;
					state = PhyRecvState::GET_LENGTH;
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
			} else if (state == PhyRecvState::GET_LENGTH) {
				bits.clear();
				// symbols_to_collect = config.get_phy_frame_length_num_bits();
				segments = 1;
				state = PhyRecvState::GET_PAYLOAD;
				// next_state = PhyRecvState::GET_PAYLOAD;
			} else if (state == PhyRecvState::GET_PAYLOAD) {
				if (!segments) {
					state = PhyRecvState::WAIT_HEADER;
					continue;
				}
				--segments;

				static int counter = 0;
				if (!retry)
					std::cerr << "Begin Get Data" << (++counter) << "\n";

				// move to length
				std::swap(length, bits);
				int payload_length = 250;
				// for (int i = 0; i < length.size(); ++i) {
				// 	if (length[i])
				// 		payload_length += (1 << i);
				// }
				// std::cerr << "Length: " << payload_length << "\n";
				// discard bad frame
				if (payload_length > config.get_phy_frame_payload_symbol_limit() || payload_length < 2) {
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
				// symbols_to_collect = payload_length + config.get_crc_residual_length();//
				symbols_to_collect = payload_length;
				state = PhyRecvState::COLLECT_BITS;
				next_state = PhyRecvState::CHECK_PAYLOAD;
			} else if (state == PhyRecvState::CHECK_PAYLOAD) {
				state = PhyRecvState::GET_PAYLOAD;
				m_decoder_queue.push(bits);
				collected.push(bits);
				continue;

				if (crc_check(bits)) {
					std::cerr << "Good to go\n";
					++good;
					// good to go
					collected.push(bits);

				} else {
					state = PhyRecvState::GET_PAYLOAD;

					bool fixed = 0;
					// for (int i = 0; i < bits.size(); ++i) {
					// 	bits[i] ^= 1;
					// 	if (crc_check(bits)) {
					// 		fixed = 1;
					// 		break;
					// 	}
					// 	bits[i] ^= 1;
					// }

					collected.push(bits);
					if (fixed) {
						corrected++;
						std::cerr << "----------------------------------Corrected\n";
						m_decoder_queue.push(bits);
						continue;
					}
					// for (int i = 0; i < bits.size(); ++i) {
					// 	bits[i] ^= 1;
					// 	for (int j = i + 1; j < bits.size(); ++j) {
					// 		bits[j] ^= 1;
					// 		if (crc_check(bits)) {
					// 			fixed = 1;
					// 			break;
					// 		}
					// 		bits[j] ^= 1;
					// 	}
					// 	bits[i] ^= 1;
					// }
					// collected.push(bits);
					// if (fixed) {
					// 	corrected++;
					// 	std::cerr << "Corrected\n";
					// 	continue;
					// } else {
					// 	std::cerr << "                                    ";
					// 	std::cerr
					// 		<< "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Bad frame!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
					// 	start = saved_start;
					// }
					// discard
				}

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
			std::cerr << "     Good:          " << good << "\n";
			std::cerr << "     Corrected:     " << corrected << "\n";
		}
	}

	bool crc_check(Bits bits)
	{
		for (int i = 0; i < bits.size() - config.get_crc_residual_length(); ++i) {
			if (bits[i]) {
				for (int j = 0; j < config.get_crc_length(); ++j) {
					bits[i + j] ^= config.get_crc()[j];
				}
			}
		}

		bool is_zero = true;
		for (int i = static_cast<int>(bits.size()) - config.get_crc_residual_length();
			 i < static_cast<int>(bits.size()); ++i) {
			if (bits[i]) {
				is_zero = false;
			}
		}
		return is_zero;
	}

	int to_bits(int count, Bits& bits)
	{
		int rightmost_pos
			= m_recv_buffer.size() - config.get_silence_length() - config.get_symbol_length() + 1;
		int converted_count = 0;

		for (int i = start; i < rightmost_pos && converted_count < count;
			 i += config.get_silence_length() + config.get_symbol_length(),
				 start += config.get_silence_length() + config.get_symbol_length()) {
			for (const auto& carrier : config.get_carriers(Tag<T>())) {
				T max_val_0 = 0;
				T max_val_1 = 0;
				T sum_of_square_0 = 0;
				T sum_of_square_1 = 0;
				for (int offset = config.get_silence_length() - config.get_silence_length() / 4;
					 offset < config.get_silence_length() + config.get_silence_length() / 4; ++offset) {
					T dot_product_0 = 0;
					T dot_product_1 = 0;
					for (int j = 0; j < config.get_symbol_length(); ++j) {
						dot_product_0 += mul_small(m_recv_buffer[i + offset + j], carrier[0][j], Tag<T>());
						dot_product_1 += mul_small(m_recv_buffer[i + offset + j], carrier[1][j], Tag<T>());
					}
					max_val_0 = std::max(max_val_0, dot_product_0);
					max_val_1 = std::max(max_val_1, dot_product_1);
					sum_of_square_0 += dot_product_0 * dot_product_0;
					sum_of_square_1 += dot_product_1 * dot_product_1;
				}
				if (max_val_0 > max_val_1) {
					bits.push_back(0);
				} else {
					bits.push_back(1);
				}
				++converted_count;
				if (converted_count >= count)
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
	Athernet::SyncQueue<Frame>& m_recv_queue;
	Athernet::SyncQueue<Frame>& m_decoder_queue;
	std::thread worker;
	std::atomic_bool running;
	int start;
	PhyRecvState state = PhyRecvState::WAIT_HEADER;
	Athernet::SyncQueue<Frame> collected;
};
}