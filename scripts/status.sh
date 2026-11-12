#!/bin/sh
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD

if [ -n "${YAM_PKG:-}" ] && [ -d "$YAM_PKG/share/horohi.soloist/bin" ]; then
    BASE_DIR="$YAM_PKG/share/horohi.soloist"
else
    BASE_DIR="$(cd "$(dirname "$0")" 2>/dev/null && pwd)"
    [ -d "$BASE_DIR/bin" ] || BASE_DIR="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
    [ -d "$BASE_DIR/bin" ] || BASE_DIR="${YAM_ROOT:-/data/yam}/share/horohi.soloist"
    [ -d "$BASE_DIR/bin" ] || BASE_DIR="/data/opt/soloist"
fi
SOLOIST_BIN="$BASE_DIR/bin/soloist"

run_soloist() {
    _ld="$BASE_DIR/lib64/ld-linux-aarch64.so.1"
    if [ -x "$_ld" ] && ! LD_LIBRARY_PATH="$BASE_DIR/lib64" "$SOLOIST_BIN" --help >/dev/null 2>&1; then
        LD_LIBRARY_PATH="$BASE_DIR/lib64" "$_ld" --library-path "$BASE_DIR/lib64" --argv0 "$SOLOIST_BIN" "$SOLOIST_BIN" "$@"
    else
        LD_LIBRARY_PATH="$BASE_DIR/lib64" "$SOLOIST_BIN" "$@"
    fi
}

echo "=== Spotify Soloist Status ==="
run_soloist ctl -w 127.0.0.1:9090 status 2>/dev/null || echo "Soloist daemon: NOT RUNNING"
echo ""
echo "=== Current Track ==="
run_soloist ctl -w 127.0.0.1:9090 now 2>/dev/null || echo "No track active"
echo ""
echo "=== Processes ==="
ps aux | grep -E 'soloist|soloist-bridge' | grep -v grep
