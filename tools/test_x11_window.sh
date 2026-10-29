#!/usr/bin/env bash
# Run inside a dedicated X server (xvfb-run), never on the user's desktop.
set -euo pipefail
if [[ $# -eq 0 || -z ${DISPLAY:-} ]]; then
    echo 'Usage: xvfb-run -a bash tools/test_x11_window.sh COMMAND [ARGS...]' >&2
    exit 2
fi

wm_log=$(mktemp)
openbox --sm-disable >"$wm_log" 2>&1 &
wm_pid=$!
cleanup() {
    status=$?
    if [[ $status -ne 0 ]]; then
        cat "$wm_log" >&2
    fi
    kill "$wm_pid" 2>/dev/null || true
    wait "$wm_pid" 2>/dev/null || true
    rm -f -- "$wm_log"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Fullscreen is an asynchronous EWMH request, so wait for the window manager
# to advertise support before SDL creates the test window.
for ((attempt = 0; attempt < 100; attempt++)); do
    if ! kill -0 "$wm_pid" 2>/dev/null; then
        cat "$wm_log" >&2
        echo 'Openbox exited before the window test' >&2
        exit 1
    fi
    if xprop -root _NET_SUPPORTED | grep -q '_NET_WM_STATE_FULLSCREEN'; then
        echo 'Openbox ready: EWMH fullscreen supported'
        "$@"
        exit 0
    fi
    sleep 0.1
done
cat "$wm_log" >&2
echo 'Timed out waiting for EWMH fullscreen support' >&2
exit 1
