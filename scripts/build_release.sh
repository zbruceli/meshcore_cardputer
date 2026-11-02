#!/usr/bin/env bash
# Builds the firmware and merges bootloader + partition table + app into one image flashed at offset 0x0,
# the format M5Burner, ESP Web Tools, MeshCore's web flasher and esptool all take.
# Output: dist/meshcore-cardputer-v<version>.bin (version = FW_VERSION in platformio.ini)
set -euo pipefail
cd "$(dirname "$0")/.."

ENV=cardputer_adv_lora1262
VERSION=$(sed -n "s/.*FW_VERSION='\"\([^\"]*\)\"'.*/\1/p" platformio.ini)
B=.pio/build/$ENV
CORE=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}
# the Python and esptool PlatformIO itself uses for building/uploading (deps already installed)
PY=$(pio system info --json-output | python3 -c 'import json,sys; print(json.load(sys.stdin)["python_exe"]["value"])')

pio run -e $ENV
mkdir -p dist
OUT=dist/meshcore-cardputer-v$VERSION.bin
# the data partition (SPIFFS at 0x670000) is left out on purpose: each device makes its own identity,
# and upgrades keep contacts, channels and messages
"$PY" "$CORE/packages/tool-esptoolpy/esptool.py" --chip esp32s3 merge_bin -o "$OUT" \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 $B/bootloader.bin 0x8000 $B/partitions.bin \
  0xe000 "$CORE/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" 0x10000 $B/firmware.bin
echo "$OUT"
