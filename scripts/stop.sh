#!/bin/sh
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD

# Kill the PID from a pid-file only if it still belongs to the expected process
# (pid-files live in /data and survive reboots, so the PID may have been reused).
kill_pidfile() {
    pidfile="$1"; expected="$2"
    [ -f "$pidfile" ] || return 0
    pid=$(cat "$pidfile" 2>/dev/null || true)
    if [ -n "$pid" ] && [ "$(cat "/proc/$pid/comm" 2>/dev/null)" = "$expected" ]; then
        kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$pidfile"
}

YAM_DATA="${YAM_VAR:-${YAM_ROOT:-/data/yam}/var}/horohi.soloist"
YAM_CACHE="${YAM_RUN:-/tmp/yam}/soloist_cache"

for pidfile in \
    "$YAM_DATA/ram_guard.pid" \
    "$YAM_CACHE/ram_guard.pid" \
    /data/opt/soloist/data/ram_guard.pid; do
    if [ -f "$pidfile" ]; then
        rpid=$(cat "$pidfile" 2>/dev/null || true)
        [ -n "$rpid" ] && kill -9 "$rpid" 2>/dev/null || true
        rm -f "$pidfile"
    fi
done

for pidfile in "$YAM_DATA/bridge.pid" "$YAM_CACHE/bridge.pid" /data/opt/soloist/data/bridge.pid; do
    kill_pidfile "$pidfile" soloist-bridge
done
killall -9 soloist-bridge 2>/dev/null || true

for pidfile in "$YAM_DATA/soloist.pid" "$YAM_CACHE/soloist.pid" /data/opt/soloist/data/soloist.pid; do
    kill_pidfile "$pidfile" soloist
done
killall -9 soloist 2>/dev/null || true

rm -f /tmp/soloist.yield 2>/dev/null || true
echo "Spotify Soloist and Bridge stopped."
