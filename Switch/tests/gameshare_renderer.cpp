// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#include "Switch/GameShare/CpuRenderer.h"
#include <cassert>
#include <string>
#include <vector>

int main(int argc, char **argv) {
	const std::string mode = argc > 1 ? argv[1] : "clean";
	Vi2Layer layer;
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
	{
		Switch2GuestCpuRenderer renderer;
		if (mode == "partial") { fake::fail = "gsvCreateImage"; fake::failAt = 2; }
		const auto opened = renderer.Open(&layer);
		if (mode == "partial") {
			assert(opened == 0xdead);
			assert(renderer.Retire() == 0);
			assert(fake::clean());
			return 0;
		}
		assert(opened == 0);
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
		if (mode != "clean") {
			assert(retired != 0);
			assert(fake::ranges == 1 && fake::gpu == 1);
			assert(renderer.Retire() == retired);
			return 0;
		}
		assert(retired == 0);
		assert(fake::retired && fake::clean());
		assert(renderer.Retire() == 0);
	}
	assert(fake::clean());
}
