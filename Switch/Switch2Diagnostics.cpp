// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#include "Common/CommonTypes.h"
#include "Switch/Switch2Diagnostics.h"
#include <cstdio>
#include <cstddef>

namespace {
FsFile faultFile{};
bool faultFileOpen = false;
unsigned char recordingFault = 0;

struct FaultRecord {
	u64 magic;
	u64 handlerAddress;
	u64 tick;
	u64 contextSize;
	ThreadExceptionDump context;
};
static_assert(offsetof(FaultRecord, context) == 32);
}

extern "C" {
// The SDK switches to this stack before invoking the diagnostic hook.
alignas(16) u8 __nx_exception_stack[0x10000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *context) {
	if (!faultFileOpen || __atomic_test_and_set(&recordingFault, __ATOMIC_RELAXED)) {
		return;
	}
	// Avoid allocation and stdio locks on the faulting thread.
	const FaultRecord record{0x31544C5541465050ULL,
		reinterpret_cast<u64>(&__libnx_exception_handler), svcGetSystemTick(), sizeof(ThreadExceptionDump), *context};
	fsFileWrite(&faultFile, 0, &record, sizeof(record), FsWriteOption_Flush);
	// Returning lets the SDK apply its normal unhandled-exception policy.
}
}

void Switch2DiagnosticsInit() {
	FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
	if (!sd) {
		return;
	}
	constexpr const char *path = "/switch/ppsspp-switch2/native-fault.bin";
	fsFsCreateFile(sd, path, sizeof(FaultRecord), 0);
	Result rc = fsFsOpenFile(sd, path, FsOpenMode_Write, &faultFile);
	if (R_SUCCEEDED(rc)) {
		rc = fsFileSetSize(&faultFile, sizeof(FaultRecord));
		const FaultRecord empty{};
		if (R_SUCCEEDED(rc)) {
			rc = fsFileWrite(&faultFile, 0, &empty, sizeof(empty), FsWriteOption_Flush);
		}
		if (R_SUCCEEDED(rc)) {
			faultFileOpen = true;
		} else {
			fsFileClose(&faultFile);
		}
	}
	std::fprintf(stderr, "Native fault recorder: %08x\n", rc);
}

void Switch2DiagnosticsShutdown() {
	if (faultFileOpen) {
		faultFileOpen = false;
		fsFileClose(&faultFile);
	}
}
