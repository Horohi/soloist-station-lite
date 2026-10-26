#!/bin/sh
BASE_DIR="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
[ -d "$BASE_DIR/bin" ] || BASE_DIR="/data/opt/soloist"
SOLOIST_BIN="$BASE_DIR/bin/soloist"

if "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 now 2>&1 | grep -q "paused"; then
    "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 || true
    sleep 0.2
    exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play
else
    exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 pause
fi
