// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#if defined(__SWITCH2__)
#include "Switch/Switch2Jit.h"
#include <kaihen.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>

namespace {
std::mutex mappingMutex;
std::map<uintptr_t, Jit *> mappings;
}

void Switch2JitRegister(Jit *jit) {
	std::lock_guard<std::mutex> lock(mappingMutex);
	mappings.emplace(reinterpret_cast<uintptr_t>(jitGetRxAddr(jit)), jit);
}

void Switch2JitUnregister(Jit *jit) {
	std::lock_guard<std::mutex> lock(mappingMutex);
	mappings.erase(reinterpret_cast<uintptr_t>(jitGetRxAddr(jit)));
}

void Switch2JitPublish(const u8 *const *starts, const u8 *const *ends, int count) {
	std::lock_guard<std::mutex> lock(mappingMutex);
	std::map<Jit *, std::vector<KaihenJitRange>> batches;
	for (int i = 0; i < count; ++i) {
		const uintptr_t start = reinterpret_cast<uintptr_t>(starts[i]);
		const uintptr_t end = reinterpret_cast<uintptr_t>(ends[i]);
		if (end == start)
			continue;
		auto it = mappings.upper_bound(start);
		if (end < start || it == mappings.begin()) {
			std::fprintf(stderr, "JIT publication outside registered code\n");
			std::abort();
		}
		--it;
		Jit *jit = it->second;
		const size_t offset = start - it->first;
		if (offset > jit->size || end - start > jit->size - offset) {
			std::fprintf(stderr, "JIT publication crosses allocation boundary\n");
			std::abort();
		}
		batches[jit].push_back({offset, end - start});
	}
	for (auto &[jit, ranges] : batches) {
		const Result rc = kaihenJitPublishRanges(jit, ranges.data(), ranges.size());
		if (R_FAILED(rc)) {
			std::fprintf(stderr, "kaiHEN JIT publication failed: %08x\n", rc);
			fatalThrow(rc);
		}
	}
}
#endif
