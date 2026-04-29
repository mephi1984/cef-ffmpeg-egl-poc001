#!/usr/bin/env bash
#
# Container entrypoint:
#   1. start a virtual X server (Xvfb) on display :1
#   2. expose it via x11vnc on port 5900
#   3. exec the demo as PID 1, so the container exits when the demo exits
#      (and Xvfb / x11vnc are cleaned up by Docker)
#
set -euo pipefail

DISPLAY_NUM="${DISPLAY_NUM:-1}"
SCREEN_GEOMETRY="${SCREEN_GEOMETRY:-1280x720x24}"
VNC_PORT="${VNC_PORT:-5900}"

export DISPLAY=":${DISPLAY_NUM}"

echo "[entrypoint] starting Xvfb ${DISPLAY} (${SCREEN_GEOMETRY})..."
Xvfb "${DISPLAY}" -screen 0 "${SCREEN_GEOMETRY}" -ac >/tmp/xvfb.log 2>&1 &
XVFB_PID=$!

# Give Xvfb a moment to come up. A handful of short retries beats a fixed
# sleep if the container is under load.
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if [ -e "/tmp/.X11-unix/X${DISPLAY_NUM}" ]; then
        break
    fi
    sleep 0.2
done
if ! kill -0 "${XVFB_PID}" 2>/dev/null; then
    echo "[entrypoint] Xvfb failed to start; log follows:" >&2
    cat /tmp/xvfb.log >&2
    exit 1
fi

echo "[entrypoint] starting x11vnc on port ${VNC_PORT}..."
x11vnc -rfbport "${VNC_PORT}" -display "${DISPLAY}" \
       -nopw -forever -shared -quiet \
       >/tmp/x11vnc.log 2>&1 &

cd /app
echo "[entrypoint] launching cef_egl_demo..."
exec ./cef_egl_demo "$@"
