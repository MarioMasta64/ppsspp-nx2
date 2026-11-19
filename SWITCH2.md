# PPSSPP for Nintendo Switch 2 (kaiHEN)

This native port uses libounce, OunceVK/Vulkan, PPSSPP's existing UI and ARM64
recompiler. It is the first single-emulator stage of the dual-PSP/GameShare
project. A successful cross build does not establish console gameplay.

## Build

Install the public [ounce-devkit](https://github.com/ouncebrew/ounce-devkit)
into devkitPro, including libkaihen and its persistent JIT publication hooks.
The port uses `kaihenJitPublishRanges`; the runtime HEN/RO must support that
API (protocol 6 or a compatible newer protocol). The toolchain must provide
`portlibs/switch2/lib/kaihen-jit.o`.

```sh
git submodule update --init --recursive
sh Tools/build-switch2.sh
python3 Tools/package-switch2.py
```

Set `DEVKITPRO` to use a different SDK installation (default `/opt/devkitpro`).
Set `BUILD_JOBS` to change build concurrency (default 8). The build uses
`libounce/cmake/Switch2.cmake`, never the original Switch libnx toolchain.
Bundled FFmpeg is enabled for PSP media; mmap/mprotect and unsupported
`sysconf` probing are disabled for it. Runtime asset changes trigger repacking;
Git bookkeeping is excluded from ROMFS.

## Install and use

Extract `dist-switch2/ppsspp-switch2.zip` onto the SD card. Launch
`sdmc:/switch/ppsspp-switch2/ppsspp-switch2.nro` through kaiHEN hbmenu in a
Switch 2 title override. Applet-mode launch is not qualified.

For FTP deployment, leave FTPD open and run
`python3 Tools/deploy-switch2.py CONSOLE_IP` (port 5000 by default). A successful
FTP transfer is accepted without a validation download, as requested. The
installer writes `dist-switch2/installation.json`.

The NRO contains the fonts, UI atlases, translations, shaders and flash0
resources in ROMFS. No PSP games or firmware dumps are included. Browse the
SD card for your PSP games; the default memory stick and settings live under
`sdmc:/switch/ppsspp-switch2/config/ppsspp`. This uses a separate app directory
from existing Switch 1 PPSSPP installations.

- B / A / Y / X map by position to PSP Cross / Circle / Square / Triangle.
- D-pad and left stick control the PSP; L/R are PSP shoulders.
- Plus is PSP Start; Minus is PSP Select.
- ZL or right-stick click opens the PPSSPP pause menu.
- Use PPSSPP's Exit action to return to hbmenu.

The first launch defaults to 1x PSP rendering resolution. Presentation uses
fixed 1280x720 output at 60 Hz, Vulkan, and
checked guest memory accesses. It does not request CPU/GPU overclocking.
Touch, rumble, camera, microphone, HTTPS services and external file-picker
applets are not implemented. PPSSPP's in-app game browser is available.
Controller bindings can be changed in PPSSPP's settings.

## JIT and memory

kaiHEN supplies an executable allocation plus a separate writable staging
buffer. The Switch 2 adapter registers each code allocation and publishes
only the emitter's dirty ranges before execution, including branch patches
and vertex decoders. The executable allocation is never treated as writable.
The startup self-test publishes and executes `mov w0, #42; ret` and records
its result and the process core mask in `boot.log`. Emulator diagnostics go
to `ppsspp.log` in the same application directory.

The Switch 1 process-memory alias SVCs are unavailable in this launch role.
The port uses PPSSPP's existing NO_MMAP/MASKED_PSP_MEMORY mode with an owned
256 MiB backing allocation, bounds-checked views and checked-memory JIT.
This mode does not provide distinct hardware-backed VRAM mirror aliases or
a Media Engine address space. Games depending on those features need further
qualification. No serialized-state layout was changed.

Networking uses libounce BSD sockets. The aemu/ENet backend is IPv4-only and
has a generated IPv4 client adapter; the pinned dependency is unchanged.
Its source is [aemu_postoffice](https://github.com/Kethen/aemu_postoffice),
licensed GPL-3.0-only; its GPL text and notice are included in the package.
Ad hoc multiplayer on this port remains untested.

## Local checks

```sh
python3 Switch/tests/run.py
git diff --check
```

The focused sanitizer test compiles the actual JIT publication registry with
mock native services and separate staging/executable buffers. It checks dirty
ranges, batching, bounds rejection, unregistration and allocation reuse.
It does not emulate RO, GPU, audio or CPU cache behavior.

The package tool validates the AArch64 ELF, NRO/asset boundaries, icon, 16 KiB
NACP, essential ROMFS assets, ZIP integrity and the packaged NRO hash.
`dist-switch2/build-receipt.json` records the resulting NRO and verification
status. Device launch, UI, audio, JIT gameplay and sustained speed require
physical testing.

## Existing GameShare SDK for stage two

Recovered from the chat **Continue GameShare research** and this worktree:

`/Users/daniilvinogradov/.codex/worktrees/gameshare-research/Moonlight-Switch/libraries/gameshare`

The reusable C++20 library exports `gameshare::gameshare` and supports one
local native guest, a separate application-rendered guest image and guest
controller state. The SDK example's animated separate image, A/B response
and normal stop/exit were confirmed by the user. Audible shared audio is
still unqualified. See its `README.md`, `docs/QUALIFICATION.md`,
`docs/RENDERING.md` and `docs/LIFECYCLE.md` before integration.

Moonlight's external PC connection stops carrying traffic during native
GameShare initialization. That finding does not establish whether two local
PPSSPP instances could communicate through loopback during a session; that
needs its own test. Native GameShare also requires a suitable title-override
context. This port does not start GameShare or modify its SDK.

After single-instance gameplay works, the next steps are two isolated emulator
contexts, local ad hoc communication, then a Vulkan guest-display adapter and
separate guest input/audio. PPSSPP currently contains process-global core
state, so a second thread alone is not an independent emulator instance.

## Initial validation (2026-11-19)

- Release cross build: devkitA64 GCC 16.1.0, native libounce/OunceVK; passed.
- Host unit tests: `PPSSPPUnitTest all`, 102 passed.
- `python3 test.py -g --graphics=software`: 527 executed single-instance cases
  passed (including the main 507-case suite and additional ad hoc modes),
  with no executed failures or missing binaries. Ten paired relay scenarios
  passed. The runner skipped its known relay failures. Eleven paired P2P
  scenarios could not run because this Mac lacks the `127.0.0.2` loopback
  alias; the aggregate runner therefore exited unsuccessfully. No host
  network configuration was changed.
- Focused Switch 2 JIT staging/publication sanitizer checks: passed.
- Fresh NRO/ELF, embedded assets and ZIP validation: passed. The NRO is
  48,967,893 bytes; its local SHA-256 is
  `c8a75d31128e3d8a1499907035dd61673c9c69ab06a919a20cd2749dcd4492fb`.
- FTP installation returned `226 OK` for
  `sdmc:/switch/ppsspp-switch2/ppsspp-switch2.nro`; no validation download.
- Console UI, controls, audible output, game loading and sustained speed
  await device qualification.
