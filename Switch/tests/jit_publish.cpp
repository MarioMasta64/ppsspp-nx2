// Copyright (c) 2012- PPSSPP Project.
// This program is free software under the GNU GPL, version 2 or later.
// See LICENSE.TXT for details.
#include "Switch/Switch2Jit.h"
#include <kaihen.h>
#include <array>
#include <cassert>
#include <cstring>
#include <string>

static int calls;
static size_t copied;
extern "C" Result kaihenJitPublishRanges(Jit *jit, const KaihenJitRange *ranges, size_t count) {
	++calls;
	for (size_t i = 0; i < count; ++i) {
		assert(ranges[i].offset <= jit->size && ranges[i].size <= jit->size - ranges[i].offset);
		memcpy(static_cast<u8 *>(jit->rx_addr) + ranges[i].offset,
			static_cast<u8 *>(jit->rw_addr) + ranges[i].offset, ranges[i].size);
		copied += ranges[i].size;
	}
	return 0;
}
int main(int argc, char **argv) {
	std::array<u8, 256> rx{}, staging{};
	std::array<u8, 128> rx2{}, staging2{};
	Jit a{rx.data(), staging.data(), rx.size()};
	Jit b{rx2.data(), staging2.data(), rx2.size()};
	Switch2JitRegister(&a);
	Switch2JitRegister(&b);
	staging.fill(0xA5);
	staging2.fill(0x42);
	const u8 *starts[] = {rx.data() + 4, rx.data() + 80, rx2.data()};
	const u8 *ends[] = {rx.data() + 12, rx.data() + 84, rx2.data() + 16};
	if (argc > 1) {
		const std::string mode = argv[1];
		if (mode == "cross-boundary")
			ends[0] = reinterpret_cast<const u8 *>(reinterpret_cast<uintptr_t>(rx.data()) + 257);
		else if (mode == "reversed")
			ends[0] = rx.data();
		else if (mode == "unregistered")
			Switch2JitUnregister(&a);
		Switch2JitPublish(starts, ends, 1);
		return 0;
	}
	assert(rx[4] == 0 && rx2[0] == 0);
	Switch2JitPublish(starts, ends, 3);
	assert(calls == 2 && copied == 28);
	for (size_t i = 0; i < rx.size(); ++i)
		assert(rx[i] == ((i >= 4 && i < 12) || (i >= 80 && i < 84) ? 0xA5 : 0));
	for (size_t i = 0; i < rx2.size(); ++i)
		assert(rx2[i] == (i < 16 ? 0x42 : 0));
	const u8 *empty[] = {rx.data() + rx.size()};
	Switch2JitPublish(empty, empty, 1);
	assert(calls == 2);
	Switch2JitUnregister(&b);
	Switch2JitUnregister(&a);
	Switch2JitRegister(&a);
	staging[4] = 0x11;
	Switch2JitPublish(starts, ends, 1);
	assert(rx[4] == 0x11);
	Switch2JitUnregister(&a);
	return 0;
}
