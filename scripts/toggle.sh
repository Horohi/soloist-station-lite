#!/bin/sh
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD

BASE_DIR="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
[ -d "$BASE_DIR/bin" ] || BASE_DIR="/data/opt/soloist"
SOLOIST_BIN="$BASE_DIR/bin/soloist"

run_soloist() {
    LD_LIBRARY_PATH="$BASE_DIR/lib64" "$SOLOIST_BIN" "$@"
}

if run_soloist ctl -w 127.0.0.1:9090 now 2>&1 | grep -q "paused"; then
    run_soloist ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 || true
    sleep 0.2
    run_soloist ctl -w 127.0.0.1:9090 play
else
    run_soloist ctl -w 127.0.0.1:9090 pause
fi
