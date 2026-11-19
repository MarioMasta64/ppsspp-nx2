// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#pragma once

void Switch2DiagnosticsInit(const char *directory = "sdmc:/switch/ppsspp-switch2/");
void Switch2DiagnosticsShutdown();
void Switch2DiagnosticsCheckpoint(unsigned int stage);
