// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#pragma once

#include <cstdint>

constexpr uint16_t GAMESHARE_ADHOC_CTL_PORT = 27314;
constexpr uint16_t GAMESHARE_ADHOC_RELAY_PORT = 27315;

// Each PSP core selects its route when it initializes ad hoc.
void SetGameShareAdhocEnabled(bool enabled);
bool GameShareAdhocNeedsRestart();
bool IsGameShareAdhoc();
