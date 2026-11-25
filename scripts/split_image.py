#!/usr/bin/env python3
"""Splits the merged release image (flashed at 0x0) into the parts the web installer writes one by one.

The merged image fills the gap between the partition table and boot_app0 (0x9000-0xDFFF) with 0xFF. That gap is
the NVS partition, where the Bluetooth bonds live, so writing the merged image forgets the phone pairing. Writing
the parts at their own offsets leaves NVS (and SPIFFS) alone.

Usage: split_image.py <merged.bin> <out_dir>   -> bootloader.bin, partitions.bin, boot_app0.bin, app.bin
"""
import os
import sys

PARTS = [   # name, start, end (None = to the end of the image); offsets match manifest.json
    ("bootloader.bin", 0x0000, 0x8000),
    ("partitions.bin", 0x8000, 0x9000),
    ("boot_app0.bin", 0xE000, 0x10000),   # otadata: must be written whole (0xFF = boot the first app)
    ("app.bin", 0x10000, None),
]

src, out = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
if len(data) <= 0x10000 or data[0] != 0xE9 or data[0x10000] != 0xE9:   # ESP image magic at both images
    sys.exit(f"{src}: not a merged ESP32 image")
os.makedirs(out, exist_ok=True)
for name, start, end in PARTS:
    with open(os.path.join(out, name), "wb") as f:
        f.write(data[start:end])
print(" ".join(f"{n}@{s:#x}" for n, s, _ in PARTS))
