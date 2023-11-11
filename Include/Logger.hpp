#pragma once

#include <SyncQueue.hpp>
#include <atomic>
#include <cassert>
#include <format>
#include <string>
#include <vector>

namespace Athernet {
class Logger {
public:
	Logger()
	{
		remove(NOTEBOOK_DIR "log.txt");
		fd = fopen(NOTEBOOK_DIR "log.txt", "w");
		assert(fd);
		running.store(true);
		worker = std::thread(&Logger::work, this);
	}
	~Logger()
	{
		running.store(false);
		worker.join();
		fflush(fd);
		fclose(fd);
	}

	void append_log(std::string item) { log.push(std::move(item)); }

	void work()
	{
		std::string item;
		while (running.load()) {
			if (!log.pop(item)) {
				std::this_thread::yield();
				continue;
			}
			fprintf(fd, item.c_str());
			fprintf(fd, "\n");
		}
	}

private:
	SyncQueue<std::string> log;
	std::atomic_bool running;
	std::thread worker;
	FILE* fd;
};
}