#!/usr/bin/env python3
"""Stage fresh runtime assets without Git bookkeeping."""
from pathlib import Path
import shutil
import sys

source = Path(__file__).resolve().parents[1] / "assets"
output = Path(sys.argv[1]).resolve()
if output.name != "romfs-switch2" or output == source or source in output.parents:
    raise SystemExit("Expected a separate build/romfs-switch2 output directory")
if output.exists():
    shutil.rmtree(output)
shutil.copytree(source, output, ignore=shutil.ignore_patterns(".git", "__pycache__"))
print("Staged", sum(p.is_file() for p in output.rglob("*")), "runtime asset files")
