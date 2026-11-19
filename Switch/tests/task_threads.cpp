// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#include "Switch/Switch2TaskThreads.h"
#include <cassert>
#include <condition_variable>

int main() {
	Switch2TaskThreads threads;
	std::mutex mutex;
	std::condition_variable condition;
	bool started = false, release = false;
	std::atomic<int> completed{0};
	threads.Start([&] {
		std::unique_lock<std::mutex> lock(mutex);
		started = true;
		condition.notify_one();
		condition.wait(lock, [&] { return release; });
		++completed;
		// Shutdown must allow a worker to launch a dependent dedicated job.
		threads.Start([&] { ++completed; });
	});
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait(lock, [&] { return started; });
	}
	std::atomic<bool> joined{false};
	std::thread joiner([&] { threads.Join(); joined = true; });
	{
		std::lock_guard<std::mutex> lock(mutex);
		assert(!joined);
		release = true;
	}
	condition.notify_one();
	joiner.join();
	assert(completed == 2);
	// Repeated work exercises retirement, reuse and concurrent submissions.
	std::vector<std::thread> producers;
	for (int i = 0; i < 4; ++i) {
		producers.emplace_back([&] {
			for (int j = 0; j < 32; ++j) {
				threads.Start([&] { ++completed; });
			}
		});
	}
	for (auto &producer : producers) {
		producer.join();
	}
	threads.Join();
	assert(completed == 130);
	// Destruction drains running jobs without an explicit Join().
	{
		Switch2TaskThreads scoped;
		scoped.Start([&] { ++completed; });
	}
	assert(completed == 131);
}
