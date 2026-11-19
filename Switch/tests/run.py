#!/usr/bin/env python3
"""Exercise the actual Switch 2 publication registry with separate staging/RX buffers."""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="ppsspp-switch2-jit-") as temp:
    binary = Path(temp) / "jit-publish"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-D__SWITCH2__",
                    "-fsanitize=address,undefined", "-g", "-I", str(root / "Switch/tests/stubs"),
                    "-I", str(root), str(root / "Switch/tests/jit_publish.cpp"),
                    str(root / "Switch/Switch2Jit.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    for case in ("cross-boundary", "reversed", "unregistered"):
        result = subprocess.run([str(binary), case], capture_output=True)
        assert result.returncode == -signal.SIGABRT, (case, result.returncode, result.stderr)
    print("Switch 2 JIT publication: staging, batching, bounds, release/reuse passed")
