#pragma once

#include "Config.hpp"
#include <vector>

namespace Athernet {

class ReceiverSlidingWindow {
public:
	ReceiverSlidingWindow()
		: config { Config::get_instance() }
		, window(config.get_seq_limit())
		, packets(config.get_seq_limit())
		, window_start { 0 }
	{
	}

	int get_num_collected() { return collected; }

	void collect(std::vector<int>& file)
	{
		file = std::move(stream);
		stream.clear();
		collected = 0;
	}

	int receive_packet(std::vector<int> packet_payload, int seq)
	{
		bool accepted = false;
		if (window_start + config.get_window_size() > config.get_seq_limit()) {
			if (seq >= window_start) {
				accepted = true;

			} else if (seq < window_start + config.get_window_size() - config.get_seq_limit()) {
				accepted = true;
			}
		} else {
			if (seq >= window_start && seq < window_start + config.get_window_size()) {
				accepted = true;
			}
		}
		std::cerr << "Received:  " << seq << "   " << window_start << "\n";
		if (accepted) {
			window[seq] = 1;
			packets[seq] = std::move(packet_payload);
			while (window[window_start]) {
				ever_received = 1;
				window[window_start] = 0;
				std::copy(std::begin(packets[window_start]), std::end(packets[window_start]),
					std::back_inserter(stream));
				collected += packets[window_start].size();
				if (++window_start >= config.get_seq_limit())
					window_start -= config.get_seq_limit();
			}
			if (ever_received) {
				last_received = (window_start == 0) ? config.get_seq_limit() - 1 : window_start - 1;
			}
			return last_received;
		} else {
			return last_received;
		}
	}

private:
	Config& config;
	std::vector<int> window;
	std::vector<std::vector<int>> packets;
	std::vector<int> stream;
	int collected = 0;
	int window_start = 0;
	int last_received = -1;
	int ever_received = 0;
};

}