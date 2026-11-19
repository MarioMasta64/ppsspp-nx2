// Copyright (c) 2012- PPSSPP Project.
// This program is free software under the GNU GPL, version 2 or later.
// See LICENSE.TXT for details.
#include <switch.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <thread>
#include "Common/Audio/AudioBackend.h"
#include "Common/System/NativeApp.h"
#if defined(PPSSPP_SWITCH2_GAMESHARE)
#include "Switch/GameShare/Integration.h"
#endif

namespace {
class Switch2AudioBackend final : public AudioBackend {
public:
	~Switch2AudioBackend() override {
		running_ = false;
		if (thread_.joinable())
			thread_.join();
		if (initialized_) {
			audoutStopAudioOut();
			// Closing the output session retires its buffers before their memory is freed.
			audoutExit();
		}
		for (auto &buffer : buffers_)
			free(buffer.buffer);
	}
	void EnumerateDevices(std::vector<AudioDeviceDesc> *devices, bool capture = false) override {
		if (!capture)
			devices->push_back({"Switch 2 system output", "default"});
	}
	void SetRenderCallback(RenderCallback callback, void *userdata) override {
		callback_ = callback;
		userdata_ = userdata;
	}
	bool InitOutputDevice(std::string_view id, LatencyMode mode, bool *reverted) override {
		if (reverted)
			*reverted = !id.empty() && id != "default";
		if (initialized_)
			return running_;
		Result rc = audoutInitialize();
		if (R_FAILED(rc)) {
			error_ = "Could not initialize native audio";
			std::fprintf(stderr, "Audio: %s\n", error_.c_str());
			return false;
		}
		initialized_ = true;
		if (audoutGetSampleRate() != SAMPLE_RATE || audoutGetChannelCount() != 2 || audoutGetPcmFormat() != PcmFormat_Int16) {
			error_ = "Unsupported native audio output format";
			std::fprintf(stderr, "Audio: %s\n", error_.c_str());
			return false;
		}
		for (auto &buffer : buffers_) {
			buffer.buffer_size = 0x1000;
			buffer.data_size = PERIOD_FRAMES * 2 * sizeof(int16_t);
			buffer.buffer = memalign(0x1000, buffer.buffer_size);
			if (!buffer.buffer) {
				error_ = "Could not allocate native audio buffers";
				std::fprintf(stderr, "Audio: %s\n", error_.c_str());
				return false;
			}
			memset(buffer.buffer, 0, buffer.buffer_size);
			armDCacheFlush(buffer.buffer, buffer.data_size);
			if (R_FAILED(audoutAppendAudioOutBuffer(&buffer))) {
				error_ = "Could not submit native audio buffer";
				std::fprintf(stderr, "Audio: %s\n", error_.c_str());
				return false;
			}
		}
		if (R_FAILED(audoutStartAudioOut())) {
			error_ = "Could not start native audio";
			std::fprintf(stderr, "Audio: %s\n", error_.c_str());
			return false;
		}
		std::fprintf(stderr, "Audio: 48000 Hz stereo, 512-frame period, three buffers\n");
		running_ = true;
		thread_ = std::thread([this] {
			std::array<float, PERIOD_FRAMES * 2> samples{};
			while (running_) {
				AudioOutBuffer *released = nullptr;
				u32 count = 0;
				Result rc = audoutWaitPlayFinish(&released, &count, 50000000);
				if (R_FAILED(rc) || !released || !count)
					continue;
				if (!running_)
					break;
				samples.fill(0.0f);
				if (callback_)
					callback_(samples.data(), PERIOD_FRAMES, SAMPLE_RATE, userdata_);
				#if defined(PPSSPP_SWITCH2_GAMESHARE)
				Switch2GameShareMix(samples.data(), PERIOD_FRAMES, SAMPLE_RATE);
				#endif
				auto *pcm = static_cast<int16_t *>(released->buffer);
				for (size_t i = 0; i < samples.size(); ++i) {
					const float sample = std::isfinite(samples[i]) ? samples[i] : 0.0f;
					pcm[i] = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
				}
				armDCacheFlush(released->buffer, released->data_size);
				if (R_FAILED(audoutAppendAudioOutBuffer(released)))
					running_ = false;
			}
		});
		return true;
	}
	int SampleRate() const override { return SAMPLE_RATE; }
	int BufferSize() const override { return PERIOD_FRAMES * 3; }
	int PeriodFrames() const override { return PERIOD_FRAMES; }
	std::string GetErrorString() const override { return error_; }
	std::string GetCurrentDeviceName() const override { return "Switch 2 system output"; }
private:
	static constexpr int SAMPLE_RATE = 48000;
	static constexpr int PERIOD_FRAMES = 512;
	std::array<AudioOutBuffer, 3> buffers_{};
	RenderCallback callback_ = nullptr;
	void *userdata_ = nullptr;
	bool initialized_ = false;
	std::atomic<bool> running_{false};
	std::thread thread_;
	std::string error_;
};
}

AudioBackend *System_CreateAudioBackend() {
	return new Switch2AudioBackend();
}
