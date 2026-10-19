#!/usr/bin/env bash
# Pre-seed a freshly flashed Raspberry Pi OS card so it boots headless:
# user account, SSH (key + password), hostname, optional WiFi.
#
#   sudo bash scripts/preseed-card.sh /dev/sda --user musicbox --hostname musicbox
#   sudo bash scripts/preseed-card.sh /dev/sda --user musicbox --wifi "MyNet" "pass phrase"
#
# Everything here was verified against the 2026-10-06 Trixie image's own scripts:
#   * /usr/lib/systemd/system/userconfig.service -> /usr/lib/userconf-pi/userconf-service
#     reads <boot>/userconf.txt, "user:hash", then DELETES it
#   * sshswitch.service reads <boot>/ssh and enables ssh
#   * imager_custom set_wlan writes the NetworkManager keyfile format used below
set -euo pipefail

DEV="${1:-}"; shift || true
USER_NAME=""; HOSTNAME_=""; # sudo resets $HOME to /root, so derive the invoking user's home instead.
INVOKER_HOME="$(getent passwd "${SUDO_USER:-root}" | cut -d: -f6)"
KEY_FILE="${INVOKER_HOME:-$HOME}/.ssh/id_ed25519.pub"; WIFI_SSID=""; WIFI_PASS=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --user) USER_NAME="$2"; shift 2 ;;
        --hostname) HOSTNAME_="$2"; shift 2 ;;
        --key) KEY_FILE="$2"; shift 2 ;;
        --wifi) WIFI_SSID="$2"; WIFI_PASS="$3"; shift 3 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

die() { echo "REFUSING: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || die "run as root"
[ -b "$DEV" ] || die "$DEV is not a block device"
DEV="$(readlink -f "$DEV")"
USER_NAME="${USER_NAME:-musicbox}"
HOSTNAME_="${HOSTNAME_:-musicbox}"
[[ "$USER_NAME" =~ ^[a-z][a-z0-9-]{0,31}$ ]] || die "username must be ^[a-z][a-z0-9-]*$"
[ "$USER_NAME" != root ] || die "cannot be root"

BOOT="${DEV}1"; ROOTFS="${DEV}2"
[ -b "$BOOT" ] && [ -b "$ROOTFS" ] || die "expected ${DEV}1 (boot) and ${DEV}2 (rootfs)"

WORK="$(mktemp -d)"; trap 'umount "$WORK/boot" 2>/dev/null; umount "$WORK/root" 2>/dev/null; rm -rf "$WORK"' EXIT
mkdir -p "$WORK/boot" "$WORK/root"
mount "$BOOT" "$WORK/boot"
mount "$ROOTFS" "$WORK/root"
B="$WORK/boot"; R="$WORK/root"

# No pipe that closes early: with 'set -o pipefail', `tr < /dev/urandom | head -c 16`
# makes tr die on SIGPIPE (141) and aborts the whole script before it writes anything.
PASSWORD="$(head -c 100 /dev/urandom | base64 -w0 | tr -dc 'A-Za-z2-9' | cut -c1-16)"
[ -n "$PASSWORD" ] || die "could not generate a password"
HASH="$(openssl passwd -6 "$PASSWORD")"

# --- user -------------------------------------------------------------------
# NOTE: userconf-pi renames the existing uid-1000 user (pi), so its home is
# /home/pi on the card and becomes /home/<user> on first boot.
printf '%s:%s\n' "$USER_NAME" "$HASH" >"$WORK/userconf.txt"
install -m 0644 "$WORK/userconf.txt" "$B/userconf.txt"

# --- ssh --------------------------------------------------------------------
touch "$B/ssh"
EXISTING_HOME="$(awk -F: '$3==1000{print $6}' "$R/etc/passwd")"
EXISTING_HOME="${EXISTING_HOME:-/home/pi}"
KEY_INSTALLED=""
if [ -f "$KEY_FILE" ]; then
    install -d -m 700 -o 1000 -g 1000 "$R$EXISTING_HOME/.ssh"
    install -m 600 -o 1000 -g 1000 "$KEY_FILE" "$R$EXISTING_HOME/.ssh/authorized_keys"
    KEY_INSTALLED="$KEY_FILE"
fi

# --- hostname ---------------------------------------------------------------
printf '%s\n' "$HOSTNAME_" >"$WORK/hostname"
install -m 0644 "$WORK/hostname" "$R/etc/hostname"
if grep -qE '^127\.0\.1\.1' "$R/etc/hosts"; then
    sed -i "s/^127\.0\.1\.1.*/127.0.1.1\t$HOSTNAME_/" "$R/etc/hosts"
else
    printf '127.0.1.1\t%s\n' "$HOSTNAME_" >>"$R/etc/hosts"
fi

# --- wifi (only useful with a USB dongle; the Pi 2B has no radio) -----------
printf '[main]\nWirelessEnabled=true\n' >"$WORK/nmstate"
install -m 0644 "$WORK/nmstate" "$R/var/lib/NetworkManager/NetworkManager.state"
if [ -n "$WIFI_SSID" ]; then
    {
        echo "[connection]"
        echo "id=preconfigured"
        echo "uuid=$(uuidgen)"
        echo "type=wifi"
        echo "autoconnect=true"
        echo "[wifi]"
        echo "mode=infrastructure"
        echo "ssid=$WIFI_SSID"
        echo "hidden=false"
        if [ -n "$WIFI_PASS" ]; then
            echo "[wifi-security]"
            echo "key-mgmt=wpa-psk"
            echo "psk=$WIFI_PASS"
        fi
        echo "[ipv4]"
        echo "method=auto"
        echo "[ipv6]"
        echo "addr-gen-mode=default"
        echo "method=auto"
    } >"$WORK/preconfigured.nmconnection"
    # NetworkManager ignores keyfiles that are not root:root 0600
    install -m 600 -o root -g root "$WORK/preconfigured.nmconnection" \
        "$R/etc/NetworkManager/system-connections/preconfigured.nmconnection"
fi

sync
echo
echo "user      : $USER_NAME"
echo "password  : $PASSWORD"
echo "hostname  : $HOSTNAME_"
echo "ssh       : enabled${KEY_INSTALLED:+ + key from $KEY_INSTALLED}"
echo "wifi      : ${WIFI_SSID:-not configured}"
echo
echo "Save that password — it is not stored anywhere by this script."
