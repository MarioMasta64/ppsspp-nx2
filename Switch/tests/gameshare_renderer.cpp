// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#include "Switch/GameShare/CpuRenderer.h"
#include <cassert>
#include <string>
#include <vector>
#include <switch2/services/gsv.h>

extern "C" Result __wrap_gsvGpuCreateSlotEvent(GsvGpu *, uint32_t, Event *, bool);
extern "C" Result __wrap_gsvGpuOpenWorker(GsvGpu *, uint32_t, Event *);
extern "C" void __wrap_gsvGpuClose(GsvGpu *);

extern "C" Result __real_gsvGpuCreateSlotEvent(GsvGpu *gpu, uint32_t slot, Event *event, bool) {
	assert(gpu->unused == 7);
	if (slot >= 2) {
		assert(slot == 2 && fake::lastSlot >= slot);
		if (fake::slotRegistered) {
			return 0x200f3;
		}
		fake::slotRegistered = true;
		++fake::slotCreations;
		fake::openedSlot = slot;
		event->unused = 1;
		++fake::events;
	}
	return 0;
}

extern "C" Result __real_gsvGpuOpenWorker(GsvGpu *gpu, uint32_t slot, Event *) {
	assert(slot == 0 && fake::gpu == 0);
	gpu->unused = 7;
	++fake::gpu;
	return 0;
}

extern "C" void __real_gsvGpuClose(GsvGpu *gpu) {
	assert(gpu->unused == 7 && fake::gpu == 1 && fake::clean());
	fake::slotRegistered = false;
	gpu->unused = 0;
	--fake::gpu;
}

int main(int argc, char **argv) {
	const std::string mode = argc > 1 ? argv[1] : "clean";
	Vi2Layer layer;
	GsvGpu driver{};
	Event driverEvent{};
	assert(__wrap_gsvGpuOpenWorker(&driver, 0, &driverEvent) == 0);
	assert(__wrap_gsvGpuCreateSlotEvent(&driver, 1, &driverEvent, false) == 0);
	if (mode == "no-driver") {
		__wrap_gsvGpuClose(&driver);
		Switch2GuestCpuRenderer renderer;
		assert(renderer.Open(&layer) != 0);
		assert(renderer.Retire() == 0 && fake::clean());
		return 0;
	}
	if (mode == "no-slot") {
		fake::lastSlot = 1;
		Switch2GuestCpuRenderer renderer;
		assert(renderer.Open(&layer) != 0);
		assert(renderer.Retire() == 0 && fake::clean());
		__wrap_gsvGpuClose(&driver);
		return 0;
	}
	const size_t stride = 1280 * 4 + 16;
	std::vector<uint8_t> pixels(stride * 720);
	for (int y = 0; y < 720; ++y) {
		for (int x = 0; x < 1280; ++x) {
			pixels[y * stride + x * 4] = x;
			pixels[y * stride + x * 4 + 1] = y;
			pixels[y * stride + x * 4 + 2] = x ^ y;
			pixels[y * stride + x * 4 + 3] = 255;
		}
	}
	const unsigned sessions = mode == "reconnect" ? 2 : 1;
	for (unsigned session = 0; session < sessions; ++session) {
		fake::retired = false;
		Switch2GuestCpuRenderer renderer;
		if (mode == "partial") { fake::fail = "gsvCreateImage"; fake::failAt = 2; }
		const auto opened = renderer.Open(&layer);
		if (mode == "partial") {
			assert(opened == 0xdead);
			assert(renderer.Retire() == 0);
			assert(fake::sessionClean() && fake::events == 1);
			__wrap_gsvGpuClose(&driver);
			return 0;
		}
		assert(opened == 0);
		assert(__wrap_gsvGpuCreateSlotEvent(&driver, 2, &driverEvent, false) != 0);
		assert(renderer.Present(pixels.data(), 640, 720, stride) != 0);
		if (mode == "present-failure") { fake::fail = "vi2PresentLayer"; }
		const auto presented = renderer.Present(pixels.data(), 1280, 720, stride);
		if (mode == "present-failure") {
			assert(presented == 0xdead);
			assert(renderer.Retire() == 0xdead);
			assert(fake::ranges == 1 && fake::images == 2 && fake::aliases == 2);
			return 0;
		}
		assert(presented == 0);
		// Known Turing-sector fixture locations, including a 128-row block boundary.
		struct Point { int x, y; size_t offset; };
		for (const auto &point : {Point{0, 0, 0}, Point{4, 0, 64}, Point{8, 0, 256},
			Point{16, 0, 8192}, Point{0, 1, 16}, Point{0, 4, 128}, Point{0, 8, 512}, Point{0, 128, 655360}}) {
			assert(memcmp(fake::presented + point.offset, pixels.data() + point.y * stride + point.x * 4, 4) == 0);
		}
		if (mode == "release-timeout") { fake::counterReady = false; }
		if (mode == "range-timeout") { fake::fail = "eventWait"; }
		if (mode == "unregister-failure") { fake::fail = "vi2UnregisterLayerImage"; }
		const auto retired = renderer.Retire();
		if (mode != "clean" && mode != "reconnect") {
			assert(retired != 0);
			assert(fake::ranges == 1 && fake::gpu == 1);
			assert(renderer.Retire() == retired);
			return 0;
		}
		assert(retired == 0);
		assert(fake::retired && fake::sessionClean());
		assert(fake::events == 1 && fake::slotCreations == 1);
		assert(__wrap_gsvGpuCreateSlotEvent(&driver, 2, &driverEvent, false) != 0);
		assert(renderer.Retire() == 0);
	}
	__wrap_gsvGpuClose(&driver);
	assert(fake::clean() && !fake::slotRegistered);
}
