#!/usr/bin/env python3
"""Package MiniQuad-F405 firmware images.

Patches the 512-byte application header (image size, image CRC-32, header
CRC-32 - see firmware/common/app_header.h) and writes to --out-dir:

    miniquad-app.bin / .hex / .elf    application (for the bootloader / web)
    miniquad-bootloader.bin / .hex / .elf
    miniquad-full.hex                 bootloader + application (keeps config sectors)
    miniquad-full.bin                 flat image from 0x08000000 (ERASES config sectors)
    manifest.json                     metadata used by the web configurator
"""
import argparse
import binascii
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

HDR_SIZE = 0x200
OFF_MAGIC = 0x000
OFF_IMAGE_SIZE = 0x010
OFF_IMAGE_CRC = 0x014
OFF_VERSION = 0x018
OFF_BUILD_TIME = 0x01C
OFF_GIT = 0x028
OFF_BOARD = 0x038
OFF_HDR_CRC = 0x1FC
APP_MAGIC = 0x5041514D


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def parse_memory_map(path: str) -> dict:
    mm = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"\s*#define\s+(MM_\w+)\s+(0x[0-9A-Fa-f]+|\d+)", line)
            if m:
                mm[m.group(1)] = int(m.group(2), 0)
    return mm


def ihex(segments) -> str:
    """Intel HEX for [(address, bytes), ...]."""
    out = []

    def rec(addr16, rtype, payload):
        b = bytes([len(payload), (addr16 >> 8) & 0xFF, addr16 & 0xFF, rtype]) + payload
        out.append(":" + b.hex().upper() + "%02X" % ((-sum(b)) & 0xFF))

    for base, data in segments:
        upper = None
        for i in range(0, len(data), 16):
            addr = base + i
            if (addr >> 16) != upper:
                upper = addr >> 16
                rec(0, 0x04, struct.pack(">H", upper))
            rec(addr & 0xFFFF, 0x00, data[i:i + 16])
    rec(0, 0x01, b"")
    return "\n".join(out) + "\n"


def cstr(b: bytes) -> str:
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def describe(path: str, address: int, data: bytes) -> dict:
    return {
        "file": os.path.basename(path),
        "address": "0x%08X" % address,
        "size": len(data),
        "crc32": "0x%08X" % crc32(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app-elf", required=True)
    ap.add_argument("--app-raw", required=True, help="objcopy -O binary output of the app ELF")
    ap.add_argument("--bl-raw", required=True)
    ap.add_argument("--bl-elf", required=True)
    ap.add_argument("--objcopy", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--memory-map", required=True)
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args()

    mm = parse_memory_map(a.memory_map)
    os.makedirs(a.out_dir, exist_ok=True)

    raw = open(a.app_raw, "rb").read()
    if len(raw) < HDR_SIZE + 8:
        sys.exit("app image too small")
    hdr = bytearray(raw[:HDR_SIZE])
    image = bytearray(raw[HDR_SIZE:])
    while len(image) % 4:
        image.append(0xFF)
    magic, = struct.unpack_from("<I", hdr, OFF_MAGIC)
    if magic != APP_MAGIC:
        sys.exit("app header magic not found at the start of the image (linker script?)")
    max_image = mm["MM_APP_MAX_SIZE"] - HDR_SIZE
    if len(image) > max_image:
        sys.exit("application too large: %d > %d bytes" % (len(image), max_image))

    struct.pack_into("<I", hdr, OFF_IMAGE_SIZE, len(image))
    struct.pack_into("<I", hdr, OFF_IMAGE_CRC, crc32(bytes(image)))
    struct.pack_into("<I", hdr, OFF_HDR_CRC, crc32(bytes(hdr[:OFF_HDR_CRC])))
    app = bytes(hdr) + bytes(image)

    version, = struct.unpack_from("<I", hdr, OFF_VERSION)
    build_time, = struct.unpack_from("<I", hdr, OFF_BUILD_TIME)
    git = cstr(bytes(hdr[OFF_GIT:OFF_GIT + 16]))
    board = cstr(bytes(hdr[OFF_BOARD:OFF_BOARD + 16]))

    out = lambda name: os.path.join(a.out_dir, name)
    app_base = mm["MM_APP_BASE"]
    bl_base = mm["MM_BL_BASE"]

    with open(out("miniquad-app.bin"), "wb") as f:
        f.write(app)
    with open(out("miniquad-app.hex"), "w") as f:
        f.write(ihex([(app_base, app)]))
    hdr_file = out("app_header.bin")
    with open(hdr_file, "wb") as f:
        f.write(bytes(hdr))
    subprocess.check_call([a.objcopy, "--update-section", ".app_header=" + hdr_file,
                           a.app_elf, out("miniquad-app.elf")])
    os.remove(hdr_file)

    bl = open(a.bl_raw, "rb").read()
    if len(bl) > mm["MM_BL_SIZE"]:
        sys.exit("bootloader too large: %d > %d bytes" % (len(bl), mm["MM_BL_SIZE"]))
    with open(out("miniquad-bootloader.bin"), "wb") as f:
        f.write(bl)
    with open(out("miniquad-bootloader.hex"), "w") as f:
        f.write(ihex([(bl_base, bl)]))
    shutil.copyfile(a.bl_elf, out("miniquad-bootloader.elf"))

    with open(out("miniquad-full.hex"), "w") as f:
        f.write(ihex([(bl_base, bl), (app_base, app)]))
    gap = app_base - bl_base - len(bl)
    with open(out("miniquad-full.bin"), "wb") as f:
        f.write(bl + b"\xFF" * gap + app)

    manifest = {
        "board": board,
        "version": "%d.%d.%d" % ((version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF),
        "version_u32": version,
        "git": git,
        "build_time": build_time,
        "files": {
            "app": describe(out("miniquad-app.bin"), app_base, app),
            "bootloader": describe(out("miniquad-bootloader.bin"), bl_base, bl),
        },
        "memory": {k: "0x%08X" % v for k, v in sorted(mm.items())},
    }
    with open(out("manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)

    print("fwpack: app %d B (%.1f%% of %d KB), bootloader %d B (%.1f%% of %d KB), v%s %s" % (
        len(app), 100.0 * len(app) / mm["MM_APP_MAX_SIZE"], mm["MM_APP_MAX_SIZE"] // 1024,
        len(bl), 100.0 * len(bl) / mm["MM_BL_SIZE"], mm["MM_BL_SIZE"] // 1024,
        manifest["version"], git))
    return 0


if __name__ == "__main__":
    sys.exit(main())
