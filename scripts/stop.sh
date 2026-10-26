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

kill_pidfile /data/opt/soloist/data/bridge.pid soloist-bridge
killall -9 soloist-bridge 2>/dev/null || true

kill_pidfile /data/opt/soloist/data/soloist.pid soloist
killall -9 soloist 2>/dev/null || true

echo "Spotify Soloist and Bridge stopped."
