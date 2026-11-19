# PPSSPP GameShare experiment

This branch experiments with a separate PPSSPP guest running on the host Switch
2 and streamed to a stock GameShare client. It is not a mirrored controller or
a second PSP controller in the host emulator. Compilation and host tests do not
establish console operation or acceptable two-game performance.

The working single-instance checkpoint is `switch2-single-instance-working`
(commit `1c3b38a`). Its exact tested NRO, package and matching debug ELF remain
under `dist-switch2/checkpoints/switch2-single-instance-working/`.

## Build and install

```sh
sh Tools/build-switch2-gameshare.sh
python3 Tools/deploy-switch2.py 192.168.1.182 --experimental-gameshare
```

Open FTPD for the second command. A successful transfer is accepted without a
validation download. The experiment installs as **PPSSPP GameShare**, at
`sdmc:/switch/ppsspp-switch2-gameshare/ppsspp-switch2-gameshare.nro`. It leaves
the working `ppsspp-switch2` NRO and its settings/saves in place. The normal
`Tools/build-switch2.sh` selects GameShare OFF. Both builds reuse the build
cache, but packaging rejects mismatched native/experimental modes.

Launch through the same GameShare-capable native title override used for the
Moonlight SDK example. A generic title override is not known to grant the
required GameShare context. Choose **GameShare (experimental)** in the host's
main or pause menu, then join from the client's system GameShare screen.
The client should see its own PPSSPP menu, with a guest-only startup message,
and browse `sdmc:/switch/ppsspp-switch2/games`. Both browsers can access SD-card
games; no game files are copied into the package.

Choose the same GameShare action to stop sharing. The guest pauses between
sessions, retaining its independent emulator state. Exiting the guest's PPSSPP
ends that guest instance; restart the application before creating another.
Exit the host normally when done. If a real cleanup failure reports retained
resources, close the entire hosting title from HOME, rather than returning to
hbmenu. The SDK explicitly requires this for uncertain native ownership.

## Isolation and rendering

The build links a second PPSSPP frontend/core into a relocatable object and
renames every defined core-owned global symbol and ELF COMDAT group signature.
Only eight plain-data
C entry points are exported. The audit requires distinct Config, memory-base
and core-state symbols; the host also compares their runtime addresses before
starting. SDK/OS/C++ runtime entry points remain shared. The two emulators have
independent core, UI, filesystem state, JIT registries, Vulkan contexts and
settings/memory-stick directories. They share one OS process, so a fatal error
in either copy can still affect the entire app.

The host keeps its 1920x1080 output and 120 Hz request. The guest uses PPSSPP's
existing offscreen Vulkan path at 1280x720 with a 60 fps presentation target.
A readback callback transfers completed RGBA frames to a latest-frame mailbox;
a dedicated presenter uploads them to a separate native VI2 guest layer.
This first route adds CPU readback/swizzle cost; zero-copy is not claimed.

The GameShare SDK is vendored from Moonlight-Switch's research worktree.
`Switch/GameShare/sdk-provenance.json` records the source revision and file
hashes. Its MIT license and per-file ISC notices are retained. The SDK's pinned
CPU-console uploader is disabled. The guest renderer borrows the statically
linked OunceVK driver's GSV main and worker sessions and uses its registered VM.
It owns the guest image backing, registrations, client and completion event.
The adapter tracks the driver's occupied slots at the libounce call boundary
and reserves a free slot for guest range retirement. It does not access
OunceVK's internal structs. Retire the guest images before Vulkan teardown.
Coexistence and reuse on hardware still require qualification.

The SDK owns matchmaking, native hosting, controller reads and the separate
VI2 display/layer. Renderer retirement runs before native Finalize; layer
closure runs afterwards. Uncertain presentation or release/retirement failure
retains backing and blocks ordinary unload. Guest controls never enter the
host emulator. Stale/disconnected snapshots become neutral.

## Storage, audio and current limits

Host settings/saves use `ppsspp-switch2-gameshare/config/ppsspp`; guest
settings/saves use `ppsspp-switch2-gameshare/guest/config/ppsspp`. The existing
working app's memory stick is not reused. Guest native text-entry applets are
disabled because their routing to the remote screen is not implemented.

The SDK shares application audio. This experiment mixes both emulator streams
into the application's output, with headroom while hosting. It does not claim
separate host-only/guest-only audio routing. Audible GameShare audio still
requires a device test, even though native PPSSPP audio worked previously.

PSP ad hoc multiplayer between the copies is not yet configured or qualified.
Moonlight's research found LAN disruption during GameShare; that does not prove
whether loopback traffic between these local cores survives. Separate-game
video/input and safe lifecycle are the first console checks. Measured FPS,
sustained simultaneous gameplay and guest restart behavior remain unqualified.

## Diagnostics and validation

Host `boot.log`, `ppsspp.log`, `native-exit.bin` and `native-fault.bin` live in
the experimental app directory. The guest writes `guest/ppsspp.log`. Native
fault/exit records with valid headers are retained as `.previous` on the next
launch. Preserve diagnostics before relaunching after a failure.

```sh
python3 Switch/tests/run.py
python3 Switch/tests/run_gameshare.py
python3 Switch/GameShare/sdk/tests/native/run.py
python3 Switch/GameShare/sdk/tests/guest_input/run.py
python3 Switch/GameShare/sdk/tests/guest_display/run.py
git diff --check
```

Actual-source sanitizer tests cover native guest-image sector locations,
partial creation, successful retirement, retained uncertain presents and
release/range timeouts. SDK tests cover native lifecycle/cancellation,
controller parsing/freshness and display ownership. They do not simulate the
GPU, native title permissions, GameShare radio behavior or console performance.
CPU, GPU, threading and shutdown regression risks require console testing;
serialized PSP state layouts are unchanged.

## Initial host validation (2026-11-19)

The dual-core AArch64 NRO and ZIP built and validated. The symbol audit found
27,173 renamed core-owned symbols and eight guest ABI exports. All 28 SDK
native lifecycle scenarios, the input parser/freshness suite and display
lifetime suite passed under ASan/UBSan. Six actual-source image-owner scenarios
passed (clean, partial create, uncertain present, release timeout, range
retirement timeout and unregister failure). Existing JIT publication and
dedicated-worker tests also passed.

Host regression suites: 102 C++ unit tests passed; 527 executed single-instance
PSP cases and ten paired relay scenarios passed. Eleven paired P2P cases remain
unrun for the existing missing `127.0.0.2` host loopback alias, causing the
aggregate runner's failure status. No executed regression test failed.
Console dual-instance rendering, pairing, guest controls/audio and sustained
performance still require physical qualification.

## First console launch investigation (2026-11-19)

The initial experimental build produced a system error before the PPSSPP menu.
Its boot log reached display configuration; native fault/exit records remained
empty. The separate working single-instance installation is unchanged. Pairing
and the second emulator have not yet been reached on the console.

The SDK bridge is compiled as C++20 separately from the C++17 frontend. Startup
checkpoints around Vulkan initialization write `startup-stage.bin` directly
with native flushes. Its four little-endian 64-bit words are magic
`0x3254475350505350`, stage number, recorder address and system tick. Stages
1–4 cover diagnostics, display configuration, graphics-object construction and
error-string construction; stages 10 and 11 bracket InitAPI's initial string
assignment. Stages 20 and 21 bracket NativeInit, and 22 precedes window setup.
The diagnostic boot marker is `comdat-isolation`. The native exit recorder
also wraps `svcBreak` (kind 6, reason in the high 32 bits and a readable four-byte
detail in the low 32 bits).

The final ELF showed that the linker turned a required host string-constructor
call into a no-op. GCC used a local C5 signature for the constructor group.
Renaming only global symbols let the guest group retain that signature, causing
the linker to discard the host copy. The build now renames and checks all 7,351
guest COMDAT groups in addition to the global symbol audit. Disassembly of the
corrected ELF confirms that host startup calls its own string constructor and
the guest constructor has a distinct address.

The console opened the corrected build and started a host game. On the first
GameShare attempt, VI2 reached the renderer hook, where a second GSV GPU open
failed with `0x200F3`. The guest emulator independently reached offscreen
Vulkan initialization. The revised uploader uses the existing driver session.
The host also joins the SDK thread after terminal states before asking if
native resources were retained, so a completed thread alone no longer forces a
title restart. Confirm pairing, guest frames and stop/restart on hardware.

The shared-session build paired with the client, and the SDK completed cleanup.
The client remained black because PPSSPP rejected readback from the guest's
offscreen Vulkan presentation before any frame could reach the uploader.
Offscreen presentation now declares its transfer-source capability. The copy
waits for the color attachment writes and skips frames without an acquired
image. A local MoltenVK run cleared three consecutive offscreen frames to
different colors and verified every returned RGBA byte. Client presentation
and independent input still require a console check.

The console confirmed that the host and client can see and control their own
PPSSPP screens. Both emulator audio streams were audible from the host. The
console-confirmed NRO, matching ELF and installation receipt are saved under
`dist-switch2-gameshare/checkpoints/dual-screen-controls/`.

The client was silent in separate-display sessions with both AudOut and
Audren. The Audren test boot log confirms the application mix used Audren,
the guest received frames and SDK cleanup completed. The launcher NRO was
restored to the console-confirmed dual-screen build using the saved SD-card
copy. The renderer trial is recorded in commit `c5a8716`.

The validated audio path currently plays both games on the host. The SDK
exposes an application-wide mirroring flag and device-volume configuration.
A separate guest audio route remains unverified. Another audio backend
replacement should be preceded by evidence of a native guest audio route or a
controlled mirror-mode comparison.
