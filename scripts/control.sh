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
    clean-cache|clean-cache-full)
        ;;
    *) exit 2;;
esac

# The bridge advances its FIFO after admission, while this process keeps working.
if [ "${SOLOIST_BRIDGE_ADMISSION:-}" = 1 ]; then
    printf A >&3 || exit 1
    exec 3>&-
fi
if [ "$1" = clean-cache ]; then
    if [ -d /tmp/soloist_cache/cache ]; then
        find /tmp/soloist_cache/cache -type f -amin +1 -delete 2>/dev/null || true
        find /tmp/soloist_cache/cache -type f -mmin +1 -delete 2>/dev/null || true
    fi
    exit 0
elif [ "$1" = clean-cache-full ]; then
    if [ -d /tmp/soloist_cache/cache ]; then
        rm -rf /tmp/soloist_cache/cache/* 2>/dev/null || true
    fi
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
