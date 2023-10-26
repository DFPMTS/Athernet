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
	using Samples = std::vector<T>;
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

	struct CSI {
		float scale;
		int shift;
	};

	void frame_extract_loop()
	{
		T max_val = 0;
		int max_pos = -1;
		int saved_start = 0;
		int symbols_to_collect = 0;
		PhyRecvState next_state = PhyRecvState::INVALID_STATE;
		Bits length;
		Bits bits;
		Samples samples;
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

					if (greater_than(mul_large_small(dot_product_square, 6, Tag<T>()),
							preamble_received_energy_product, Tag<T>())) {
						if (dot_product_square > max_val) {
							max_val = dot_product_square;
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
					std::cerr << "head>  " << m_recv_buffer.show_head() << "\n";
					start = 0;
					saved_start = 0;
					max_pos = -1;
					max_val = 0;

					samples.clear();
					symbols_to_collect = 2;
					state = PhyRecvState::COLLECT_SAMPLES;
					next_state = PhyRecvState::ESTIMATE_CHANNEL;
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
			} else if (state == PhyRecvState::ESTIMATE_CHANNEL) {
				// * y1 = h_11 * cosx - h_21 * cosx
				Samples y1;
				// * y2 = h_11 * cosx + h_21 * cosx
				Samples y2;
				for (int i = 0; i < config.get_symbol_length(); ++i) {
					y1.push_back(samples[i]);
					y2.push_back(samples[config.get_symbol_length() + i]);
				}

				Samples h_11_cosx(config.get_symbol_length());
				for (int i = 0; i < config.get_symbol_length(); ++i) {
					h_11_cosx[i] = (y1[i] + y2[i]) / 2;
				}

				Samples h_21_cosx(config.get_symbol_length());
				for (int i = 0; i < config.get_symbol_length(); ++i) {
					h_21_cosx[i] = (y2[i] - y1[i]) / 2;
				}

				h_11_cosx = y1;
				h_21_cosx = y2;

				for (auto x : h_11_cosx) {
					std::cerr << x << ", ";
				}
				std::cerr << "\n";

				h1 = phase_detect(h_11_cosx);

				for (auto x : h_21_cosx) {
					std::cerr << x << ", ";
				}
				std::cerr << "\n";

				h2 = phase_detect(h_21_cosx);
				start += 50;
				state = PhyRecvState::GET_LENGTH;
			} else if (state == PhyRecvState::GET_LENGTH) {
				std::cerr << "start:  " << start << "\n";
				bits.clear();
				symbols_to_collect = config.get_phy_frame_length_num_bits();
				state = PhyRecvState::COLLECT_BITS;
				next_state = PhyRecvState::GET_PAYLOAD;
			} else if (state == PhyRecvState::GET_PAYLOAD) {
				static int counter = 0;
				std::cerr << "Begin Get Data" << (++counter) << "\n";

				// move to length
				std::swap(length, bits);
				int payload_length = 0;
				for (int i = 0; i < length.size(); ++i) {
					if (length[i])
						payload_length += (1 << i);
				}
				std::cerr << "Length: " << payload_length << "\n";
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
				symbols_to_collect = payload_length + config.get_crc_residual_length();
				state = PhyRecvState::COLLECT_BITS;
				next_state = PhyRecvState::CHECK_PAYLOAD;
			} else if (state == PhyRecvState::CHECK_PAYLOAD) {
				std::cerr << "Begin checking\n";
				if (crc_check(bits)) {
					// good to go
					for (int i = 0; i < config.get_crc_residual_length(); ++i) {
						bits.pop_back();
					}
					good++;
					// dispatch normal frame to recv_queue, and coded frame to decoder_queue
					if (!bits[bits.size() - 1]) {
						bits.pop_back();
						m_recv_queue.push(std::move(bits));

					} else {
						bits.pop_back();
						m_decoder_queue.push(std::move(bits));
					}

				} else {
					// discard
					std::cerr << "                                    ";
					std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Bad frame!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
					start = saved_start;
				}

				state = PhyRecvState::WAIT_HEADER;
			} else if (state == PhyRecvState::COLLECT_SAMPLES) {
				if (!symbols_to_collect) {
					state = next_state;
				} else {
					symbols_to_collect -= get_samples(symbols_to_collect, samples);
					if (symbols_to_collect) {
						std::this_thread::yield();
					}
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
			std::cerr << "     Bad:           " << received - good << "\n";
		}
	}

	CSI phase_detect(Samples signal)
	{
		static Samples ref(config.get_carriers(Tag<T>())[0][0]);
		float auto_signal = 0;
		float auto_ref = 0;
		float sum = 0;
		for (int i = 0; i < signal.size(); ++i) {
			auto_signal += signal[i] * signal[i];
			auto_ref += ref[i] * ref[i];
		}
		float max_corr = 0;

		int max_offset = 0;
		for (int offset = 0; offset < 48000 / 6000; ++offset) {
			float corr = 0;
			for (int i = 0; i < signal.size(); ++i) {
				int j = offset + i;
				if (j >= signal.size())
					j -= signal.size();
				corr += signal[j] * ref[i];
			}
			if (corr > max_corr) {
				max_corr = corr;
				max_offset = offset;
			}
		}
		std::cerr << "Best shift:  " << max_offset << "\n";
		return CSI { sqrtf(auto_signal / auto_ref), max_offset };
	}

	std::pair<float, float> phase_detect_useless(Samples signal)
	{
		static Samples ref(config.get_carriers(Tag<T>())[0][0]);
		static Samples axis(config.get_axes(Tag<T>())[0]);

		float auto_signal = 0;
		float auto_ref = 0;
		float sum = 0;
		for (int i = 0; i < signal.size(); ++i) {
			sum += signal[i] * ref[i];
			auto_signal += signal[i] * signal[i];
			auto_ref += ref[i] * ref[i];
		}

		float scale = sqrtf(auto_ref / auto_signal);
		float abs_shift = acosf(sum / sqrtf(auto_ref * auto_signal));

		for (int i = 0; i < signal.size(); ++i) {
			signal[i] *= scale;
		}
		sum = 0;
		auto_signal = 0;
		for (int i = 0; i < signal.size(); ++i) {
			sum += axis[i] * (signal[i] - ref[i]);
			auto_signal += signal[i] * signal[i];
		}
		float shift = asinf(sum / sqrtf(auto_ref * auto_signal));

		if (shift < 0) {
			shift = (shift + (-abs_shift)) / 2;
		} else {
			shift = (shift + abs_shift) / 2;
		}
		return std::make_pair(scale, shift);
	}

	int get_samples(int count, Samples& samples)
	{
		int step = config.get_phy_frame_CP_length() + config.get_symbol_length()
			+ config.get_phy_frame_CP_length();
		int rightmost_pos = m_recv_buffer.size() - step + 1;
		int collected_count = 0;

		// std::cerr << "Dump:\n";
		// for (int i = start - 10 * step; i < start + 10 * step; ++i) {
		// 	std::cerr << m_recv_buffer[i] << ", ";
		// }
		// std::cerr << "\n";

		for (int i = start; i < rightmost_pos && collected_count < count; i += step) {
			for (int j = 0; j < config.get_symbol_length(); ++j) {
				samples.push_back(m_recv_buffer[i + config.get_phy_frame_CP_length() + j]);
			}
			start += step;
			if (++collected_count >= count)
				break;
		}

		return collected_count;
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
		int step = config.get_phy_frame_CP_length() + config.get_symbol_length()
			+ config.get_phy_frame_CP_length();
		int rightmost_pos = m_recv_buffer.size() - step * 2 + 1;
		int converted_count = 0;

		for (int i = start; i < rightmost_pos && converted_count < count; i += step * 2, start += step * 2) {
			for (const auto& carrier : config.get_carriers(Tag<T>())) {

				Samples y1;
				for (int j = 0; j < config.get_symbol_length(); ++j) {
					y1.push_back(m_recv_buffer[i + config.get_phy_frame_CP_length() + j]);
				}

				Samples y2;
				for (int j = 0; j < config.get_symbol_length(); ++j) {
					y2.push_back(m_recv_buffer[i + step + config.get_phy_frame_CP_length() + j]);
				}

				Samples z1 = vec_add(apply_conjugate(y1, h1), apply_conjugate(y2, h2));
				Samples z2 = vec_sub(apply_conjugate(y1, h2), apply_conjugate(y2, h1));

				int s1 = -1, s2 = -1;
				T dot_product = 0;
				for (int j = 0; j < config.get_symbol_length(); ++j) {
					dot_product += z1[j] * carrier[0][j];
				}
				s1 = (dot_product < 0);
				bits.push_back(s1);

				dot_product = 0;
				for (int j = 0; j < config.get_symbol_length(); ++j) {
					dot_product += z2[j] * carrier[0][j];
				}
				s2 = (dot_product < 0);
				bits.push_back(s2);

				if ((converted_count += 2) >= count) {
					if (converted_count > count) {
						bits.pop_back();
						--converted_count;
					}
					break;
				}
			}
		}

		return converted_count;
	}

	Samples vec_add(const Samples& x, const Samples& y)
	{
		assert(x.size() == y.size());
		Samples ret;
		for (int i = 0; i < x.size(); ++i) {
			ret.push_back(x[i] + y[i]);
		}
		return ret;
	}

	Samples vec_sub(const Samples& x, const Samples& y)
	{
		assert(x.size() == y.size());
		Samples ret;
		for (int i = 0; i < x.size(); ++i) {
			ret.push_back(x[i] - y[i]);
		}
		return ret;
	}

	Samples apply_conjugate(const Samples& y, CSI h)
	{
		Samples ret;
		int shift = h.shift <= 4 ? h.shift : y.size() - h.shift;
		for (int i = 0; i < y.size(); ++i) {
			int j = i + shift;
			if (j >= y.size())
				j -= y.size();

			ret.push_back(h.scale * y[j]);
		}
		return ret;
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
		ESTIMATE_CHANNEL,
		GET_LENGTH,
		GET_PAYLOAD,
		CHECK_PAYLOAD,
		COLLECT_BITS,
		COLLECT_SAMPLES,
		INVALID_STATE
	};

	Athernet::Config& config;
	Athernet::RingBuffer<T>& m_recv_buffer;
	Athernet::SyncQueue<Frame>& m_recv_queue;
	Athernet::SyncQueue<Frame>& m_decoder_queue;
	std::thread worker;
	std::atomic_bool running;
	int start;
	CSI h1, h2;
	PhyRecvState state = PhyRecvState::WAIT_HEADER;
};
}