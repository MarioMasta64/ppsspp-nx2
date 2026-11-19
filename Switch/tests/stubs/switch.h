#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
using u8 = uint8_t;
using u64 = uint64_t;
using Result = uint32_t;
struct Jit { void *rx_addr; void *rw_addr; size_t size; };
inline void *jitGetRxAddr(Jit *jit) { return jit->rx_addr; }
inline void *jitGetRwAddr(Jit *jit) { return jit->rw_addr; }
#define R_FAILED(rc) ((rc) != 0)
[[noreturn]] inline void fatalThrow(Result) { std::abort(); }
