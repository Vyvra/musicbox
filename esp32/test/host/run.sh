#!/usr/bin/env bash
# Host-side test for fat_reader: the same source the firmware compiles.
#
# Builds FAT16 and FAT32 images, has them validated by fsck.vfat (dosfstools - an
# independent implementation, which is what makes this a real test and not a
# self-consistency check), then compiles and runs the reader against both.
#
# No hardware, no ESP toolchain, no root: run it anywhere.
set -euo pipefail
cd "$(dirname "$0")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

for spec in "fat16 16" "fat32 64"; do
  set -- $spec
  kind=$1; size=$2
  echo "== building $kind image (${size} MB)"
  python3 makeimage.py "$TMP/$kind.img" "$kind" "$size"

  echo "== validating with fsck.vfat (independent)"
  fsck.vfat -n "$TMP/$kind.img" | tail -1
done

echo "== compiling the reader for the host"
g++ -std=c++17 -O2 -Wall -Wextra -o "$TMP/test_fat" test_fat.cpp ../../src/fat_reader.cpp

status=0
for kind in fat16 fat32; do
  echo
  echo "== running against $kind"
  "$TMP/test_fat" "$TMP/$kind.img" || status=1
done

echo
[ "$status" -eq 0 ] && echo "ALL IMAGES PASS" || echo "FAILURES"
exit "$status"
