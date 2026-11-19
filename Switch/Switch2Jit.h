// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#pragma once
#if defined(__SWITCH2__)
#include <switch.h>
void Switch2JitRegister(Jit *jit);
void Switch2JitUnregister(Jit *jit);
void Switch2JitPublish(const u8 *const *starts, const u8 *const *ends, int count);
#endif
