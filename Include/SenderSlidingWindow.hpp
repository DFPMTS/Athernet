#pragma once

#include "PHY_Unit.hpp"
#include "RingBuffer.hpp"
#include <atomic>
#include <mutex>
#include <queue>

using namespace std::chrono_literals;

namespace Athernet {

class SenderSlidingWindow {
public:
	SenderSlidingWindow()
		: config { Config::get_instance() }
		, start { 0 }
		, window_start { 0 }
	{
	}

	int get_next_seq()
	{
		std::scoped_lock lock { mutex };
		int id = window_start + window.size();
		std::cerr << window_start << " : start\n";
		std::cerr << window.size() << " : size\n";
		if (id >= config.get_seq_limit())
			id -= config.get_seq_limit();
		return id;
	}

	bool try_push(std::shared_ptr<PHY_Unit> phy_unit)
	{
		std::unique_lock lock { mutex };
		producer.wait_for(lock, 100ms, [&]() { return window.size() < config.get_window_size(); });

		if (window.size() < config.get_window_size()) {
			window.push(phy_unit);
			return true;
		} else {
			return false;
		}
	}

	bool consume_one(std::shared_ptr<PHY_Unit>& unit)
	{
		std::scoped_lock lock { mutex };
		if (start < window.size()) {
			unit = window[start++];
			return true;
		} else {
			return false;
		}
	}

	void remove_acked(int ack)
	{
		std::scoped_lock lock { mutex };
		bool accepted = false;
		std::cerr << "Got ack:  " << ack << "   -   " << window_start << "\n";
		if (window_start + config.get_window_size() > config.get_seq_limit()) {
			if (ack >= window_start) {
				accepted = true;

			} else if (ack < window_start + config.get_window_size() - config.get_seq_limit()) {
				accepted = true;
				while (window.size() && window[0]->seq >= window_start) {
					window.discard(1);
					if (++window_start >= config.get_seq_limit())
						window_start -= config.get_seq_limit();
				}
				start = 0;
			}
		} else {
			if (ack >= window_start && ack < window_start + config.get_window_size()) {
				accepted = true;
			}
		}
		if (accepted) {
			std::cerr << "Got ack:  " << ack << "\n";
			while (window.size() && window[0]->seq <= ack) {
				window.discard(1);
				if (++window_start >= config.get_seq_limit())
					window_start -= config.get_seq_limit();
			}
			start = 0;
			producer.notify_one();
		}
	}

	void reset() { start = 0; }

private:
	Config& config;
	RingBuffer<std::shared_ptr<PHY_Unit>> window;
	std::mutex mutex;
	std::condition_variable producer;
	int window_start;
	int start;
};

}