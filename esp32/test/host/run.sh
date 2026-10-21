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

# fat16/fat32 carry the standard musicbox.txt (Spotify instruction + shuffle=on);
# the two variants exercise per-stick volume and an audio-only stick.
for spec in "fat16 16 " "fat32 64 " "fat16 16 local" "fat16 16 defaultvol" \
            "fat16 16 fragmented" "fat16 16 wifi" "fat16 16 wifi_alias" \
            "fat16 16 wifi_nosssid"; do
  set -- $spec
  kind=$1; size=$2; variant=${3:-}
  name="$kind${variant:+_$variant}"
  echo "== building $name image (${size} MB)"
  python3 makeimage.py "$TMP/$name.img" "$kind" "$size" "$variant"

  echo "== validating with fsck.vfat (independent)"
  fsck.vfat -n "$TMP/$name.img" | tail -1
done

echo "== compiling for the host (same sources the firmware uses)"
g++ -std=c++17 -O2 -Wall -Wextra -o "$TMP/test_fat" test_fat.cpp ../../src/fat_reader.cpp
g++ -std=c++17 -O2 -Wall -Wextra -I stubs -I ../../src \
    -o "$TMP/test_fs_adapter" test_fs_adapter.cpp ../../src/usb_fs.cpp ../../src/fat_reader.cpp
g++ -std=c++17 -O2 -Wall -Wextra -I stubs -I ../../src \
    -o "$TMP/test_logic" test_logic.cpp fake_players.cpp ../../src/controller.cpp \
    ../../src/spotify_backend.cpp ../../src/spotify_uri.cpp ../../src/musicbox_txt.cpp \
    ../../src/storage.cpp ../../src/resume.cpp ../../src/usb_fs.cpp ../../src/fat_reader.cpp
g++ -std=c++17 -O2 -Wall -Wextra -o "$TMP/test_fragmented" test_fragmented.cpp \
    ../../src/fat_reader.cpp
g++ -std=c++17 -O2 -Wall -Wextra -I . -I stubs -I ../../src -o "$TMP/test_provisioning" \
    test_provisioning.cpp ../../src/wifi_config.cpp ../../src/musicbox_txt.cpp \
    ../../src/usb_fs.cpp ../../src/fat_reader.cpp
g++ -std=c++17 -O2 -Wall -Wextra -I stubs -I ../../src \
    -o "$TMP/test_spotify" test_spotify.cpp fake_players.cpp ../../src/spotify_uri.cpp \
    ../../src/spotify_backend.cpp ../../src/controller.cpp ../../src/musicbox_txt.cpp \
    ../../src/storage.cpp ../../src/resume.cpp ../../src/usb_fs.cpp ../../src/fat_reader.cpp

status=0
for kind in fat16 fat32; do
  echo
  echo "== fat_reader against $kind"
  "$TMP/test_fat" "$TMP/$kind.img" || status=1
  echo "== fs::FS adapter against $kind"
  "$TMP/test_fs_adapter" "$TMP/$kind.img" || status=1
done

echo
echo "== fragmented file: cluster chain that jumps over a hole"
"$TMP/test_fragmented" "$TMP/fat16_fragmented.img" || status=1

echo
echo "== provisioning: wifi.txt parsing and the NVS round trip"
"$TMP/test_provisioning" "$TMP/fat16_wifi.img" "$TMP/fat16_wifi_alias.img" \
    "$TMP/fat16_wifi_nosssid.img" "$TMP/fat16_local.img" || status=1

echo
echo "== logic: musicbox.txt, ordering, resume, control vocabulary"
"$TMP/test_logic" "$TMP/fat16_local.img" "$TMP/fat16.img" "$TMP/fat16_defaultvol.img" || status=1

echo
echo "== spotify: reference->URI, backend verbs, controller integration"
"$TMP/test_spotify" "$TMP/fat16.img" || status=1

echo
[ "$status" -eq 0 ] && echo "ALL TESTS PASS" || echo "FAILURES"
exit "$status"
