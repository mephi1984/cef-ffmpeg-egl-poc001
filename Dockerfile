# syntax=docker/dockerfile:1.6
#
# Two-stage build:
#   1. `builder`  — full Ubuntu 24.04 with C++ toolchain, downloads CEF and
#                   compiles cef_egl_demo against it.
#   2. `runtime`  — minimal Ubuntu 24.04 with just the shared libraries CEF
#                   and our binary need at run time, plus Xvfb + x11vnc.
#
# Build:   docker build -t cef-egl-demo .
# Run:     docker run --rm -p 8080:8080 cef-egl-demo
# View:    point a VNC client at localhost:8080 (no password).

# ---------------------------------------------------------------------------
# Stage 1: builder
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        # toolchain
        ca-certificates wget bzip2 \
        cmake build-essential pkg-config \
        # X11 / EGL / GLES / GL — what our binary itself links against
        libx11-dev libxext-dev libxrandr-dev libxi-dev \
        libxcursor-dev libxinerama-dev libxxf86vm-dev \
        libegl1-mesa-dev libgles2-mesa-dev libgl-dev \
        # FFmpeg
        libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
        # libcef.so has DT_NEEDED entries for the full Chromium runtime;
        # the linker resolves them at link time (CEF passes
        # -Wl,--fatal-warnings, so missing libs become hard errors).
        libnss3 libnspr4 libgtk-3-0 libgbm1 libxss1 libasound2t64 \
        libxshmfence1 libdrm2 libatk1.0-0 libatk-bridge2.0-0 libatspi2.0-0 \
        libcups2 libxcomposite1 libxdamage1 libxfixes3 libxkbcommon0 \
        libpango-1.0-0 libpangocairo-1.0-0 libdbus-1-3 \
    && rm -rf /var/lib/apt/lists/*

# CEF binary distribution. Override at build time with --build-arg if a newer
# release is desired, e.g.
#   docker build --build-arg CEF_VERSION='148.0.x+...' -t cef-egl-demo .
ARG CEF_VERSION=147.0.10+gd58e84d+chromium-147.0.7727.118
ARG CEF_VARIANT=minimal
ARG CEF_DIST=cef_binary_${CEF_VERSION}_linux64_${CEF_VARIANT}

WORKDIR /src

# Download + extract CEF in its own layer so source edits don't re-download
# the ~300 MB tarball.
RUN mkdir -p third_party \
 && wget -q "https://cef-builds.spotifycdn.com/${CEF_DIST}.tar.bz2" -O /tmp/cef.tar.bz2 \
 && tar -xjf /tmp/cef.tar.bz2 -C third_party \
 && mv third_party/${CEF_DIST} third_party/cef \
 && rm /tmp/cef.tar.bz2

# Application sources. Listed individually so unrelated changes (README,
# Dockerfile comments) don't bust the cache for the build step.
COPY CMakeLists.txt /src/
COPY src            /src/src
COPY html           /src/html
COPY testvideo001.mp4 /src/testvideo001.mp4

RUN cmake -S /src -B /src/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /src/build -j"$(nproc)"

# ---------------------------------------------------------------------------
# Stage 2: runtime
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive \
    DISPLAY=:1

RUN apt-get update && apt-get install -y --no-install-recommends \
        # X server + VNC
        xvfb x11vnc \
        # GL / X libs the binary links against
        libegl1 libgles2 libgl1 libx11-6 \
        # Mesa software renderer (for llvmpipe under Xvfb)
        libgl1-mesa-dri libglx-mesa0 \
        # FFmpeg shared libraries
        libavcodec60 libavformat60 libavutil58 libswscale7 \
        # CEF runtime requirements (Chromium pulls all of these)
        libnss3 libnspr4 libgtk-3-0 libgbm1 libxss1 libasound2t64 \
        libxshmfence1 libdrm2 libatk1.0-0 libatk-bridge2.0-0 libatspi2.0-0 \
        libcups2 libxcomposite1 libxdamage1 libxfixes3 libxkbcommon0 \
        libpango-1.0-0 libpangocairo-1.0-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Pull the staged build directory wholesale — it already has the binary,
# libcef.so, all *.pak/*.bin/*.so/locales/, html/ and testvideo001.mp4 next
# to each other, which is exactly the layout the demo expects.
COPY --from=builder /src/build/cef_egl_demo            /app/
COPY --from=builder /src/build/libcef.so               /app/
COPY --from=builder /src/build/libEGL.so               /app/
COPY --from=builder /src/build/libGLESv2.so            /app/
COPY --from=builder /src/build/libvk_swiftshader.so    /app/
COPY --from=builder /src/build/libvulkan.so.1          /app/
COPY --from=builder /src/build/v8_context_snapshot.bin /app/
COPY --from=builder /src/build/vk_swiftshader_icd.json /app/
COPY --from=builder /src/build/chrome-sandbox          /app/
COPY --from=builder /src/build/chrome_100_percent.pak  /app/
COPY --from=builder /src/build/chrome_200_percent.pak  /app/
COPY --from=builder /src/build/resources.pak           /app/
COPY --from=builder /src/build/icudtl.dat              /app/
COPY --from=builder /src/build/locales                 /app/locales
COPY --from=builder /src/build/html                    /app/html
COPY --from=builder /src/build/testvideo001.mp4        /app/

COPY scripts/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

# x11vnc listens here. The user's existing setup uses 8080, so match that.
EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
