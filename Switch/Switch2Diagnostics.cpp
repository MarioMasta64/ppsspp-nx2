// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#include "Common/CommonTypes.h"
#include "Switch/Switch2Diagnostics.h"
#include <cstdio>
#include <cstddef>
#include <cstring>

namespace {
FsFile faultFile{};
FsFile exitFile{};
bool faultFileOpen = false;
bool exitFileOpen = false;
unsigned char recordingFault = 0;
unsigned char recordingExit = 0;

struct ExitRecord {
	u64 magic;
	u64 recorderAddress;
	u64 kind;
	u64 result;
	u64 tick;
	u64 returnAddresses[24];
};

__attribute__((noinline)) void RecordExit(u64 kind, u64 result) {
	if (!exitFileOpen || __atomic_test_and_set(&recordingExit, __ATOMIC_RELAXED)) {
		return;
	}
	ExitRecord record{0x3154495845505050ULL, reinterpret_cast<u64>(&RecordExit), kind, result, svcGetSystemTick(), {}};
	u64 frame = reinterpret_cast<u64>(__builtin_frame_address(0));
	for (size_t i = 0; i < 24 && frame && !(frame & 15); ++i) {
		MemoryInfo info{};
		u32 pageInfo = 0;
		if (R_FAILED(svcQueryMemory(&info, &pageInfo, frame)) || !(info.perm & Perm_R) ||
			info.size < 16 || frame < info.addr || frame - info.addr > info.size - 16) {
			break;
		}
		const auto *saved = reinterpret_cast<const u64 *>(frame);
		record.returnAddresses[i] = saved[1];
		if (saved[0] <= frame) {
			break;
		}
		frame = saved[0];
	}
	fsFileWrite(&exitFile, 0, &record, sizeof(record), FsWriteOption_Flush);
}

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

void __real_exit(int status) __attribute__((noreturn));
void __real__exit(int status) __attribute__((noreturn));
void __real_abort() __attribute__((noreturn));
void __real_diagAbortWithResult(Result rc) __attribute__((noreturn));
void __real_fatalThrow(Result rc) __attribute__((noreturn));

void __wrap_exit(int status) {
	RecordExit(1, static_cast<u64>(status));
	__real_exit(status);
}
void __wrap__exit(int status) {
	RecordExit(2, static_cast<u64>(status));
	__real__exit(status);
}
void __wrap_abort() {
	RecordExit(3, 0);
	__real_abort();
}
void __wrap_diagAbortWithResult(Result rc) {
	RecordExit(4, rc);
	__real_diagAbortWithResult(rc);
}
void __wrap_fatalThrow(Result rc) {
	RecordExit(5, rc);
	__real_fatalThrow(rc);
}

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

namespace {
Result PreserveReport(FsFileSystem *sd, const char *path, u64 expectedMagic) {
	FsFile previous;
	Result rc = fsFsOpenFile(sd, path, FsOpenMode_Read, &previous);
	if (R_FAILED(rc)) { return 0; }
	u64 magic = 0;
	uint64_t read = 0;
	rc = fsFileRead(&previous, 0, &magic, sizeof(magic), FsReadOption_None, &read);
	fsFileClose(&previous);
	if (R_FAILED(rc) || read != sizeof(magic) || magic != expectedMagic) { return 0; }
	char archived[512];
	if (std::snprintf(archived, sizeof(archived), "%s.previous", path) >= sizeof(archived)) {
		return MAKERESULT(Module_Libnx, LibnxError_BadInput);
	}
	FsDirEntryType type;
	if (R_SUCCEEDED(fsFsGetEntryType(sd, archived, &type))) {
		rc = fsFsDeleteFile(sd, archived);
		if (R_FAILED(rc)) { return rc; }
	}
	return fsFsRenameFile(sd, path, archived);
}
}

void Switch2DiagnosticsInit(const char *directory) {
	FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
	if (!sd) {
		return;
	}
	const char *relative = std::strncmp(directory, "sdmc:/", 6) == 0 ? directory + 5 : directory;
	char path[512], exitPath[512];
	if (std::snprintf(path, sizeof(path), "%snative-fault.bin", relative) >= sizeof(path) ||
		std::snprintf(exitPath, sizeof(exitPath), "%snative-exit.bin", relative) >= sizeof(exitPath)) {
		return;
	}
	Result rc = PreserveReport(sd, path, 0x31544C5541465050ULL);
	if (R_SUCCEEDED(rc)) {
		fsFsCreateFile(sd, path, sizeof(FaultRecord), 0);
		rc = fsFsOpenFile(sd, path, FsOpenMode_Write, &faultFile);
	}
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
	rc = PreserveReport(sd, exitPath, 0x3154495845505050ULL);
	if (R_SUCCEEDED(rc)) {
		fsFsCreateFile(sd, exitPath, sizeof(ExitRecord), 0);
		rc = fsFsOpenFile(sd, exitPath, FsOpenMode_Write, &exitFile);
	}
	if (R_SUCCEEDED(rc)) {
		rc = fsFileSetSize(&exitFile, sizeof(ExitRecord));
		const ExitRecord empty{};
		if (R_SUCCEEDED(rc)) {
			rc = fsFileWrite(&exitFile, 0, &empty, sizeof(empty), FsWriteOption_Flush);
		}
		if (R_SUCCEEDED(rc)) {
			exitFileOpen = true;
		} else {
			fsFileClose(&exitFile);
		}
	}
	std::fprintf(stderr, "Native exit recorder: %08x\n", rc);
}

void Switch2DiagnosticsShutdown() {
	if (exitFileOpen) {
		exitFileOpen = false;
		fsFileClose(&exitFile);
	}
	if (faultFileOpen) {
		faultFileOpen = false;
		fsFileClose(&faultFile);
	}
}
