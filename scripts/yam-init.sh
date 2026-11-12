#!/bin/sh
# Service lifecycle manager for YAM v2 (BusyBox init, soloist CLI, and yam service spec).

if [ -n "${YAM_PKG:-}" ] && [ -d "$YAM_PKG/share/horohi.soloist/bin" ]; then
    PKG_SHARE="$YAM_PKG/share/horohi.soloist"
else
    PKG_SHARE="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
fi
[ -d "$PKG_SHARE/bin" ] || PKG_SHARE="${YAM_ROOT:-/data/yam}/share/horohi.soloist"

if [ -z "${YAM_ROOT:-}" ]; then
    case "$PKG_SHARE" in
        */pkgs/horohi/soloist/*/share/horohi.soloist) YAM_ROOT="${PKG_SHARE%/pkgs/horohi/soloist/*/share/horohi.soloist}" ;;
        */share/horohi.soloist) YAM_ROOT="${PKG_SHARE%/share/horohi.soloist}" ;;
        *) YAM_ROOT="/data/yam" ;;
    esac
fi

YAM_ETC="${YAM_ETC:-$YAM_ROOT/etc}"
YAM_VAR="${YAM_VAR:-$YAM_ROOT/var}"
if [ "$YAM_ROOT" != "/data/yam" ]; then
    [ "$YAM_ETC" != "/data/yam/etc" ] || YAM_ETC="$YAM_ROOT/etc"
    [ "$YAM_VAR" != "/data/yam/var" ] || YAM_VAR="$YAM_ROOT/var"
fi
YAM_RUN="${YAM_RUN:-/tmp/yam}"

DATA_ROOT="$YAM_VAR/horohi.soloist"
CONF_DIR="$YAM_ETC/horohi.soloist"
CACHE_DIR="$YAM_RUN/soloist_cache"

pid_alive() {
    pidfile="$1"
    expected="$2"
    [ -f "$pidfile" ] || return 1
    pid=$(cat "$pidfile" 2>/dev/null || true)
    [ -n "$pid" ] || return 1
    kill -0 "$pid" 2>/dev/null || return 1
    comm=$(cat "/proc/$pid/comm" 2>/dev/null || true)
    [ "$comm" = "$expected" ] && return 0
    if [ "$expected" = "soloist" ] && [ "$comm" = "ld-linux-aarch6" ]; then
        case "$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)" in
            *soloist*) return 0 ;;
        esac
    fi
    return 1
}

run_detached() {
    if command -v setsid >/dev/null 2>&1; then
        setsid "$@" </dev/null
    else
        "$@" </dev/null
    fi
}

stop_daemons() {
    YAM_ROOT="$YAM_ROOT" YAM_VAR="$YAM_VAR" YAM_ETC="$YAM_ETC" YAM_RUN="$YAM_RUN" \
        sh "$PKG_SHARE/scripts/stop.sh" >/dev/null 2>&1 || true
}

start_daemons() {
    mkdir -p "$CONF_DIR" "$DATA_ROOT/requests" "$DATA_ROOT/cache" "$CACHE_DIR/cache" "$CACHE_DIR/Storage"
    chmod 700 "$DATA_ROOT/requests" 2>/dev/null || true
    rm -f /tmp/soloist.yield

    if pid_alive "$DATA_ROOT/soloist.pid" soloist && pid_alive "$DATA_ROOT/bridge.pid" soloist-bridge; then
        echo "[yam-init] Soloist and Bridge are already running."
        return 0
    fi

    stop_daemons

    run_detached env \
        YAM_ROOT="$YAM_ROOT" \
        YAM_PKG="${YAM_PKG:-}" \
        YAM_ETC="$YAM_ETC" \
        YAM_VAR="$YAM_VAR" \
        YAM_RUN="$YAM_RUN" \
        YAM_PLATFORM="${YAM_PLATFORM:-}" \
        sh "$PKG_SHARE/scripts/soloist-service.sh" >/tmp/soloist.log 2>&1 &
    echo $! > "$DATA_ROOT/soloist.pid"

    sleep 1

    if [ -x "$PKG_SHARE/bin/soloist-bridge" ]; then
        run_detached env \
            YAM_ROOT="$YAM_ROOT" \
            YAM_PKG="${YAM_PKG:-}" \
            YAM_ETC="$YAM_ETC" \
            YAM_VAR="$YAM_VAR" \
            YAM_RUN="$YAM_RUN" \
            "$PKG_SHARE/bin/soloist-bridge" >/tmp/soloist-bridge.log 2>&1 &
        echo $! > "$DATA_ROOT/bridge.pid"
    fi

    # Keep tmpfs cache bounded against BusyBox memory pressure
    run_detached sh -c '
        sleep 30
        while true; do
            _sp=$(cat "$1/soloist.pid" 2>/dev/null || true)
            [ -n "$_sp" ] && kill -0 "$_sp" 2>/dev/null || break
            sleep 30
            MEM_AVAIL=$(awk "/MemAvailable/{print int(\$2/1024)}" /proc/meminfo 2>/dev/null || echo 100)
            if [ "${MEM_AVAIL:-100}" -lt 55 ]; then
                YAM_ROOT="$2" YAM_VAR="$3" YAM_ETC="$4" YAM_RUN="$5" \
                    SOLOIST_CACHE_DIR="$6/cache" \
                    sh "$7/scripts/control.sh" clean-cache-full >/dev/null 2>&1 || true
                sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true
            elif [ "${MEM_AVAIL:-100}" -lt 80 ]; then
                YAM_ROOT="$2" YAM_VAR="$3" YAM_ETC="$4" YAM_RUN="$5" \
                    SOLOIST_CACHE_DIR="$6/cache" \
                    sh "$7/scripts/control.sh" clean-cache >/dev/null 2>&1 || true
            fi
        done
    ' sh "$DATA_ROOT" "$YAM_ROOT" "$YAM_VAR" "$YAM_ETC" "$YAM_RUN" "$CACHE_DIR" "$PKG_SHARE" >/dev/null 2>&1 &
    echo $! > "$DATA_ROOT/ram_guard.pid"

    run_detached sh -c '
        _bin="$1/bin/soloist"
        _lib="$1/lib64"
        _ld="$_lib/ld-linux-aarch64.so.1"
        for i in 1 2 3 4 5; do
            sleep 2
            if [ -x "$_ld" ] && ! LD_LIBRARY_PATH="$_lib" "$_bin" --help >/dev/null 2>&1; then
                LD_LIBRARY_PATH="$_lib" "$_ld" --library-path "$_lib" --argv0 "$_bin" "$_bin" ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 && break
            else
                LD_LIBRARY_PATH="$_lib" "$_bin" ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 && break
            fi
        done
    ' sh "$PKG_SHARE" >/dev/null 2>&1 &
}

run_foreground() {
    trap 'stop_daemons; exit 0' HUP INT TERM
    start_daemons
    while pid_alive "$DATA_ROOT/soloist.pid" soloist && pid_alive "$DATA_ROOT/bridge.pid" soloist-bridge; do
        sleep 5
    done
    stop_daemons
}

case "${1:-start}" in
    start)
        start_daemons
        ;;
    run)
        run_foreground
        ;;
    stop)
        stop_daemons
        ;;
    restart)
        stop_daemons
        sleep 1
        start_daemons
        ;;
    status)
        YAM_ROOT="$YAM_ROOT" YAM_VAR="$YAM_VAR" YAM_ETC="$YAM_ETC" YAM_RUN="$YAM_RUN" \
            exec sh "$PKG_SHARE/scripts/status.sh"
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status|run}" >&2
        exit 2
        ;;
esac
