#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the table and browser tests with a staged SDK: python3 tests/run.py SDK."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="album-artists-tests-") as name:
    work = Path(name)
    # Widen the pointer-bearing native layouts for the host; record offsets stay.
    header = (sdk / "osos-2.0.5.h").read_text()

    def widen(match):
        key, value = match.groups()
        if key.startswith(("ITunesReader_", "MediaBrowser_", "MediaList_", "MediaEntry_",
                           "MediaOrder_", "TrackData_", "LibraryBrowser_",
                           "MediaListSelection_")):
            return f"#define {key} {int(value, 16) * 2:#x}"
        return match[0]

    (work / "osos-2.0.5.h").write_text(
        re.sub(r"#define (\w+) (0x[0-9a-f]+)", widen, header)
    )
    for header in ("rv_api.h", "rv_libc.h", "rv_media.h"):
        (work / header).write_text((sdk / header).read_text())
    for source in ("contracts.c", "browser_contracts.c"):
        executable = work / source[:-2]
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-g",
                "-fsanitize=address,undefined",
                "-I",
                str(work),
                str(root / source),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
print("Album Artists contracts passed")
