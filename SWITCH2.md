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

The first launch defaults to 1x PSP rendering resolution. Vulkan presentation
uses fixed 1920x1080 output in handheld and docked modes, requesting 120 Hz
with a 120 fps presentation target and a 60 fps fallback on 60 Hz displays.
VRR is allowed where supported. The menu limiter follows the active native
presentation interval, including refresh-rate changes. `boot.log` records the
requested dimensions and changes to the presentation target; these are not
measured frame rates. Games retain their original emulation speed and frame
rate; 1080p output does not change the internal PSP rendering resolution.
The port uses checked guest memory accesses and requests no CPU/GPU overclocking.
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
- The user confirmed that the PPSSPP menu appears on Switch 2 and controller
  navigation works. Audible output, game loading and sustained speed still
  await device qualification.

## 1080p/120 fps output update

The output policy above supersedes the initial 720p/60 configuration. Console
confirmation of the first build covers its menu and controls only; this new
resolution/refresh policy requires another device test.

## Game-start diagnostics (2026-11-19)

The user confirmed that the 1080p build's menu works. Its native presentation
interval reported a 120 fps target; this is not a measured game frame rate.
Tekken: Dark Resurrection (ULUS10139) hit the guest crash screen in `strlen`
at PC `08a6db2c`, reading address `00000022` with the ARM64 JIT. A temporary
CPU-interpreter test progressed through MPEG module loading, then returned
to hbmenu. Neither backend has qualified gameplay yet.

The diagnostic build embeds the standard PPSSPP logo as a 256x256 JPEG NRO
icon. Packaging checks that the embedded icon matches that asset exactly.
The logo comes from `icons/hicolor/256x256/apps/ppsspp.png`.

A native fault recorder preopens `native-fault.bin` before graphics startup.
The SDK exception hook writes a fixed-size record through the native FS API,
without allocating or using stdio on the faulting thread. Its header contains
magic `0x31544c5541465050`, the runtime handler address, the system tick and
the SDK context size, followed at offset 32 by `ThreadExceptionDump`. The
runtime handler address anchors symbolization against the matching ELF.
The file is cleared on each launch; a zero header means no fault was recorded.
This is best-effort diagnosis, not exception recovery. Normal main-loop exit
is logged separately in `boot.log`. Retrieve diagnostic files before relaunch.

The first instrumented interpreter run returned to hbmenu without populating
the exception record or logging normal main-loop completion. Explicit exit,
abort, fatal and diagnostic-abort calls are therefore also traced into
`native-exit.bin`. It contains magic `0x3154495845505050`, a recorder-address
anchor, kind (1 exit, 2 _exit, 3 abort, 4 diagnostic abort, 5 fatal), result,
system tick and up to 24 return addresses. Each frame read is bounded by a
successful readable-memory query. Both recorders preserve the original SDK
termination behavior and clear their previous report at launch.

The exit trace for revision `291ecd5` identified `abort` through
`std::thread::detach`, called by `ThreadManager::EnqueueTask` during the first
Vulkan game-shader compilation. Switch 2 dedicated tasks now retain joinable
threads, reap completed jobs at the next submission and join outstanding jobs
before pool shutdown. Dedicated work still starts immediately and stays off
the shared pool. Other platforms keep their existing detach path. The focused
sanitizer checks cover asynchronous execution, nested jobs during shutdown,
concurrent producers, reuse and destructor cleanup. Regression risk is worker
lifetime and shutdown ordering; a stuck dedicated task can delay shutdown.
This fixes the traced termination path; Tekken gameplay and the original
ARM64-JIT guest-memory error still require console qualification.
