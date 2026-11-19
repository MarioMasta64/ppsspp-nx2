#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
cmake -S . -B build-switch2 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/libounce/cmake/Switch2.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    "$@" -DPPSSPP_SWITCH2_GAMESHARE=ON
cmake --build build-switch2 --target PPSSPPSwitch2_nro --parallel "${BUILD_JOBS:-8}"
python3 Tools/package-switch2.py --experimental-gameshare
