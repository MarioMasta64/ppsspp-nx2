// Copyright (c) 2012- PPSSPP Project.
// This program is free software under the GNU GPL, version 2 or later.
// See LICENSE.TXT for details.
#include "Common/CommonTypes.h"
#include <kaihen.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "Common/CPUDetect.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/GPU/Vulkan/VulkanGraphicsContext.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/Input/InputState.h"
#include "Common/Log.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Core/CmdLine.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/EmuThread.h"
#include "Switch/Switch2Diagnostics.h"
#if defined(PPSSPP_SWITCH2_GAMESHARE)
#include "Switch/GameShare/Integration.h"
#endif

namespace {
#if defined(PPSSPP_SWITCH2_GUEST_BUILD)
constexpr const char *DATA_PATH = "sdmc:/switch/ppsspp-switch2-gameshare/guest/";
#elif defined(PPSSPP_SWITCH2_GAMESHARE)
constexpr const char *DATA_PATH = "sdmc:/switch/ppsspp-switch2-gameshare/";
#else
constexpr const char *DATA_PATH = "sdmc:/switch/ppsspp-switch2/";
#endif
std::atomic<bool> quitRequested{false};
uint32_t displayWidth = 1920;
uint32_t displayHeight = 1080;
std::atomic<float> displayRefreshRate{60.0f};

bool VerifyJit(std::string *error) {
	Jit code{};
	Result rc = jitCreate(&code, 0x1000);
	if (R_FAILED(rc)) {
		*error = "kaiHEN could not allocate executable JIT memory";
		std::fprintf(stderr, "JIT allocation failed: %08x\n", rc);
		return false;
	}
	const uint32_t instructions[] = {0x52800540, 0xD65F03C0}; // mov w0, #42; ret
	memcpy(jitGetRwAddr(&code), instructions, sizeof(instructions));
	const KaihenJitRange range{0, sizeof(instructions)};
	rc = kaihenJitPublishRanges(&code, &range, 1);
	bool passed = false;
	if (R_SUCCEEDED(rc)) {
		auto function = reinterpret_cast<int (*)()>(jitGetRxAddr(&code));
		passed = function() == 42;
	}
	const Result closeResult = jitClose(&code);
	std::fprintf(stderr, "JIT self-test: publish=%08x close=%08x result=%s\n", rc, closeResult, passed ? "pass" : "fail");
	if (!passed)
		*error = "kaiHEN JIT publication self-test failed. See boot.log.";
	return passed;
}

struct NativeTimeService {
	bool initialized = R_SUCCEEDED(timeInitialize());
	~NativeTimeService() {
		if (initialized)
			timeExit();
	}
};

void PollInput(PadState &pad) {
	padUpdate(&pad);
	const u64 down = padGetButtonsDown(&pad);
	const u64 up = padGetButtonsUp(&pad);
	struct Button { u64 native; InputKeyCode key; };
	static constexpr Button buttons[] = {
		{HidNpadButton_B, NKCODE_BUTTON_A}, {HidNpadButton_A, NKCODE_BUTTON_B},
		{HidNpadButton_Y, NKCODE_BUTTON_X}, {HidNpadButton_X, NKCODE_BUTTON_Y},
		{HidNpadButton_Up, NKCODE_DPAD_UP}, {HidNpadButton_Down, NKCODE_DPAD_DOWN},
		{HidNpadButton_Left, NKCODE_DPAD_LEFT}, {HidNpadButton_Right, NKCODE_DPAD_RIGHT},
		{HidNpadButton_L, NKCODE_BUTTON_L1}, {HidNpadButton_R, NKCODE_BUTTON_R1},
		{HidNpadButton_Plus, NKCODE_BUTTON_START}, {HidNpadButton_Minus, NKCODE_BUTTON_SELECT},
		{HidNpadButton_StickR, NKCODE_HOME},
	};
	for (const auto &button : buttons) {
		if (down & button.native)
			NativeKey(KeyInput(DEVICE_ID_XINPUT_0, button.key, KeyInputFlags::DOWN));
		if (up & button.native)
			NativeKey(KeyInput(DEVICE_ID_XINPUT_0, button.key, KeyInputFlags::UP));
	}
	const HidAnalogStickState left = padGetStickPos(&pad, 0);
	const HidAnalogStickState right = padGetStickPos(&pad, 1);
	const AxisInput axes[] = {
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_X, std::clamp(left.x / 32767.0f, -1.0f, 1.0f)},
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_Y, std::clamp(-left.y / 32767.0f, -1.0f, 1.0f)},
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_Z, std::clamp(right.x / 32767.0f, -1.0f, 1.0f)},
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_RZ, std::clamp(-right.y / 32767.0f, -1.0f, 1.0f)},
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_LTRIGGER, (padGetButtons(&pad) & HidNpadButton_ZL) ? 1.0f : 0.0f},
	};
	NativeAxis(axes, std::size(axes));
}

int ShowStartupError(const std::string &error) {
	consoleInit(nullptr);
	std::printf("PPSSPP Switch 2\n\nStartup failed:\n%s\n\nPress Plus to exit.\n", error.c_str());
	PadState pad;
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	while (appletMainLoop()) {
		padUpdate(&pad);
		if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
			break;
		consoleUpdate(nullptr);
	}
	consoleExit(nullptr);
	return 1;
}
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME: return "Nintendo Switch 2";
	case SYSPROP_DEVICE_MODEL: return "Switch 2 (kaiHEN)";
	case SYSPROP_LANGREGION: return "en_US";
	case SYSPROP_CPUINFO: return cpu_info.Summarize();
	default: return "";
	}
}
std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) { return {}; }
std::vector<std::string> System_GetCameraDeviceList() { return {}; }
int64_t System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DEVICE_TYPE: return DEVICE_TYPE_TV;
	case SYSPROP_DISPLAY_XRES: return displayWidth;
	case SYSPROP_DISPLAY_YRES: return displayHeight;
	case SYSPROP_DISPLAY_COUNT: return 1;
	case SYSPROP_AUDIO_SAMPLE_RATE:
	case SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE: return 48000;
	case SYSPROP_AUDIO_FRAMES_PER_BUFFER:
	case SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER: return 512;
	default: return -1;
	}
}
float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE: return displayRefreshRate.load();
	case SYSPROP_DISPLAY_DPI: return 96.0f;
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM: return 0.0f;
	default: return -1.0f;
	}
}
bool System_GetPropertyBool(SystemProperty prop) {
#if defined(PPSSPP_SWITCH2_GUEST_BUILD)
	if (prop == SYSPROP_HAS_TEXT_INPUT_DIALOG || prop == SYSPROP_KEYBOARD_IS_SOFT) {
		return false;
	}
#endif
	switch (prop) {
	case SYSPROP_CAN_JIT:
	case SYSPROP_HAS_BACK_BUTTON:
	case SYSPROP_HAS_TEXT_INPUT_DIALOG:
	case SYSPROP_KEYBOARD_IS_SOFT:
	case SYSPROP_CAN_GET_FREE_SPACE_FAST: return true;
	default: return false;
	}
}
void System_Toast(std::string_view text) { std::fprintf(stderr, "%.*s\n", static_cast<int>(text.size()), text.data()); }
void System_ShowKeyboard() {}
void System_Vibrate(int length_ms) {}
void System_LaunchUrl(LaunchUrlType type, std::string_view url) {}
void System_Notify(SystemNotification notification) {}
void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }
bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) {
	switch (type) {
	case SystemRequestType::EXIT_APP:
	case SystemRequestType::RESTART_APP:
		quitRequested = true;
		return true;
	case SystemRequestType::INPUT_TEXT_MODAL:
#if defined(PPSSPP_SWITCH2_GUEST_BUILD)
		g_requestManager.PostSystemFailure(requestId);
#else
		System_RunOnMainThread([=] {
			SwkbdConfig keyboard{};
			Result rc = swkbdCreate(&keyboard, 0);
			if (R_FAILED(rc)) {
				g_requestManager.PostSystemFailure(requestId);
				return;
			}
			swkbdConfigMakePresetDefault(&keyboard);
			swkbdConfigSetHeaderText(&keyboard, param1.c_str());
			swkbdConfigSetInitialText(&keyboard, param2.c_str());
			if (param3)
				swkbdConfigSetPasswordFlag(&keyboard, 1);
			char text[1024]{};
			rc = swkbdShow(&keyboard, text, sizeof(text));
			swkbdClose(&keyboard);
			if (R_SUCCEEDED(rc))
				g_requestManager.PostSystemSuccess(requestId, text);
			else
				g_requestManager.PostSystemFailure(requestId);
		});
#endif
		return true;
	case SystemRequestType::SET_WINDOW_TITLE:
	case SystemRequestType::SET_KEEP_SCREEN_BRIGHT:
	case SystemRequestType::APPLY_FULLSCREEN_STATE:
		return true;
	default:
		return false;
	}
}

#if !defined(PPSSPP_SWITCH2_GUEST_BUILD)
int main(int argc, char *argv[]) {
	mkdir("sdmc:/switch", 0777);
	mkdir(DATA_PATH, 0777);
	chdir(DATA_PATH);
	std::freopen("boot.log", "w", stderr);
	setvbuf(stderr, nullptr, _IONBF, 0);
	std::fprintf(stderr, "PPSSPP Switch 2 startup\n");
	Switch2DiagnosticsInit(DATA_PATH);
	Switch2DiagnosticsCheckpoint(1);
	std::fprintf(stderr, "Startup diagnostic revision: comdat-isolation\n");
	Result rc = romfsInit();
	if (R_FAILED(rc))
		return ShowStartupError("Could not mount embedded assets");
	std::string jitError;
	if (!VerifyJit(&jitError)) {
		romfsExit();
		return ShowStartupError(jitError);
	}
	uint64_t coreMask = 0;
	if (R_SUCCEEDED(svcGetInfo(&coreMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
		cpu_info.num_cores = std::max(1, __builtin_popcountll(coreMask));
	std::fprintf(stderr, "Application core mask: %llx (%d cores)\n", static_cast<unsigned long long>(coreMask), cpu_info.num_cores);
	NativeTimeService timeService;
	const bool socketsReady = R_SUCCEEDED(socketInitializeDefault());
	g_VFS.Register("", new DirectoryReader(Path("romfs:/")));
	CommandLineOptions options;
	const auto parsed = options.Parse(argc, const_cast<const char **>(argv));
	if (parsed != CommandLineParseResult::Continue) {
		if (socketsReady)
			socketExit();
		romfsExit();
		return parsed == CommandLineParseResult::Exit ? 0 : 1;
	}
	if (!options.log.has_value())
		options.log = std::string(DATA_PATH) + "ppsspp.log";
	options.gpuBackend = GPUBackend::VULKAN;
	Switch2DiagnosticsCheckpoint(20);
	NativeInit(argc, const_cast<const char **>(argv), options, DATA_PATH, "sdmc:/", DATA_PATH);
	Switch2DiagnosticsCheckpoint(21);
	if (g_Config.bFirstRun)
		g_Config.iInternalResolution = 1;
	g_Config.bShowTouchControls = false;
	g_Config.bFastMemory = false;
	g_Config.iGPUBackend = static_cast<int>(GPUBackend::VULKAN);
	if (g_Config.currentDirectory == Path("/")) {
#if defined(PPSSPP_SWITCH2_GAMESHARE)
		g_Config.currentDirectory = Path("sdmc:/switch/ppsspp-switch2/games");
#else
		g_Config.currentDirectory = Path("sdmc:/");
#endif
	}
	// Request 120 Hz where supported, retaining 60 Hz display compatibility.
	Switch2DiagnosticsCheckpoint(22);
	rc = nwindowSetModeDimensions(nwindowGetDefault(), 1920, 1080, 1920, 1080);
	if (R_SUCCEEDED(rc)) {
		rc = nwindowSetFrameRates(nwindowGetDefault(), 120, 60, true);
	}
	if (R_FAILED(rc)) {
		std::fprintf(stderr, "Display configuration failed: %08x\n", rc);
		NativeShutdown();
		if (socketsReady) {
			socketExit();
		}
		romfsExit();
		return ShowStartupError("Could not configure 1080p/120 Hz presentation. See boot.log.");
	}
	nwindowGetDimensions(nwindowGetDefault(), &displayWidth, &displayHeight);
	std::fprintf(stderr, "Display: %ux%u, requested 120 fps (60 fps fallback), VRR allowed\n", displayWidth, displayHeight);
	Switch2DiagnosticsCheckpoint(2);
	VulkanGraphicsContext graphics;
	Switch2DiagnosticsCheckpoint(3);
	std::string error;
	Switch2DiagnosticsCheckpoint(4);
	bool apiReady = graphics.InitAPI(nullptr, &g_Config.sVulkanDevice, &error);
	if (apiReady) {
		auto *vulkan = static_cast<VulkanContext *>(graphics.GetAPIContext());
		vulkan->SetCbGetDrawSize([] {
			uint32_t width = displayWidth, height = displayHeight;
			nwindowGetDimensions(nwindowGetDefault(), &width, &height);
			return VkExtent2D{width, height};
		});
	}
	bool surfaceReady = apiReady && graphics.InitSurface(WINDOWSYSTEM_VI_NN, nullptr, nwindowGetDefault(), &error);
	if (!surfaceReady) {
		if (apiReady)
			graphics.ShutdownAPI();
		NativeShutdown();
		if (socketsReady)
			socketExit();
		romfsExit();
		return ShowStartupError(error);
	}
	Native_UpdateScreenScale(displayWidth, displayHeight, 1.0f);
	NativeResized();
	PadState pad;
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	RunMainLoop(&graphics, new NativeApplication(), [&](GraphicsContext *context) {
		if (!appletMainLoop()) {
			quitRequested = true;
		}
#if defined(PPSSPP_SWITCH2_GAMESHARE)
		Switch2GameSharePoll();
		if (quitRequested && Switch2GameSharePrepareExit()) {
			return false;
		}
#else
		if (quitRequested) {
			return false;
		}
#endif
		// The SDK follows VI2 refresh events; keep the menu limiter in sync.
		u16 numerator = 0, denominator = 0;
		if (R_SUCCEEDED(nwindowGetPresentInterval(nwindowGetDefault(), &numerator, &denominator)) && numerator && denominator) {
			const float rate = static_cast<float>(denominator) / numerator;
			if (displayRefreshRate.exchange(rate) != rate) {
				std::fprintf(stderr, "Display presentation target: %.0f fps\n", rate);
			}
		}
		PollInput(pad);
		NativeFrame(context);
#if defined(PPSSPP_SWITCH2_GAMESHARE)
		return true;
#else
		return !quitRequested;
#endif
	});
	std::fprintf(stderr, "Main loop ended: quitRequested=%d\n", quitRequested.load() ? 1 : 0);
	graphics.ShutdownSurface();
	graphics.ShutdownAPI();
	NativeShutdown();
	Switch2DiagnosticsShutdown();
	if (socketsReady)
		socketExit();
	romfsExit();
	return 0;
}

#endif // !PPSSPP_SWITCH2_GUEST_BUILD
