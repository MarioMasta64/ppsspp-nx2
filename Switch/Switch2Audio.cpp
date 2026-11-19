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
#include <memory>
#include <system_error>
#endif

namespace {
constexpr int SAMPLE_RATE = 48000;
constexpr int PERIOD_FRAMES = 512;
constexpr int CHANNELS = 2;

void ConvertPcm(const std::array<float, PERIOD_FRAMES * CHANNELS> &samples, int16_t *pcm) {
	for (size_t i = 0; i < samples.size(); ++i) {
		const float sample = std::isfinite(samples[i]) ? samples[i] : 0.0f;
		pcm[i] = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
	}
}

#if defined(PPSSPP_SWITCH2_GAMESHARE)
// Compare application-mix capture through Audren with the normal AudOut path.
class GameShareAudioRenderer {
public:
	~GameShareAudioRenderer() {
		running_ = false;
		if (worker_.joinable()) {
			worker_.join();
		}
		if (started_) {
			audrenStopAudioRenderer();
		}
		if (initialized_) {
			audrenExit();
		}
		if (driver_.etc) {
			audrvClose(&driver_);
		}
		free(pool_);
	}

	bool Start(RenderCallback callback, void *userdata) {
		const AudioRendererConfig config{AudioRendererOutputRate_48kHz, 4, 0, 1, 1, CHANNELS};
		Result rc = audrenInitialize(&config);
		if (R_FAILED(rc)) { return Fail("initialize", rc); }
		initialized_ = true;
		rc = audrvCreate(&driver_, &config, CHANNELS);
		if (R_FAILED(rc)) { return Fail("create driver", rc); }

		constexpr size_t bytes = BUFFER_COUNT * PERIOD_FRAMES * CHANNELS * sizeof(int16_t);
		constexpr size_t poolSize = (bytes + AUDREN_MEMPOOL_ALIGNMENT - 1) & ~(AUDREN_MEMPOOL_ALIGNMENT - 1);
		pool_ = static_cast<int16_t *>(memalign(AUDREN_MEMPOOL_ALIGNMENT, poolSize));
		if (!pool_) { return Fail("allocate buffers", MAKERESULT(Module_Libnx, LibnxError_OutOfMemory)); }
		memset(pool_, 0, poolSize);
		armDCacheFlush(pool_, poolSize);

		const int poolId = audrvMemPoolAdd(&driver_, pool_, poolSize);
		const u8 sinkChannels[] = {0, 1};
		if (poolId < 0 || !audrvMemPoolAttach(&driver_, poolId) ||
			audrvDeviceSinkAdd(&driver_, AUDREN_DEFAULT_DEVICE_NAME, CHANNELS, sinkChannels) < 0 ||
			!audrvVoiceInit(&driver_, 0, CHANNELS, PcmFormat_Int16, SAMPLE_RATE)) {
			return Fail("configure PCM sink", MAKERESULT(Module_Libnx, LibnxError_BadInput));
		}
		audrvVoiceSetDestinationMix(&driver_, 0, AUDREN_FINAL_MIX_ID);
		for (int ch = 0; ch < CHANNELS; ++ch) {
			audrvVoiceSetMixFactor(&driver_, 0, 1.0f, ch, ch);
		}
		for (int i = 0; i < BUFFER_COUNT; ++i) {
			auto &wave = waves_[i];
			wave.data_pcm16 = pool_;
			wave.size = poolSize;
			wave.start_sample_offset = i * PERIOD_FRAMES;
			wave.end_sample_offset = (i + 1) * PERIOD_FRAMES;
			if (!audrvVoiceAddWaveBuf(&driver_, 0, &wave)) {
				return Fail("queue initial buffers", MAKERESULT(Module_Libnx, LibnxError_BadInput));
			}
		}
		audrvVoiceStart(&driver_, 0);
		rc = audrvUpdate(&driver_);
		if (R_FAILED(rc)) { return Fail("submit initial buffers", rc); }
		rc = audrenStartAudioRenderer();
		if (R_FAILED(rc)) { return Fail("start", rc); }
		started_ = true;
		running_ = true;
		try {
			worker_ = std::thread([this, callback, userdata] { Run(callback, userdata); });
		} catch (const std::system_error &) {
			running_ = false;
			return Fail("start audio thread", MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
		}
		std::fprintf(stderr, "Audio: GameShare Audren application mix, 48000 Hz stereo\n");
		return true;
	}

	bool Running() const { return running_; }

private:
	static constexpr int BUFFER_COUNT = 3;
	bool Fail(const char *operation, Result rc) {
		std::fprintf(stderr, "Audio: GameShare Audren %s failed (%08x)\n", operation, rc);
		return false;
	}

	void Run(RenderCallback callback, void *userdata) {
		auto *frameEvent = audrenGetFrameEvent();
		std::array<float, PERIOD_FRAMES * CHANNELS> samples{};
		while (running_) {
			Result rc = eventWait(frameEvent, 20000000);
			if (!running_) { break; }
			if (R_FAILED(rc)) {
				if (R_VALUE(rc) == KERNELRESULT(TimedOut)) { continue; }
				running_ = Fail("wait for frame", rc);
				break;
			}
			bool queued = false;
			for (auto &wave : waves_) {
				if (wave.state != AudioDriverWaveBufState_Done && wave.state != AudioDriverWaveBufState_Free) {
					continue;
				}
				samples.fill(0.0f);
				if (callback) {
					callback(samples.data(), PERIOD_FRAMES, SAMPLE_RATE, userdata);
				}
				Switch2GameShareMix(samples.data(), PERIOD_FRAMES, SAMPLE_RATE);
				auto *pcm = pool_ + wave.start_sample_offset * CHANNELS;
				ConvertPcm(samples, pcm);
				armDCacheFlush(pcm, samples.size() * sizeof(*pcm));
				if (!audrvVoiceAddWaveBuf(&driver_, 0, &wave)) {
					running_ = Fail("queue buffer", MAKERESULT(Module_Libnx, LibnxError_BadInput));
					return;
				}
				queued = true;
			}
			if (queued && !audrvVoiceIsPlaying(&driver_, 0)) {
				audrvVoiceStart(&driver_, 0);
			}
			rc = audrvUpdate(&driver_);
			if (R_FAILED(rc)) {
				running_ = Fail("submit buffers", rc);
				break;
			}
		}
	}

	AudioDriver driver_{};
	std::array<AudioDriverWaveBuf, BUFFER_COUNT> waves_{};
	int16_t *pool_ = nullptr;
	bool initialized_ = false, started_ = false;
	std::atomic<bool> running_{false};
	std::thread worker_;
};
#endif

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
#if defined(PPSSPP_SWITCH2_GAMESHARE)
		if (gameShareRenderer_) {
			return gameShareRenderer_->Running();
		}
#endif
		if (initialized_)
			return running_;
#if defined(PPSSPP_SWITCH2_GAMESHARE)
		auto renderer = std::make_unique<GameShareAudioRenderer>();
		if (renderer->Start(callback_, userdata_)) {
			gameShareRenderer_ = std::move(renderer);
			return true;
		}
		renderer.reset();
		std::fprintf(stderr, "Audio: falling back to AudOut\n");
#endif
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
				ConvertPcm(samples, static_cast<int16_t *>(released->buffer));
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
#if defined(PPSSPP_SWITCH2_GAMESHARE)
	std::unique_ptr<GameShareAudioRenderer> gameShareRenderer_;
#endif
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
