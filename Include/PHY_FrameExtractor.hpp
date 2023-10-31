#include "Config.hpp"
#include "RingBuffer.hpp"
#include "SyncQueue.hpp"
#include <atomic>
#include <complex>
#include <thread>
#include <vector>

#define PI acos(-1)

namespace Athernet {
template <typename T> class FrameExtractor {
	using SoftUInt64 = std::pair<uint32_t, uint32_t>;
	using Bits = std::vector<int>;
	using Samples = std::vector<T>;
	using Frame = std::vector<int>;
	using Phase = std::complex<float>;

public:
	FrameExtractor(Athernet::RingBuffer<T>& Rx1_buffer, Athernet::RingBuffer<T>& Rx2_buffer,
		Athernet::SyncQueue<Frame>& recv_queue, Athernet::SyncQueue<Frame>& decoder_queue)
		: config { Athernet::Config::get_instance() }
		, m_Rx1_buffer { Rx1_buffer }
		, m_Rx2_buffer { Rx2_buffer }
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
		m_Rx1_buffer.dump("received.txt");
	};

private:
	struct CSI {
		Phase h[2][2];
	};

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
		Samples Rx1_samples;
		Samples Rx2_samples;
		int received = 0;
		int good = 0;
		while (running.load()) {
			if (state == PhyRecvState::WAIT_HEADER) {
				if (start > m_Rx1_buffer.size() - config.get_preamble_length()) {
					std::this_thread::yield();
					continue;
				}

				bool confirmed = false;
				for (int i = start, buffer_size = m_Rx1_buffer.size();
					 i <= buffer_size - config.get_preamble_length(); ++i, ++start) {
					T dot_product = 0;
					T received_energy = 0;
					for (int j = 0; j < config.get_preamble_length(); ++j) {
						dot_product += mul_small(
							m_Rx1_buffer[i + j], config.get_preamble(Athernet::Tag<T>())[j], Tag<T>());

						received_energy += mul_small(m_Rx1_buffer[i + j], m_Rx1_buffer[i + j], Tag<T>());
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
					m_Rx1_buffer.discard(max_pos + config.get_preamble_length());
					// ! only use Rx1 for synchronizing
					m_Rx2_buffer.discard(max_pos + config.get_preamble_length());
					std::cerr << "head>  " << m_Rx1_buffer.show_head() << "\n";
					start = 0;
					saved_start = 0;
					max_pos = -1;
					max_val = 0;
					ZF_compared_to_ML = 0;
					now_bits = 0;
					Rx1_samples.clear();
					Rx2_samples.clear();
					symbols_to_collect = 2;
					state = PhyRecvState::COLLECT_SAMPLES;
					next_state = PhyRecvState::ESTIMATE_CHANNEL;
				} else {
					if (max_pos != -1) {
						// discard everything until max_pos
						m_Rx1_buffer.discard(max_pos);
						m_Rx2_buffer.discard(max_pos);
						start -= max_pos;
						max_pos = 0;
					} else {
						m_Rx1_buffer.discard(start);
						m_Rx2_buffer.discard(start);
						start = 0;
					}
				}
			} else if (state == PhyRecvState::ESTIMATE_CHANNEL) {
				Samples h11_cosx;
				Samples h21_cosx;
				for (int i = 0; i < config.get_symbol_length(); ++i) {
					h11_cosx.push_back(Rx1_samples[i]);
					h21_cosx.push_back(Rx1_samples[config.get_symbol_length() + i]);
				}

				Samples h12_cosx;
				Samples h22_cosx;
				for (int i = 0; i < config.get_symbol_length(); ++i) {
					h12_cosx.push_back(Rx2_samples[i]);
					h22_cosx.push_back(Rx2_samples[config.get_symbol_length() + i]);
				}
				phase_log.clear();
				csi.clear();
				for (int id = 0; id < config.get_num_carriers(); ++id) {
					CSI sub;
					sub.h[0][0] = phase_detect(h11_cosx, id);
					sub.h[1][0] = phase_detect(h21_cosx, id);
					sub.h[0][1] = phase_detect(h12_cosx, id);
					sub.h[1][1] = phase_detect(h22_cosx, id);
					phase_log.push_back(PhaseLog { h11_cosx, sub.h[0][0] });
					phase_log.push_back(PhaseLog { h21_cosx, sub.h[1][0] });
					phase_log.push_back(PhaseLog { h12_cosx, sub.h[0][1] });
					phase_log.push_back(PhaseLog { h22_cosx, sub.h[1][1] });
					csi.push_back(sub);
				}

				// ! refer to PHY_Sender for value
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
					// std::cerr << "Phase log:\n";
					// for (auto& x : phase_log) {
					// 	for (auto y : x.signal)
					// 		std::cerr << y << ", ";
					// 	std::cerr << "\n";
					// 	std::cerr << x.estimate << "\n";
					// }
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
					total_ZF_vs_ML += ZF_compared_to_ML;
					total_bits += now_bits;

				} else {
					// discard
					std::cerr << "                                    ";
					std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!Bad frame!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
					// std::cerr << "Phase log:\n";
					// for (auto& x : phase_log) {
					// 	for (auto y : x.signal)
					// 		std::cerr << y << ", ";
					// 	std::cerr << "\n";
					// 	std::cerr << x.estimate << "\n";
					// }
					start = saved_start;
				}

				state = PhyRecvState::WAIT_HEADER;
			} else if (state == PhyRecvState::COLLECT_SAMPLES) {
				if (!symbols_to_collect) {
					state = next_state;
				} else {
					symbols_to_collect -= get_samples(symbols_to_collect, Rx1_samples, Rx2_samples);
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
			std::cerr << "     ZF vs ML:      " << total_ZF_vs_ML << " / " << total_bits << "\n";
		}
	}

	Phase phase_detect(const Samples& signal, int id)
	{
		auto& x = config.get_carriers(Tag<T>())[id][0];
		auto& y = config.get_axes(Tag<T>())[id];
		float real = 0, imag = 0;

		for (int i = 0; i < signal.size(); ++i) {
			real += signal[i] * x[i];
			imag += signal[i] * y[i];
		}
		return Phase { real / (x.size() / 2), imag / (x.size() / 2) };
	}

	int get_samples(int count, Samples& Rx1_samples, Samples& Rx2_samples)
	{
		int step = config.get_phy_frame_CP_length() + config.get_symbol_length()
			+ config.get_phy_frame_CP_length();
		int rightmost_pos = std::min(m_Rx1_buffer.size(), m_Rx2_buffer.size()) - step + 1;
		int collected_count = 0;

		for (int i = start; i < rightmost_pos && collected_count < count; i += step) {
			for (int j = 0; j < config.get_symbol_length(); ++j) {
				Rx1_samples.push_back(m_Rx1_buffer[i + config.get_phy_frame_CP_length() + j]);
				Rx2_samples.push_back(m_Rx2_buffer[i + config.get_phy_frame_CP_length() + j]);
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

	CSI inv(CSI x)
	{
		Phase det = x.h[0][0] * x.h[1][1] - x.h[0][1] * x.h[1][0];
		swap(x.h[0][0], x.h[1][1]);
		x.h[0][1] = -x.h[0][1];
		x.h[1][0] = -x.h[1][0];
		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 2; ++j) {
				x.h[i][j] /= det;
			}
		return x;
	}

	CSI transpose(CSI x)
	{
		swap(x.h[0][1], x.h[1][0]);
		return x;
	}

	CSI matmul(CSI x, CSI y)
	{
		CSI ret;
		memset(ret.h, 0, sizeof(ret.h));
		for (int i = 0; i < 2; ++i) {
			for (int k = 0; k < 2; ++k) {
				for (int j = 0; j < 2; ++j) {
					ret.h[i][j] += x.h[i][k] * y.h[k][j];
				}
			}
		}
		return ret;
	}

	CSI moore_penrose_inv(CSI x) { return matmul(inv(matmul(transpose(x), x)), transpose(x)); }

	std::vector<Phase> vec_matmul(std::vector<Phase> vec, CSI mat)
	{
		return { vec[0] * mat.h[0][0] + vec[1] * mat.h[1][0], vec[0] * mat.h[0][1] + vec[1] * mat.h[1][1] };
	}

	int to_bits(int count, Bits& bits)
	{
		int step = config.get_phy_frame_CP_length() + config.get_symbol_length()
			+ config.get_phy_frame_CP_length();
		int rightmost_pos = m_Rx1_buffer.size() - step + 1;
		int converted_count = 0;

		for (int i = start; i < rightmost_pos && converted_count < count; i += step, start += step) {
			for (int id = 0; id < config.get_num_carriers(); ++id) {

				Samples y1;
				for (int j = 0; j < config.get_symbol_length(); ++j) {
					y1.push_back(m_Rx1_buffer[i + config.get_phy_frame_CP_length() + j]);
				}

				Samples y2;
				for (int j = 0; j < config.get_symbol_length(); ++j) {
					y2.push_back(m_Rx2_buffer[i + config.get_phy_frame_CP_length() + j]);
				}

				auto phase_1 = phase_detect(y1, id);
				auto phase_2 = phase_detect(y2, id);

				Phase saved_1, saved_2;

				float min_dis = 999999;
				int s1_ml = -1, s2_ml = -1;
				std::vector<float> saved_y_hat_1, saved_y_hat_2;
				for (int s1 = 0; s1 <= 1; ++s1)
					for (int s2 = 0; s2 <= 1; ++s2) {
						auto x1 = config.get_carrier_phases()[id][s1];
						auto x2 = config.get_carrier_phases()[id][s2];

						float dist = 0;

						auto applied_1 = x1 * csi[id].h[0][0] + x2 * csi[id].h[1][0];
						auto applied_2 = x1 * csi[id].h[0][1] + x2 * csi[id].h[1][1];

						dist = std::abs(applied_1 - phase_1) * std::abs(applied_1 - phase_1)
							+ std::abs(applied_2 - phase_2) * std::abs(applied_2 - phase_2);

						if (dist <= min_dis) {
							min_dis = dist;
							s1_ml = s1;
							s2_ml = s2;
							saved_1 = applied_1;
							saved_2 = applied_2;
						}
					}

				int s1_zf = -1, s2_zf = -1;
				auto vec = vec_matmul({ phase_1, phase_2 }, moore_penrose_inv(csi[id]));
				s1_zf = vec[0].real() < 0;
				s2_zf = vec[1].real() < 0;

				// bits.push_back(s1_ml);
				// bits.push_back(s2_ml);
				bits.push_back(s1_zf);
				bits.push_back(s2_zf);

				if (dump) {
					std::cerr << "---------------------------Y1---------------------------\n";
					for (auto x : y1) {
						std::cerr << x << ", ";
					}
					std::cerr << "\n";
					std::cerr << phase_1 << "  " << saved_1 << "  -  " << s1_ml << "\n";
					std::cerr << "---------------------------Y2---------------------------\n";
					for (auto x : y2) {
						std::cerr << x << ", ";
					}
					std::cerr << "\n";
					std::cerr << phase_2 << "  " << saved_2 << "  -  " << s2_ml << "\n";
					std::cerr << "---------------------------------\n";
					std::cerr << s1_ml << "  -  " << s1_zf << "\n";
					std::cerr << s2_ml << "  -  " << s2_zf << "\n";
				}

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

	struct PhaseLog {
		Samples signal;
		Phase estimate;
	};

	Athernet::Config& config;
	Athernet::RingBuffer<T>& m_Rx1_buffer;
	Athernet::RingBuffer<T>& m_Rx2_buffer;
	Athernet::SyncQueue<Frame>& m_recv_queue;
	Athernet::SyncQueue<Frame>& m_decoder_queue;
	std::thread worker;
	std::atomic_bool running;
	int start;
	std::vector<CSI> csi;
	std::vector<PhaseLog> phase_log;
	int ZF_compared_to_ML = 0;
	int now_bits = 0;
	int total_ZF_vs_ML = 0;
	int total_bits = 0;
	int dump = 0;
	PhyRecvState state = PhyRecvState::WAIT_HEADER;
};
}