#!/bin/sh
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD

# Register in the bridge's event order; workers never block admission on HTTP.
BASE_DIR="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
[ -d "$BASE_DIR/bin" ] || BASE_DIR="/data/opt/soloist"
SOLOIST_BIN="$BASE_DIR/bin/soloist"
if [ -f "$BASE_DIR/scripts/request_state.sh" ]; then
    . "$BASE_DIR/scripts/request_state.sh"
elif [ -f "$BASE_DIR/bin/request_state.sh" ]; then
    . "$BASE_DIR/bin/request_state.sh"
else
    . /data/opt/soloist/scripts/request_state.sh
fi

case "$1" in
    search) request_begin || exit 1;;
    play|pause|next|prev|toggle) request_begin hold || exit 1;;
    shuffle|repeat) request_lock 8 50 || exit 1; GENERATION=;;
    auto-next)
        GENERATION=$3
        case "$GENERATION" in ''|*[!0-9]*) exit 2;; esac
        request_lock 8 50 || exit 1;;
    clean-cache|clean-cache-full|volume|what-is-playing)
        ;;
    *) exit 2;;
esac

# The bridge advances its FIFO after admission, while this process keeps working.
if [ "${SOLOIST_BRIDGE_ADMISSION:-}" = 1 ]; then
    printf A >&3 || exit 1
    exec 3>&-
fi
CACHE_DIR="${SOLOIST_CACHE_DIR:-/tmp/soloist_cache/cache}"
if [ -n "${YAM_RUN:-}" ] && [ -z "${SOLOIST_CACHE_DIR:-}" ] && [ -d "$YAM_RUN/soloist_cache/cache" ]; then
    CACHE_DIR="$YAM_RUN/soloist_cache/cache"
fi
if [ "$1" = clean-cache ]; then
    if [ -d "$CACHE_DIR" ]; then
        # BusyBox find lacks -amin and -delete; use -mmin +1 -exec rm -f {} +
        find "$CACHE_DIR" -type f -mmin +1 -exec rm -f {} + 2>/dev/null || true
        CACHE_KB=$(du -sk "$CACHE_DIR" 2>/dev/null | awk '{print $1}')
        if [ "${CACHE_KB:-0}" -gt 16384 ]; then
            for f in $(find "$CACHE_DIR" -type f -exec ls -1t {} + 2>/dev/null | tail -n +4); do
                rm -f "$f" 2>/dev/null || true
            done
        fi
        CACHE_KB=$(du -sk "$CACHE_DIR" 2>/dev/null | awk '{print $1}')
        if [ "${CACHE_KB:-0}" -gt 24576 ]; then
            rm -rf "$CACHE_DIR"/* 2>/dev/null || true
        fi
    fi
    exit 0
elif [ "$1" = clean-cache-full ]; then
    if [ -d "$CACHE_DIR" ]; then
        rm -rf "$CACHE_DIR"/* 2>/dev/null || true
    fi
    exit 0
elif [ "$1" = volume ]; then
    vol="${2:-50}"
    amixer set Master "${vol}%" >/dev/null 2>&1 || \
    amixer set 'Master Playback Volume' "${vol}%" >/dev/null 2>&1 || \
    amixer sset Master "${vol}%" >/dev/null 2>&1 || true
    exit 0
elif [ "$1" = what-is-playing ]; then
    "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 now 2>/dev/null || echo "Nothing is playing"
    exit 0
fi
if [ "$1" = search ]; then
    exec 8>&-
    if [ -f "$BASE_DIR/scripts/search_and_play.sh" ]; then
        exec sh "$BASE_DIR/scripts/search_and_play.sh" "$2" "$GENERATION"
    elif [ -f "$BASE_DIR/bin/search_and_play.sh" ]; then
        exec sh "$BASE_DIR/bin/search_and_play.sh" "$2" "$GENERATION"
    else
        exec sh /data/opt/soloist/scripts/search_and_play.sh "$2" "$GENERATION"
    fi
else
    # Keep the admission lock until dispatch so rapid local actions stay ordered.
    request_dispatch "$@"
fi
