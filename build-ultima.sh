#!/usr/bin/env bash
# Build ScummVM (Ultima engine only, stereoscopic patch applied) for the 3DS
# inside devkitPro's MSYS2:   bash /c/devkitPro/3ds-projects/ultima-3ds/build-ultima.sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export CTRULIB=/opt/devkitpro/libctru
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/3ds/bin:$PATH
export PKG_CONFIG_LIBDIR=$DEVKITPRO/portlibs/3ds/lib/pkgconfig
cd "$(dirname "$0")" || exit 1
LOG=$PWD/ultima-build.log; : > "$LOG"
{
  [ -d scummvm ] || { echo "=== unpacking"; tar xzf scummvm-src.tgz; }
  cd scummvm
  if [ ! -f config.mk ]; then
    echo "=== configure (3DS, Ultima only)"
    ./configure --host=3ds \
      --disable-all-engines --enable-engine=ultima,ultima4,ultima6 \
      --disable-cloud --disable-libcurl --disable-sdlnet \
      --disable-mt32emu --disable-fluidsynth --disable-freetype2 --disable-detection-full \
      --disable-debug --enable-release-mode --enable-optimizations
    echo "configure EXIT: $?"
  fi
  echo "=== make"
  make -j"$(nproc)"
  echo "make EXIT: $?"
  make scummvm.3dsx
  echo "3dsx EXIT: $?"
  ls -la scummvm.3dsx
  mkdir -p /c/devkitPro/3ds-projects/SD-CARD/3ds/scummvm && mv -f scummvm.3dsx /c/devkitPro/3ds-projects/SD-CARD/3ds/scummvm/ && echo "moved to SD-CARD/3ds/scummvm/"
} 2>&1 | tee -a "$LOG"
