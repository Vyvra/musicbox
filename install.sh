#!/usr/bin/env bash
# Install the kids' music box on the Pi. Run as root from this directory:
#   sudo bash install.sh
# Then reboot.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

[ "$(id -u)" -eq 0 ] || { echo "run as root: sudo bash install.sh" >&2; exit 1; }

MODEL="$(tr -d '\0' </proc/device-tree/model 2>/dev/null || echo unknown)"
echo "== Raspberry Pi model: $MODEL"

# ---------------------------------------------------------------- boot config
BOOT=/boot/firmware/config.txt
[ -f "$BOOT" ] || BOOT=/boot/config.txt
echo "== boot config: $BOOT"
BACKUP="$BOOT.bak-musicbox-$(date +%Y%m%d%H%M%S)"
cp -a "$BOOT" "$BACKUP"
echo "   backup: $BACKUP"

# Onboard analogue/HDMI audio off: the HAT must be the only card.
sed -i -E 's/^[[:space:]]*dtparam=audio=on[[:space:]]*$/dtparam=audio=off/' "$BOOT"
grep -qE '^[[:space:]]*dtparam=audio=off' "$BOOT" \
    || echo 'dtparam=audio=off' >>"$BOOT"

# vc4-kms-v3d also exposes an audio device unless told not to.
if grep -qE '^[[:space:]]*dtoverlay=vc4-kms-v3d[[:space:]]*$' "$BOOT"; then
    sed -i -E 's/^[[:space:]]*dtoverlay=vc4-kms-v3d[[:space:]]*$/dtoverlay=vc4-kms-v3d,noaudio/' "$BOOT"
fi

# HiFiBerry Amp2. Kernel >= 6.1.77 needs the -std variant; plain hifiberry-dacplus
# is for older kernels and silently gives you no card on this image (6.18.50).
if grep -qE '^[[:space:]]*dtoverlay=hifiberry-' "$BOOT"; then
    echo "   a HiFiBerry overlay is already present, leaving it alone:"
    grep -E '^[[:space:]]*dtoverlay=hifiberry-' "$BOOT" | sed 's/^/     /'
else
    cat >>"$BOOT" <<'EOF'

# --- HiFiBerry Amp2 (kids music box) ---
dtoverlay=hifiberry-dacplus-std
# If the card does not appear after reboot, uncomment the next line:
#force_eeprom_read=0
EOF
    echo "   added dtoverlay=hifiberry-dacplus-std"
fi

# ---------------------------------------------------------------- packages
echo "== installing packages (mpv is the big one)"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends mpv alsa-utils exfatprogs ntfs-3g

# ---------------------------------------------------------------- files
echo "== installing musicboxd"
install -m 0755 "$HERE/scripts/musicboxd.py" /usr/local/bin/musicboxd.py
install -d -m 0755 /usr/local/share/musicbox
install -m 0644 "$HERE/README.md" /usr/local/share/musicbox/README.md 2>/dev/null || true
install -d -m 0755 /var/lib/musicbox

cat >/etc/asound.conf <<'EOF'
# Kids music box: the HiFiBerry HAT is the only sound card (onboard audio is off).
# 'hw' rather than 'default' on purpose: no dmix, no resampling, exclusive access.
pcm.!default {
    type hw
    card sndrpihifiberry
}
ctl.!default {
    type hw
    card sndrpihifiberry
}
EOF

# ---------------------------------------------------------------- service
echo "== installing systemd unit"
install -m 0644 "$HERE/systemd/musicboxd.service" /etc/systemd/system/musicboxd.service
systemctl daemon-reload
systemctl enable musicboxd.service

echo
echo "Done. Reboot now:  sudo reboot"
echo "After the reboot, verify with:  bash $(printf '%q' "$HERE")/check.sh"
echo "The music box then starts by itself. Plug a stick in."
