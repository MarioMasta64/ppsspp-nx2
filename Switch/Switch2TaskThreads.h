// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Keep dedicated jobs off the pool without requiring native detach support.
class Switch2TaskThreads {
public:
	~Switch2TaskThreads() { Join(); }

	void Start(std::function<void()> function) {
		std::lock_guard<std::mutex> lock(mutex_);
		for (auto it = threads_.begin(); it != threads_.end();) {
			if ((*it)->complete.load(std::memory_order_acquire)) {
				(*it)->thread.join();
				it = threads_.erase(it);
			} else {
				++it;
			}
		}
		// Allocate ownership before starting a thread that references the entry.
		threads_.push_back(std::make_unique<Entry>());
		Entry *entry = threads_.back().get();
		entry->thread = std::thread([entry, function = std::move(function)] {
			function();
			entry->complete.store(true, std::memory_order_release);
		});
	}

	void Join() {
		for (;;) {
			std::vector<std::unique_ptr<Entry>> pending;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				pending.swap(threads_);
			}
			if (pending.empty()) {
				return;
			}
			// A job may start another job; never join while holding the registry lock.
			for (auto &entry : pending) {
				if (entry->thread.joinable()) {
					entry->thread.join();
				}
			}
		}
	}

private:
	struct Entry {
		std::thread thread;
		std::atomic<bool> complete{false};
	};
	std::mutex mutex_;
	std::vector<std::unique_ptr<Entry>> threads_;
};
