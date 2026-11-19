// Copyright (c) 2012- PPSSPP Project.
// This program is free software under the GNU GPL, version 2 or later.
// See LICENSE.TXT for details.
#pragma once
#if defined(__SWITCH2__)
#include <climits>
// devkitA64/newlib can omit the C99 long-long limit macros in C++ mode.
#ifndef LLONG_MAX
#define LLONG_MAX __LONG_LONG_MAX__
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULLONG_MAX (LLONG_MAX * 2ULL + 1ULL)
#endif
#endif
