#!/usr/bin/env python3
"""Install the native test NRO over FTP, without downloading it for validation."""
import argparse
from ftplib import FTP, error_perm
import hashlib
import json
from pathlib import Path
import time

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("host")
parser.add_argument("--port", type=int, default=5000)
args = parser.parse_args()
source = root / "dist-switch2/switch/ppsspp-switch2/ppsspp-switch2.nro"
blob = source.read_bytes()
assert blob[16:20] == b"NRO0", "Expected a native NRO"
with FTP() as ftp:
    ftp.connect(args.host, args.port, timeout=30)
    ftp.login()
    ftp.cwd("/switch")
    try:
        ftp.mkd("ppsspp-switch2")
    except error_perm:
        ftp.cwd("ppsspp-switch2")
        ftp.cwd("..")
    ftp.cwd("ppsspp-switch2")
    filename = "ppsspp-switch2.nro"
    existing = {Path(item).name for item in ftp.nlst()}
    temporary = ".ppsspp-switch2.nro.upload"
    with source.open("rb") as stream:
        reply = ftp.storbinary("STOR " + temporary, stream, blocksize=65536)
    assert reply.startswith("226"), reply
    backup = None
    if filename in existing:
        backup = filename + ".previous-" + str(int(time.time()))
        ftp.rename(filename, backup)
    ftp.rename(temporary, filename)
receipt = {"host": args.host, "port": args.port,
           "remote_path": "/switch/ppsspp-switch2/ppsspp-switch2.nro",
           "bytes": len(blob), "local_sha256": hashlib.sha256(blob).hexdigest(),
           "upload_reply": reply, "remote_backup": backup,
           "download_verification": False, "console_launched": False}
(root / "dist-switch2/installation.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
