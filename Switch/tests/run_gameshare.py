#!/usr/bin/env python3
"""Exercise the actual guest image owner against synthetic native services."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='ppsspp-gameshare-renderer-') as work:
    executable = Path(work) / 'renderer'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', str(root / 'Switch/tests/gameshare-fakes'), '-I', str(root),
                    str(root / 'Switch/tests/gameshare_renderer.cpp'),
                    str(root / 'Switch/GameShare/CpuRenderer.cpp'), '-o', str(executable)], check=True)
    for mode in ('clean', 'partial', 'present-failure', 'release-timeout', 'range-timeout', 'unregister-failure'):
        environment = os.environ.copy()
        if mode not in ('clean', 'partial'):
            environment['ASAN_OPTIONS'] = environment.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
        subprocess.run([str(executable), mode], check=True, env=environment, timeout=10)
        print('Guest image ownership:', mode, 'passed', flush=True)
