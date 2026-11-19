#pragma once
#include <switch.h>
struct KaihenJitRange { u64 offset; u64 size; };
extern "C" Result kaihenJitPublishRanges(Jit *, const KaihenJitRange *, size_t);
