#!/usr/bin/env bash
# Write a Raspberry Pi OS image to an SD card. Destroys the target disk.
#
#   sudo bash scripts/flash-sd.sh images/2026-10-06-raspios-trixie-armhf-lite.img.xz /dev/sda
#
# Guards, in order: must be root; target must be a block device; must be
# USB-attached (never the NVMe/eMMC the machine boots from); must not back any
# mounted filesystem; must be large enough. Then it asks you to type the device
# name back. --yes skips only that last question.
set -euo pipefail

IMAGE="${1:-}"
DEV="${2:-}"
ASSUME_YES="${3:-}"

die() { printf '\nREFUSING: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run as root (sudo bash $0 IMAGE DEVICE)"
[ -n "$IMAGE" ] && [ -n "$DEV" ] || die "usage: $0 IMAGE DEVICE [--yes]"
[ -f "$IMAGE" ] || die "image not found: $IMAGE"
[ -b "$DEV" ] || die "not a block device: $DEV"

DEV="$(readlink -f "$DEV")"

# --- identiy checks -------------------------------------------------------
MODEL="$(lsblk -dno MODEL "$DEV" | xargs || true)"
SIZE_BYTES="$(lsblk -bdno SIZE "$DEV")"
TRAN="$(lsblk -dno TRAN "$DEV" | xargs || true)"
SYSFS="$(readlink -f "/sys/class/block/$(basename "$DEV")")"

case "$(basename "$DEV")" in
    nvme*|mmcblk*|zram*) die "$DEV is an internal disk, not a removable card" ;;
esac
case "$SYSFS" in
    */usb*) : ;;
    *) die "$DEV is not USB-attached (sysfs: $SYSFS)" ;;
esac

ROOT_SRC="$(findmnt -no SOURCE / | sed 's/\[.*//')"
ROOT_PARENT="$(lsblk -no PKNAME "$ROOT_SRC" 2>/dev/null | head -1)"
[ "/dev/$ROOT_PARENT" = "$DEV" ] && die "$DEV holds the running system"

MOUNTED="$(lsblk -no MOUNTPOINT "$DEV" | grep -v '^$' || true)"
[ -z "$MOUNTED" ] || die "$DEV has mounted filesystems: $MOUNTED"

# --- does it even fit? ----------------------------------------------------
if [[ "$IMAGE" == *.xz ]]; then
    IMG_BYTES="$(xz -l --robot "$IMAGE" | awk -F'\t' '$1=="totals"{print $5}')"
else
    IMG_BYTES="$(stat -c %s "$IMAGE")"
fi
[ "$SIZE_BYTES" -ge "$IMG_BYTES" ] || die "image ($IMG_BYTES bytes) does not fit on $DEV ($SIZE_BYTES bytes)"

cat <<EOF

Target : $DEV
Model  : ${MODEL:-unknown} (transport: ${TRAN:-unknown})
Size   : $(numfmt --to=iec "$SIZE_BYTES")
Image  : $IMAGE ($(numfmt --to=iec "$IMG_BYTES") uncompressed)

Current contents of $DEV:
$(lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV")

*** EVERYTHING ON $DEV WILL BE DESTROYED. ***
EOF

if [ "$ASSUME_YES" != "--yes" ]; then
    printf 'Type the device path to confirm (%s): ' "$DEV"
    read -r CONFIRM
    [ "$CONFIRM" = "$DEV" ] || die "confirmation did not match"
fi

echo
if [[ "$IMAGE" == *.xz ]]; then
    xzcat "$IMAGE" | dd of="$DEV" bs=4M conv=fsync status=progress
else
    dd if="$IMAGE" of="$DEV" bs=4M conv=fsync status=progress
fi
sync
partprobe "$DEV" 2>/dev/null || true
udevadm settle 2>/dev/null || true

cat <<EOF

Done. $DEV now holds the image.
First boot resizes the root filesystem to fill the card (the 'resize' flag in
cmdline.txt), so nothing needs doing by hand.
EOF
lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV"
