# TL;DR

**On Windows (PowerShell) — one command, browser opens automatically:**

```powershell
.\run.ps1
```

This builds the image, starts the container, and opens the noVNC viewer in
your default browser at `http://localhost:6080`. No VNC client needed.

**Manual fallback:**

```bash
docker build -t cef-egl-demo .
docker run --rm -p 5900:5900 -p 6080:6080 cef-egl-demo
# Then open http://localhost:6080/vnc.html?autoconnect=true in a browser
# or connect a VNC client to localhost:5900
```

# CEF + EGL + OpenGL OSR demo

A small C++/CMake application that opens an EGL/X11 window, runs CEF
(Chromium Embedded Framework) in off-screen rendering mode, decodes a
video file with FFmpeg, and composites both into a single OpenGL ES 2
texture pipeline:

```
+----------------------+
|   CEF-rendered HTML  |   <- upper half (CEF OnPaint -> GL_TEXTURE_2D)
+----------------------+
|       FFmpeg video   |   <- lower half (libavcodec -> swscale -> GL_TEXTURE_2D)
+----------------------+      cover-cropped, looped
```

The video stream is `testvideo001.mp4` placed alongside the binary; it
loops automatically and is centre-cropped (the "cover" fit familiar from
CSS `background-size: cover`) so the source aspect ratio is preserved
regardless of the window shape.

## Repo layout

```
CMakeLists.txt           Top-level build
Dockerfile               Two-stage Docker build (builder + runtime)
scripts/entrypoint.sh    Container entrypoint: Xvfb + x11vnc + demo
src/                     C++ sources
  main.cpp                 - entry point + render loop
  browser_app.{h,cpp}      - CefApp + command-line tweaks
  browser_client.{h,cpp}   - CefClient + lifespan/load handlers
  render_handler.{h,cpp}   - CefRenderHandler: receives OnPaint, stages BGRA
  video_decoder.{h,cpp}    - FFmpeg-based video decode + sws_scale to BGRA
  gl_window.{h,cpp}        - X11 window + EGL display/surface/context
  gl_renderer.{h,cpp}      - shaders + generic textured-quad draw
html/index.html          Page CEF loads at startup
testvideo001.mp4         Looping video shown on the lower half
third_party/cef/         CEF binary distribution (downloaded separately)
```

## Quick start (Docker)

This is the easiest path: the image builds CEF and the demo, and
ships with Xvfb + x11vnc preconfigured. Building takes a few minutes
on first run (CEF tarball is ~300 MB and the wrapper takes a while to
compile).

```bash
docker build -t cef-egl-demo .
docker run --rm -p 8080:8080 --name cef-egl-demo cef-egl-demo
```

Then point a VNC viewer at `localhost:8080` (no password). Closing the
demo window from VNC, or `docker stop cef-egl-demo`, terminates the
container.

Build-time options:

```bash
# Pin a different CEF release:
docker build \
  --build-arg CEF_VERSION='148.0.x+gxxxxxx+chromium-148.0.x.x' \
  -t cef-egl-demo .

# Use the full distribution instead of "minimal" (debug symbols + tests):
docker build --build-arg CEF_VARIANT=standard -t cef-egl-demo .
```

Runtime options (override via `-e`):

| Variable          | Default      | Meaning                              |
| ----------------- | ------------ | ------------------------------------ |
| `DISPLAY_NUM`     | `1`          | Xvfb display number (becomes `:N`)   |
| `SCREEN_GEOMETRY` | `1280x720x24`| Xvfb screen geometry / depth         |
| `VNC_PORT`        | `5900`       | x11vnc raw VNC port                  |
| `NOVNC_PORT`      | `6080`       | noVNC HTTP port (browser viewer)     |

## Native WSL2 on Windows 11 (no Docker, no VNC)

Windows 11 ships **WSLg** — a built-in Wayland/X11 compositor that
automatically forwards Linux GUI windows to the Windows desktop. If you
build natively inside a WSL2 Ubuntu shell (not inside Docker), the demo
window appears directly in Windows without any VNC setup:

```bash
# inside WSL2 — WSLg sets DISPLAY and WAYLAND_DISPLAY automatically
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cd build && ./cef_egl_demo   # window pops up on the Windows desktop
```

Requirements: Windows 11 with WSL2 and the WSLg-enabled kernel
(`wsl --update`). Windows 10 does not include WSLg.

## Native build (Ubuntu 24.04)

```bash
# 1. install build + runtime dependencies
apt-get update
apt-get install -y --no-install-recommends \
    cmake build-essential pkg-config wget bzip2 ca-certificates \
    libx11-dev libegl1-mesa-dev libgles2-mesa-dev libgl-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libnss3 libnspr4 libgtk-3-0 libgbm1 libxss1 libasound2t64 \
    libxshmfence1 libdrm2 libatk1.0-0 libatk-bridge2.0-0 libatspi2.0-0 \
    libcups2 libxcomposite1 libxdamage1 libxfixes3 libxkbcommon0 \
    libpango-1.0-0 libpangocairo-1.0-0 \
    xvfb x11vnc

# 2. download CEF
mkdir -p third_party && cd third_party
CEF=cef_binary_147.0.10+gd58e84d+chromium-147.0.7727.118_linux64_minimal
wget "https://cef-builds.spotifycdn.com/${CEF}.tar.bz2" -O cef.tar.bz2
tar xjf cef.tar.bz2 && mv "${CEF}" cef && rm cef.tar.bz2
cd ..

# 3. build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 4. run under Xvfb + VNC
Xvfb :1 -screen 0 1280x720x24 &
export DISPLAY=:1
x11vnc -rfbport 8080 -display :1 -nopw -forever -bg
cd build && ./cef_egl_demo
```

Build output ends up in `build/`:

```
build/
  cef_egl_demo            our binary
  libcef.so               CEF runtime (and friends)
  *.pak, *.dat, locales/  CEF resources
  html/index.html         page CEF loads
  testvideo001.mp4        video the lower half plays
```

## Implementation notes

- CEF on Linux requires a multi-process model. The same binary serves as
  the browser, renderer, GPU, utility, and zygote process: `main()`
  dispatches `CefExecuteProcess` first; if it returns ≥ 0 we're a child
  process and just exit with that code.
- CEF is pumped on the render thread via `CefDoMessageLoopWork`, so
  `OnPaint` lands on the same thread that owns the GL context. We can
  upload the browser texture directly without a cross-thread handoff.
- Both the CEF `OnPaint` buffer and the FFmpeg `sws_scale` output are
  BGRA8. We upload them to `GL_RGBA` textures and the fragment shader
  swizzles `c.bgr` → RGB on the way out, which keeps the path portable
  on GLES2 implementations that don't expose `GL_BGRA_EXT`.
- The video path runs entirely on CPU: libavcodec decodes frames, libsws
  converts to BGRA at the source resolution, the main loop picks the
  newest frame whose PTS has elapsed, and `glTexSubImage2D` updates the
  texture in place. Loop offset is bumped by the stream duration on
  every wrap, so PTS comparisons stay monotonic across loops.
- The container has no DBus / UPower / etc. CEF logs about
  "Failed to connect to the bus" are harmless in this environment.
