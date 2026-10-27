# Shared by the controller and search worker; BASE_DIR must name this installation.
umask 077
STATE_DIR="$BASE_DIR/data/requests"
mkdir -p "$STATE_DIR" || exit 1
exec 8>"$STATE_DIR/control.lock"
SELECTION_PENDING=0

request_cleanup() {
    if [ -n "${CLI_PID:-}" ]; then
        kill -TERM "$CLI_PID" 2>/dev/null || true
        sleep 0.2
        kill -KILL "$CLI_PID" 2>/dev/null || true
        wait "$CLI_PID" 2>/dev/null || true
        CLI_PID=
    fi
    [ "$SELECTION_PENDING" = 0 ] || rm -f "$STATE_DIR/selection.pending"
    rm -f "$STATE_DIR/cli.$$" "$STATE_DIR/now.$$" "$STATE_DIR/selection.$$" "$STATE_DIR/body.$$" "$STATE_DIR/headers.$$"
}

request_traps() {
    trap 'request_cleanup' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
}
request_traps

request_report() {
    printf '[control] action=%s stage=%s status=%s\n' "$1" "$2" "$3" >> "$STATE_DIR/control.log"
}

# The CLI cannot retain either lock after its worker exits.
request_cli() {
    (trap - EXIT; exec 8>&- 9>&-; LD_LIBRARY_PATH="$BASE_DIR/lib64" exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 "$@") > "$STATE_DIR/cli.$$" 2>/dev/null &
    CLI_PID=$!
    CLI_TICKS_LEFT=${CLI_TICKS_LEFT:-30}
    cli_timed_out=0
    while kill -0 "$CLI_PID" 2>/dev/null; do
        if [ "$CLI_TICKS_LEFT" -le 0 ]; then
            cli_timed_out=1
            kill -TERM "$CLI_PID" 2>/dev/null || true
            sleep 0.2
            kill -KILL "$CLI_PID" 2>/dev/null || true
            break
        fi
        sleep 0.1
        CLI_TICKS_LEFT=$((CLI_TICKS_LEFT - 1))
    done
    wait "$CLI_PID" 2>/dev/null
    cli_status=$?
    CLI_PID=
    [ "$cli_timed_out" = 0 ] || cli_status=124
    [ "$cli_status" != 0 ] || cat "$STATE_DIR/cli.$$"
    rm -f "$STATE_DIR/cli.$$"
    return "$cli_status"
}

request_current() {
    current_generation=$(cat "$STATE_DIR/generation" 2>/dev/null)
    [ "${current_generation:-0}" = "$GENERATION" ]
}

request_lock() {
    attempts=0
    while ! flock -n "$1"; do
        attempts=$((attempts + 1))
        [ "$attempts" -lt "$2" ] || return 1
        [ "$1" != 9 ] || request_current || return 1
        sleep 0.1
    done
}

request_begin() {
    request_lock 8 50 || return 1
    GENERATION=$(cat "$STATE_DIR/generation" 2>/dev/null)
    case "$GENERATION" in ''|*[!0-9]*) GENERATION=0;; esac
    GENERATION=$((GENERATION + 1))
    printf '%s\n' "$GENERATION" > "$STATE_DIR/generation" || { flock -u 8; return 1; }
    [ "${1:-}" = hold ] || flock -u 8
}

request_dispatch() {
    request_traps
    action=$1
    request_lock 8 50 || { request_report "$action" admission 1; return 1; }
    CLI_TICKS_LEFT=27 # One dispatch budget, reserving ALSA delay and TERM grace below the admission limit.
    if [ "$1" != shuffle ] && [ "$1" != repeat ] && ! request_current; then
        flock -u 8
        return 1
    fi
    if [ "$1" = auto-next ]; then
        observation=$(cat "$STATE_DIR/track" 2>/dev/null)
        observed_uri=$(printf '%s\n' "$observation" | head -n 1)
        if [ -z "$2" ] || [ "$observed_uri" != "$2" ] || [ -f "$STATE_DIR/selection.pending" ]; then
            flock -u 8
            return 0
        fi
        if [ -f "$STATE_DIR/selection" ] && [ "$observation" = "$(cat "$STATE_DIR/selection" 2>/dev/null)" ]; then
            flock -u 8
            return 0
        fi
        set -- next
    fi
    if [ "$1" = toggle ]; then
        request_cli now > "$STATE_DIR/now.$$"
        result=$?
        if [ "$result" != 0 ]; then
            rm -f "$STATE_DIR/now.$$"
            request_report "$action" now "$result"
            flock -u 8
            return "$result"
        fi
        if grep -q paused "$STATE_DIR/now.$$"; then
            set -- play
        else
            set -- pause
        fi
        rm -f "$STATE_DIR/now.$$"
    fi
    if [ "$1" = play ]; then
        request_cli activate >/dev/null
        result=$?
        if [ "$result" != 0 ]; then
            request_report "$action" activate "$result"
            flock -u 8
            return "$result"
        fi
        sleep 0.3 # Allow Alice to release ALSA dmix, as in the bridge's previous play path.
    fi
    if [ "$1" = play ] && [ -n "${2:-}" ]; then
        selection_before=$(cat "$STATE_DIR/track" 2>/dev/null)
        if ! printf '%s\n' "$selection_before" > "$STATE_DIR/selection.$$" ||
           ! mv -f "$STATE_DIR/selection.$$" "$STATE_DIR/selection.pending"; then
            request_report "$action" selection 1
            flock -u 8
            return 1
        fi
        SELECTION_PENDING=1
    fi
    request_cli "$@" >/dev/null
    result=$?
    if [ "$SELECTION_PENDING" = 1 ]; then
        if [ "$result" = 0 ]; then
            if ! mv -f "$STATE_DIR/selection.pending" "$STATE_DIR/selection"; then
                # Retain pending on commit failure: automatic recovery must fail closed.
                request_report "$action" selection 1
                result=1
            fi
        else
            rm -f "$STATE_DIR/selection.pending"
        fi
        SELECTION_PENDING=0
    fi
    [ "$result" = 0 ] || request_report "$action" dispatch "$result"
    flock -u 8
    return "$result"
}
