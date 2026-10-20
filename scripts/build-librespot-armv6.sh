#!/usr/bin/env bash
# Cross-build librespot v0.8.0 for a Raspberry Pi Zero W (ARMv6, hard-float).
#
# Why this exists: no prebuilt binary can be used. librespot v0.8.0 publishes no release
# assets at all, and spotifyd ships armv7/aarch64/x86_64 only - every armv6 asset URL 404s
# (verified). The Zero W is ARMv6, so the binary has to be produced here.
#
# Needs: docker, the librespot source at ~/build/librespot (tag v0.8.0), ~4 GB disk and
# ~10 minutes the first time (the Rust toolchain and the crate cache live in named volumes,
# so re-runs are fast).
#
#   ./build-librespot-armv6.sh                 # build
#   SOURCE=~/src/librespot ./build-librespot-armv6.sh
#
# Output: ~/build/librespot-v0.8.0-armv6 (plus sha256), verified for ABI and glibc.
set -euo pipefail

SOURCE=${SOURCE:-$HOME/build/librespot}
IMAGE=ghcr.io/cross-rs/arm-unknown-linux-gnueabihf:main
OUT=${OUT:-$HOME/build/librespot-v0.8.0-armv6}
CARGO_VOLUME=${CARGO_VOLUME:-librespot-cargo}
RUSTUP_VOLUME=${RUSTUP_VOLUME:-librespot-rustup}

# ---------------------------------------------------------------- inner (in container)
if [ "${1:-}" = "--inside" ]; then
  set -euo pipefail
  SYS=/x-tools/arm-unknown-linux-gnueabihf/arm-unknown-linux-gnueabihf/sysroot
  export PATH=/root/.cargo/bin:$PATH

  # 1. Rust inside the container: the cross image ships only the crosstool toolchain.
  if ! command -v rustup >/dev/null; then
    echo "== installing rust"
    curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.98.0
  fi
  rustup target add arm-unknown-linux-gnueabihf 2>&1 | tail -2

  # 2. armhf libasound into the cross sysroot. apt cannot do this: the image's foreign
  #    armhf index pins candidates to priority -1, and Ubuntu noble renamed the runtime
  #    package to libasound2t64. So fetch the two .debs by exact pool path.
  BASE=http://ports.ubuntu.com/ubuntu-ports/pool/main/a/alsa-lib
  cd /tmp && rm -f ./*.deb
  curl -fsSLO "$BASE/libasound2-dev_1.2.11-1build2_armhf.deb"
  curl -fsSLO "$BASE/libasound2t64_1.2.11-1build2_armhf.deb"
  for d in /tmp/*.deb; do dpkg -x "$d" "$SYS/"; done
  test -f "$SYS/usr/include/alsa/asoundlib.h" || { echo "libasound missing" >&2; exit 1; }

  # 3. Point cargo/cc/pkg-config at that sysroot.
  export CARGO_TARGET_ARM_UNKNOWN_LINUX_GNUEABIHF_LINKER=arm-unknown-linux-gnueabihf-gcc
  export CC_arm_unknown_linux_gnueabihf=arm-unknown-linux-gnueabihf-gcc
  export AR_arm_unknown_linux_gnueabihf=arm-unknown-linux-gnueabihf-ar
  export PKG_CONFIG_ALLOW_CROSS=1
  export PKG_CONFIG_SYSROOT_DIR="$SYS"
  export PKG_CONFIG_PATH="$SYS/usr/lib/arm-linux-gnueabihf/pkgconfig"
  # libasound is a *runtime* dependency only (DT_NEEDED, no code linked out of it), but
  # its own imports reference glibc 2.34/2.38 while this sysroot ships glibc 2.17, so ld
  # would refuse to resolve them. Tolerate undefined symbols coming from shared libraries;
  # our own objects are still checked and do resolve.
  export CARGO_TARGET_ARM_UNKNOWN_LINUX_GNUEABIHF_RUSTFLAGS="-C link-arg=-Wl,--allow-shlib-undefined"

  # 4. rustls instead of OpenSSL (nothing to cross-compile); alsa-backend because the
  #    daemon runs `--backend alsa`; libmdns so the box advertises itself for discovery.
  cd /project
  cargo build --release --target arm-unknown-linux-gnueabihf \
    --no-default-features --features "rustls-tls-webpki-roots,alsa-backend,with-libmdns"
  exit 0
fi

# ----------------------------------------------------------------------- host driver
[ -d "$SOURCE/.git" ] || {
  echo "no librespot source at $SOURCE" >&2
  echo "  git clone -b v0.8.0 --depth 1 https://github.com/librespot-org/librespot.git $SOURCE" >&2
  exit 1
}
mkdir -p "$(dirname "$OUT")"
SELF=$(readlink -f "$0")     # docker -v needs an absolute path

echo "== building in $IMAGE"
docker run --rm \
  -v "$SOURCE":/project \
  -v "$CARGO_VOLUME":/root/.cargo \
  -v "$RUSTUP_VOLUME":/root/.rustup \
  -v "$SELF":/build.sh:ro \
  "$IMAGE" bash /build.sh --inside

BIN="$SOURCE/target/arm-unknown-linux-gnueabihf/release/librespot"
[ -f "$BIN" ] || { echo "build produced no binary" >&2; exit 1; }
install -m 0755 "$BIN" "$OUT"

echo
echo "== artifact: $OUT"
sha256sum "$OUT"
file "$OUT"
readelf -h "$OUT" | grep -E 'Machine|Flags'
readelf -d "$OUT" | grep NEEDED | sed 's/^/  /'
echo -n "max GLIBC symbol: "
objdump -T "$OUT" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
cat <<EOF

Install on the board:
  sudo install -m 0755 $OUT /usr/local/bin/librespot
  librespot --version && ldd /usr/local/bin/librespot | grep 'not found'
EOF
