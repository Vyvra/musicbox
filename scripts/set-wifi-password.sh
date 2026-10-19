#!/usr/bin/env bash
# Put the WiFi passphrase into a pre-flashed card's NetworkManager keyfile.
#
#   sudo bash scripts/set-wifi-password.sh /dev/sda [SSID]
#
# The passphrase is read with `read -rs`, so it never appears in argv, in `ps`,
# in shell history, or in any transcript. Nothing echoes it back; the script
# only reports the SSID and the passphrase length.
set -euo pipefail

DEV="${1:-}"; SSID="${2:-MyNetwork}"
die() { echo "REFUSING: $*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run as root (sudo bash $0 /dev/sda [SSID])"
[ -b "$DEV" ] || die "$DEV is not a block device"
DEV="$(readlink -f "$DEV")"
ROOTFS="${DEV}2"
[ -b "$ROOTFS" ] || die "expected ${DEV}2 (rootfs)"

# Needs a real terminal, otherwise the prompt cannot disable echo.
if [ ! -t 0 ]; then
    die "run this from an interactive terminal (stdin is not a tty)"
fi

printf 'WiFi passphrase for "%s" (input hidden): ' "$SSID"
IFS= read -rs PSK
echo
[ -n "$PSK" ] || die "empty passphrase"

WORK="$(mktemp -d)"
trap 'umount "$WORK" 2>/dev/null; rm -rf "$WORK"' EXIT
mount "$ROOTFS" "$WORK"
CONNDIR="$WORK/etc/NetworkManager/system-connections"
install -d -m 0755 "$CONNDIR"
FILE="$CONNDIR/preconfigured.nmconnection"

cat >"$FILE" <<EOF
[connection]
id=preconfigured
uuid=$(uuidgen)
type=wifi
autoconnect=true
[wifi]
mode=infrastructure
ssid=$SSID
hidden=false
[wifi-security]
key-mgmt=wpa-psk
psk=$PSK
[ipv4]
method=auto
[ipv6]
addr-gen-mode=default
method=auto
EOF
# NetworkManager ignores keyfiles that are not root:root 0600.
chown root:root "$FILE"
chmod 0600 "$FILE"
PSK=""

grep -q '^psk=.\+' "$FILE" || die "passphrase did not get written"
grep -q "^ssid=$SSID\$" "$FILE" || die "ssid line is wrong"
sync

echo "written: $FILE"
echo "  ssid      : $SSID"
echo "  passphrase: $(grep -c '^psk=.\+' "$FILE") entry, hidden"
echo "  mode      : $(stat -c '%a %U:%G' "$FILE")"
