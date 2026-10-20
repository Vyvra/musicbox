#!/usr/bin/env bash
# Configure a Raspberry Pi Zero W as a standalone music box.
#
# The 3B+ box needs none of this: its HiFiBerry Amp2 announces itself over EEPROM and
# has a hardware mixer, whereas a MAX98357A has no EEPROM (so needs an explicit I2S
# overlay) and no mixer at all (so needs ALSA softvol for the volume).
#
# Run ON the Zero W, as root, AFTER install.sh. It refuses to run elsewhere, because
# every change here is wrong for a board with a real DAC.
set -euo pipefail

CONFIG=/etc/musicbox/config
BOOTCFG=/boot/firmware/config.txt
ASOUND=/etc/asound.conf
STAMP=$(date +%Y%m%d-%H%M%S)

die() { echo "refusing: $*" >&2; exit 1; }
say() { echo "== $*"; }

[ "$(id -u)" -eq 0 ] || die "run as root (sudo $0)"

# --- guard -------------------------------------------------------------------
if ! grep -qi "zero" /proc/device-tree/model 2>/dev/null; then
  die "$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo 'unknown board')" \
      "is not a Pi Zero. This script retunes the audio path for an I2S amplifier with" \
      "no mixer; on a board with a real DAC it would break the volume."
fi
say "board: $(tr -d '\0' < /proc/device-tree/model)"

# --- 1. overlays -------------------------------------------------------------
say "boot config: $BOOTCFG"
cp -a "$BOOTCFG" "$BOOTCFG.bak-zero-$STAMP"
# hifiberry-dac = "plain I2S DAC, no control interface", which is what a MAX98357A is.
grep -q "^dtoverlay=hifiberry-dac$" "$BOOTCFG" || cat >> "$BOOTCFG" <<'EOF'

# --- standalone music box (Pi Zero W + MAX98357A I2S amplifier) ---
# No EEPROM on this amp, so the overlay has to be explicit. The card it creates is
# still called sndrpihifiberry, which is why VOLUME_CARD needs no change.
dtoverlay=hifiberry-dac
# The Zero's single micro-USB port is a device port by default; the USB stick needs
# it as a host. Power comes from the PWR port, not this one.
dtoverlay=dwc2,dr_mode=host
EOF
say "overlays ensured (backup: $BOOTCFG.bak-zero-$STAMP)"

# --- 2. softvol --------------------------------------------------------------
# The MAX98357A has no mixer. Its gain is strapped in hardware, so the only usable
# level control is an ALSA softvol plugin, declared here as PCM "musicbox".
say "writing $ASOUND"
[ -f "$ASOUND" ] && cp -a "$ASOUND" "$ASOUND.bak-zero-$STAMP"
cat > "$ASOUND" <<'EOF'
# Standalone music box: the I2S amplifier has no mixer, so volume lives here.
# Both players must use this PCM; pointing them at hw: would bypass the control and
# leave playback at full scale.
pcm.musicbox {
    type softvol
    slave.pcm "hw:sndrpihifiberry"
    control { name "Musicbox"; card "sndrpihifiberry" }
    min_dB -51.0
    max_dB 0.0
    resolution 256
}
EOF

# --- 3. daemon config --------------------------------------------------------
say "updating $CONFIG"
[ -f "$CONFIG" ] || die "$CONFIG not found - run install.sh first"
cp -a "$CONFIG" "$CONFIG.bak-zero-$STAMP"
set_kv() {  # key value
  local key=$1 value=$2
  if grep -q "^${key}=" "$CONFIG"; then
    sed -i "s|^${key}=.*|${key}=${value}|" "$CONFIG"
  else
    printf '%s=%s\n' "$key" "$value" >> "$CONFIG"
  fi
}
set_kv VOLUME_CARD sndrpihifiberry
set_kv VOLUME_CONTROL Musicbox        # the softvol control created above
set_kv VOLUME_INIT_PCM musicbox       # opened once so ALSA creates the control
set_kv AUDIO_DEVICE alsa/musicbox     # mpv: go through softvol, not hw:
set_kv SPOTIFY_ALSA_DEVICE musicbox   # librespot --device
set_kv SPOTIFY_BITRATE 96             # ARMv6: ~1/3 the decode work of 320
set_kv LIBRESPOT_ALWAYS_ON 0          # 512 MB: start on demand
say "config updated (backup: $CONFIG.bak-zero-$STAMP)"

cat <<'EOF'

Done. Next:
  1. reboot (the overlays only apply at boot):  sudo reboot
  2. check the card exists:                      aplay -l | grep sndrpihifiberry
  3. create + verify the softvol control:        /usr/local/bin/musicboxctl volume -24
     then read it back:                          amixer -c sndrpihifiberry sget Musicbox
  4. plug a stick in and confirm it plays, then confirm a *fresh boot* does not start
     at full volume (that is the softvol trap: an unopened PCM defaults to 0 dB).
  5. optional, big win on ARMv6: purge cloud-init (see docs/pi-zero-w-bringup.md).
EOF
