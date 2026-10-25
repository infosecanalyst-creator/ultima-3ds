# Cross-build environment for Ultima IV / VI on the New 3DS (ScummVM + stereo patch).
#
#   docker build -t ultima-3ds .
#   docker run --rm -v "$PWD:/work" ultima-3ds ./build-3ds.sh
FROM devkitpro/devkitarm:latest

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential git pkg-config python3 ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# ScummVM's 3DS backend dependencies (devkitPro portlibs)
RUN dkp-pacman -Sy --noconfirm 3ds-dev dkp-toolchain-vars \
        3ds-zlib 3ds-libpng 3ds-libjpeg-turbo 3ds-freetype \
        3ds-libmad 3ds-libogg 3ds-libvorbisidec 3ds-flac 3ds-libtheora 3ds-curl && \
    dkp-pacman -Scc --noconfirm

# makerom + bannertool for the .cia (optional; the .3dsx does not need them)
# RUN git clone --depth 1 https://github.com/3DSGuy/Project_CTR /tmp/ctr && \
#     make -C /tmp/ctr/makerom && cp /tmp/ctr/makerom/bin/makerom /usr/local/bin/

ENV DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM CTRULIB=/opt/devkitpro/libctru
WORKDIR /work
