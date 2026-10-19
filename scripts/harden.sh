#!/usr/bin/env bash
# SD-card wear hardening for the music box appliance.
#
#   sudo bash scripts/harden.sh --check      report only, changes nothing
#   sudo bash scripts/harden.sh              apply journald + fstab changes
#   sudo bash scripts/harden.sh --overlay    also make the root filesystem read-only
#   sudo bash scripts/harden.sh --no-updates also disable the apt-daily timers
#
# A cheap SD card dies from writes, not reads. The music box writes almost nothing
# (no logs of interest, no state beyond one small JSON file), so the wins here are
# cheap and worth taking.
set -euo pipefail

MODE=apply; OVERLAY=no; NOUPDATES=no
for a in "$@"; do
    case "$a" in
        --check) MODE=check ;;
        --overlay) OVERLAY=yes ;;
        --no-updates) NOUPDATES=yes ;;
        *) echo "unknown option: $a" >&2; exit 1 ;;
    esac
done

[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 1; }
DROPIN=/etc/systemd/journald.conf.d/90-musicbox.conf
say() { printf '\n== %s\n' "$*"; }

say "current state"
printf 'journald Storage      : %s\n' "$(systemctl show systemd-journald -p LogStorageName 2>/dev/null || grep -hE '^\s*Storage=' /etc/systemd/journald.conf /etc/systemd/journald.conf.d/*.conf 2>/dev/null | tail -1 || echo '(default: auto)')"
printf 'root mount options    : %s\n' "$(findmnt -no OPTIONS / )"
printf 'root overlay          : %s\n' "$(findmnt -no FSTYPE / )"
if [ "$MODE" = check ]; then
    printf 'apt-daily enabled     : %s\n' "$(systemctl is-enabled apt-daily.timer 2>/dev/null || echo unknown)"
    printf '\n(run without --check to apply)\n'
    exit 0
fi

say "journald -> volatile (RAM), 64 MB cap"
install -d -m 0755 /etc/systemd/journald.conf.d
cat >"$DROPIN" <<'EOF'
# Music box appliance: keep logs in RAM. The SD card only has to survive.
# Trade-off: 'journalctl -b' shows the current boot only; nothing is lost that
# matters, and the daemon's own decisions are re-derivable from one boot.
[Journal]
Storage=volatile
RuntimeMaxUse=64M
EOF
systemctl restart systemd-journald

say "root mount -> commit=600"
FSTAB=/etc/fstab
if grep -qE 'commit=600' "$FSTAB"; then
    echo "already set"
else
    cp -a "$FSTAB" "$FSTAB.bak-musicbox-$(date +%Y%m%d%H%M%S)"
    # add commit=600 to the options field of the / line only
    awk 'BEGIN{OFS=" "} $1 !~ /^#/ && $2=="/" {
             n=split($4,o,","); found=0
             for(i=1;i<=n;i++) if(o[i]=="commit=600") found=1
             if(!found) $4=$4",commit=600"
         } {print}' "$FSTAB" >"$FSTAB.new"
    mv "$FSTAB.new" "$FSTAB"
    echo "added (takes effect on next boot)"
fi

if [ "$NOUPDATES" = yes ]; then
    say "disabling automatic apt timers (a kid's music box should not self-update)"
    systemctl disable --now apt-daily.timer apt-daily-upgrade.timer 2>/dev/null || true
fi

if [ "$OVERLAY" = yes ]; then
    say "enabling the read-only root overlay"
    echo "WARNING: with the overlay on, 'apt install' and config edits do NOT survive"
    echo "         a reboot until you disable it again (raspi-config nonint do_overlayfs 1)."
    raspi-config nonint do_overlayfs 0 || echo "raspi-config failed; overlay unchanged"
fi

say "done"
echo "Reboot to make the fstab change take effect:  sudo reboot"
echo "Re-run with --check to see the result."
