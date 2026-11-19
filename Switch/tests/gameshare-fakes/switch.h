#pragma once
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
using u8 = uint8_t; using u32 = uint32_t; using u64 = uint64_t; using Result = uint32_t;
struct Vi2Layer { uint32_t token = 1; };
struct Vi2Dependency { uint8_t data[16]{}; };
struct Vi2PresentOptions { uint64_t flags = 1; uint16_t interval_numerator = 1, interval_denominator = 60; };
struct GsvSync { int unused; }; struct GsvGpu { int unused; }; struct Event { int unused; };
using _Event = Event;
struct GsvGpuVaRange { uint64_t first, last; }; struct GsvImageLayout { int unused; };
constexpr unsigned Module_Libnx = 345;
constexpr unsigned LibnxError_BadInput = 1, LibnxError_OutOfMemory = 2, LibnxError_NotInitialized = 3;
#define MAKERESULT(m, d) ((m) + ((d) << 9))
#define KERNELRESULT(name) 0x1234u
#define R_FAILED(rc) ((rc) != 0)
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_VALUE(rc) (rc)
namespace fake {
inline int roots = 1, syncs = 1, gpu = 0, events = 0, clients = 0;
inline uint32_t lastSlot = 3, openedSlot = 0;
inline int ranges = 0, memory = 0, images = 0, aliases = 0;
inline uint32_t id = 10;
inline bool retired = false, counterReady = true;
inline uint64_t ticks = 0;
inline std::string fail;
inline int failAt = 1, seen = 0;
inline std::map<uint32_t, uintptr_t> memoryAddress;
inline std::map<uint32_t, uint32_t> imageMemory;
inline const uint8_t *presented = nullptr;
inline Result failure(const char *name) { return fail == name && ++seen == failAt ? 0xdead : 0; }
inline bool clean() { return roots == 1 && syncs == 1 && !events && !clients && !ranges && !memory && !images && !aliases; }
}
inline uint64_t armGetSystemTick() { return fake::ticks; }
inline uint64_t armNsToTicks(uint64_t ns) { return ns; }
inline void svcSleepThread(uint64_t ns) { fake::ticks += ns; }
inline void armDCacheFlush(void *, size_t) {}
inline Result svcUnknown80(void *, uint64_t, uint32_t, uint32_t) { return 0; }
inline Result gsvInitialize() { ++fake::roots; return 0; }
inline void gsvExit() { --fake::roots; }
inline Result gsvOpenSync(GsvSync *) { ++fake::syncs; return 0; }
inline void gsvSyncClose(GsvSync *) { --fake::syncs; }
inline Result gsvGpuGetSlotRange(GsvGpu *gpu, uint32_t *first, uint32_t *last) {
    assert(gpu->unused == 7);
    *first = 0;
    *last = fake::lastSlot;
    return 0;
}
inline Result gsvGpuRegisterClient(GsvGpu *gpu, uint64_t, uint8_t *out) {
    assert(gpu->unused == 7);
    ++fake::clients;
    *out = 1;
    return 0;
}
inline Result gsvGpuReleaseClient(GsvGpu *gpu, uint8_t) {
    assert(gpu->unused == 7);
    --fake::clients;
    return 0;
}
inline void gsvMakeImageLayout(GsvImageLayout *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint8_t, uint16_t) {}
inline Result gsvGetImageSize(GsvImageLayout *, uint64_t *out) { *out = 5120 * 768; return 0; }
inline Result gsvGpuRegisterMemory(GsvGpu *, uint64_t, uint64_t, uint8_t, uint32_t *out) { ++fake::ranges; *out = 42; return 0; }
inline Result gsvGpuUnregisterMemory(GsvGpu *gpu, uint32_t, uint32_t slot) { assert(gpu->unused == 7 && slot == fake::openedSlot); return 0; }
inline Result eventClear(Event *) { return 0; }
inline Result eventWait(Event *, uint64_t) { if (Result rc = fake::failure(__func__)) { return rc; } --fake::ranges; return 0; }
inline void eventClose(Event *) { --fake::events; }
inline Result gsvReserveImageMemory(uint8_t, uint64_t, uint32_t *out) { ++fake::memory; *out = ++fake::id; return 0; }
inline Result gsvRegisterImageMemory(uint32_t id, uint64_t address, uint64_t, uint32_t, uint64_t) { fake::memoryAddress[id] = address; return 0; }
inline Result gsvCreateImage(GsvImageLayout *, uint32_t memory, uint32_t *out) { if (Result rc = fake::failure(__func__)) { return rc; } ++fake::images; *out = ++fake::id; fake::imageMemory[*out] = memory; return 0; }
inline Result gsvCreateDisplayAlias(uint32_t, uint32_t) { ++fake::aliases; return 0; }
inline Result gsvDestroyDisplayAlias(uint32_t) { --fake::aliases; return 0; }
inline Result gsvDestroyImage(uint32_t) { --fake::images; return 0; }
inline Result gsvReleaseImageMemory(uint32_t) { --fake::memory; return 0; }
inline Result gsvSyncReadCounter(GsvSync *, uint32_t, uint64_t *value, uint8_t *mode) { *mode = 1; *value = fake::counterReady ? 7 : 0; return 0; }
inline Result vi2GetLayerAvailability(Vi2Layer *, uint64_t *available) { *available = 1; return 0; }
inline Result vi2WaitLayerAvailability(Vi2Layer *, uint64_t ns) { fake::ticks += ns; return 0; }
inline void vi2MakeBasicPresentOptions(Vi2PresentOptions *out, uint32_t, uint32_t, uint64_t) { *out = {}; }
inline Result vi2PresentLayer(Vi2Layer *, uint8_t, uint32_t image, Vi2PresentOptions *, Vi2Dependency *, uint64_t *, Vi2Dependency *release) {
    uint32_t token = ~uint32_t(9); uint64_t value = 7;
    memcpy(release->data, &token, 4); release->data[4] = 1; memcpy(release->data + 8, &value, 8);
    fake::presented = reinterpret_cast<const uint8_t *>(fake::memoryAddress[fake::imageMemory[image]]);
    return fake::failure(__func__);
}
inline Result vi2UnregisterLayerImage(Vi2Layer *, uint8_t, Vi2PresentOptions *, uint64_t *, Vi2Dependency *out) { fake::retired = true; *out = {}; return fake::failure(__func__); }
