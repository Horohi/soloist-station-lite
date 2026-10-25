#!/bin/sh
BASE_DIR="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
[ -d "$BASE_DIR/bin" ] || BASE_DIR="/data/opt/soloist"
SOLOIST_BIN="$BASE_DIR/bin/soloist"

echo "=== Spotify Soloist Status ==="
"$SOLOIST_BIN" ctl -w 127.0.0.1:9090 status 2>/dev/null || echo "Soloist daemon: NOT RUNNING"
echo ""
echo "=== Current Track ==="
"$SOLOIST_BIN" ctl -w 127.0.0.1:9090 now 2>/dev/null || echo "No track active"
echo ""
echo "=== Processes ==="
ps aux | grep -E 'soloist|soloist-bridge' | grep -v grep
