#!/usr/bin/env python3
"""Validate a freshly built native NRO, then create an SD-card ZIP and receipt."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
source = root / "build-switch2/PPSSPPSwitch2.nro"
elf = root / "build-switch2/PPSSPPSwitch2.elf"
if not elf.exists():
    elf = root / "build-switch2/PPSSPPSwitch2"
elf_data = elf.read_bytes()
assert elf_data[:4] == b"\x7fELF" and elf_data[4:6] == b"\x02\x01", "Expected ELF64 little endian"
assert struct.unpack_from("<H", elf_data, 18)[0] == 183, "Expected AArch64 executable"
blob = source.read_bytes()
assert blob[16:20] == b"NRO0", "Missing NRO header"
image_size = struct.unpack_from("<I", blob, 24)[0]
assert blob[image_size:image_size + 4] == b"ASET", "Missing embedded assets"
sections = {}
for index, name in enumerate(("icon", "nacp", "romfs")):
    offset, size = struct.unpack_from("<QQ", blob, image_size + 8 + 16 * index)
    assert offset >= 56 and size and image_size + offset + size <= len(blob), name
    sections[name] = {"offset": image_size + offset, "bytes": size}
assert sections["nacp"]["bytes"] == 16384, "Invalid NACP size"
icon = sections["icon"]
assert blob[icon["offset"]:icon["offset"] + 2] == b"\xff\xd8", "Expected JPEG menu icon"
romfs = sections["romfs"]
romfs_data = blob[romfs["offset"]:romfs["offset"] + romfs["bytes"]]
assert b"gitdir:" not in romfs_data, "Git bookkeeping must not enter ROMFS"
for filename in (b"images.svg", b"font_atlas.zim", b"ppge_atlas.zim", b"compat.ini", b"ltn0.pgf", b"en_US.ini"):
    assert filename in romfs_data, f"Missing runtime asset: {filename!r}"
output = root / "dist-switch2"
app = output / "switch/ppsspp-switch2"
app.mkdir(parents=True, exist_ok=True)
shutil.copy2(source, app / "ppsspp-switch2.nro")
shutil.copy2(root / "SWITCH2.md", output / "README-switch2.md")
licenses = output / "licenses"
licenses.mkdir(exist_ok=True)
shutil.copy2(root / "LICENSE.TXT", licenses / "PPSSPP.txt")
shutil.copy2(root / "ext/aemu_postoffice/gpl-3.0.txt", licenses / "aemu-postoffice-GPL-3.0.txt")
shutil.copy2(root / "ext/aemu_postoffice/LICENSE", licenses / "aemu-postoffice-NOTICE.txt")
for relative in ("ext/ffmpeg/COPYING.LGPLv2.1", "ext/glslang/LICENSE.txt", "ext/SPIRV-Cross/LICENSE"):
    origin = root / relative
    shutil.copy2(origin, licenses / (origin.parent.name + "-" + origin.name))
devkit = Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))
for relative in ("libounce/licenses", "portlibs/switch2/share/licenses"):
    origin = devkit / relative
    if origin.is_dir():
        shutil.copytree(origin, licenses / ("SDK-" + origin.parent.name), dirs_exist_ok=True)
revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
receipt = {"upstream_revision": revision, "nro_bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
           "assets": sections, "console_tested": False}
(output / "build-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
archive = output / "ppsspp-switch2.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
    for directory in (output / "switch", licenses):
        for path in sorted(directory.rglob("*")):
            if path.is_file():
                package.write(path, path.relative_to(output))
    for name in ("README-switch2.md", "build-receipt.json"):
        package.write(output / name, name)
with zipfile.ZipFile(archive) as package:
    assert package.testzip() is None
    assert hashlib.sha256(package.read("switch/ppsspp-switch2/ppsspp-switch2.nro")).hexdigest() == receipt["sha256"]
print(json.dumps(receipt, indent=2))
print(f"Verified package: {archive}")
