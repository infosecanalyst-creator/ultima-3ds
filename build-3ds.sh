#!/usr/bin/env bash
# Build a ScummVM 3DS binary with only the Ultima engine and the stereoscopic
# depth patch applied. Produces out/3ds/scummvm/scummvm.3dsx (+ .cia if makerom
# is on the PATH).
#
#   ./build-3ds.sh          # clone (first run), patch, build
#   ./build-3ds.sh clean
#   SCUMMVM_REF=v2.9.0 ./build-3ds.sh   # pin a release instead of master
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${SCUMMVM_DIR:-$HERE/scummvm}"
REF="${SCUMMVM_REF:-master}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

: "${DEVKITPRO:?DEVKITPRO is not set}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export CTRULIB="${CTRULIB:-$DEVKITPRO/libctru}"
export PORTLIBS="$DEVKITPRO/portlibs/3ds"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PORTLIBS/bin:$PATH"
export PKG_CONFIG_LIBDIR="$PORTLIBS/lib/pkgconfig"

if [ ! -d "$SRC/.git" ]; then
    echo "==> cloning scummvm ($REF) into $SRC"
    git clone --branch "$REF" --depth 1 https://github.com/scummvm/scummvm.git "$SRC"
fi

if [ "${1:-}" = "clean" ]; then
    make -C "$SRC" distclean || true
    exit 0
fi

echo "==> applying the stereo patch"
python3 "$HERE/tools/patch_scummvm.py" "$SRC"

cd "$SRC"
if [ ! -f config.mk ]; then
    echo "==> configure (3DS, Ultima engine only, static)"
    ./configure --host=3ds \
        --disable-all-engines --enable-engine=ultima,ultima4,ultima6 \
        --disable-cloud --disable-libcurl --disable-sdlnet \
        --disable-mt32emu --disable-fluidsynth --disable-freetype2 --disable-detection-full \
        --disable-debug --enable-release-mode --enable-optimizations
fi

# On-3DS testing: the selftest library replaces these input functions only when started
# with --selftest (see backends/platform/3ds/selftest.cpp).
grep -q "selftest wraps" config.mk || echo 'LDFLAGS += -Wl,--wrap=hidScanInput,--wrap=hidKeysHeld,--wrap=hidKeysDown,--wrap=hidKeysDownRepeat,--wrap=hidKeysUp,--wrap=hidTouchRead,--wrap=hidCircleRead,--wrap=irrstCstickRead,--wrap=aptMainLoop # selftest wraps' >> config.mk

echo "==> make -j$JOBS"
make -j"$JOBS" 2>&1 | tee "$HERE/build-3ds.log"
make scummvm.3dsx
if command -v makerom >/dev/null 2>&1; then
    make scummvm.cia || echo "cia build failed (bannertool/makerom?) -- the .3dsx is still fine"
fi

OUT="$HERE/out/3ds/scummvm"
mkdir -p "$OUT"
cp scummvm.3dsx "$OUT/"
[ -f scummvm.cia ] && cp scummvm.cia "$HERE/out/"
echo
echo "==> done: $OUT/scummvm.3dsx"
echo "    copy out/3ds to the SD card root; put the Ultima IV / VI data folders anywhere on the card"
echo "    and add them from the ScummVM launcher. 3D slider up = stereoscopic depth."
