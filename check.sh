#!/usr/bin/env bash
# Post-reboot verification. Read-only: changes nothing.
set -uo pipefail
say() { printf '\n== %s\n' "$*"; }

say "model"
tr -d '\0' </proc/device-tree/model; echo

say "sound cards (need sndrpihifiberry, and no bcm2835)"
cat /proc/asound/cards

say "playback devices"
aplay -l 2>&1 | grep -E 'card|Subdevices' || echo "  NO PLAYBACK DEVICES"

say "HiFiBerry mixer controls"
amixer -c sndrpihifiberry scontrols 2>&1 | head -10

say "boot config lines that matter"
CFG=/boot/firmware/config.txt; [ -f "$CFG" ] || CFG=/boot/config.txt
grep -nE '^(dtparam=audio|dtoverlay=(hifiberry|vc4)|force_eeprom)' "$CFG" || echo "  none found in $CFG"

say "service"
systemctl is-enabled musicboxd.service 2>&1
systemctl is-active musicboxd.service 2>&1
systemctl --no-pager --lines=0 status musicboxd.service 2>&1 | head -6

say "recent daemon log"
journalctl -u musicboxd --no-pager -n 20 2>&1 | tail -20

say "plugged-in USB storage"
for d in /sys/class/block/sd*; do
    [ -e "$d" ] || continue
    n=$(basename "$d")
    case "$n" in *[0-9]) echo "  /dev/$n  $(blkid -s TYPE -o value /dev/$n 2>/dev/null)  $(blkid -s LABEL -o value /dev/$n 2>/dev/null)";; esac
done

say "resume state"
cat /var/lib/musicbox/state.json 2>/dev/null || echo "  (nothing saved yet)"
