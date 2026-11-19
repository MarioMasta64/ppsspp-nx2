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
renames every defined core-owned global symbol together. Only eight plain-data
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
CPU-console uploader is disabled: this renderer owns its GSV GPU client,
address-space import, backing and native image registrations using current
public libounce APIs, without casting the host's default window or relying on
its private ABI. Coexistence of these native resources with two Vulkan contexts
requires physical qualification.

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
