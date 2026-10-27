#!/bin/sh
# Isolated fake transports: no credentials, network, playback, or shared /tmp cache.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TEST=$(mktemp -d)
trap 'rm -rf "$TEST"' EXIT HUP INT TERM
mkdir -p "$TEST/app/bin" "$TEST/app/scripts" "$TEST/app/data" "$TEST/tmp"
cp "$ROOT"/scripts/*.sh "$TEST/app/scripts/"
# Keep the unmodified upstream RED run isolated too.
sed -e "s|/tmp/|$TEST/tmp/|g" -e "s|/data/opt/soloist|$TEST/app|g" "$ROOT/scripts/search_and_play.sh" > "$TEST/app/scripts/search_and_play.sh"
export TEST
cat > "$TEST/app/spotify_api.conf" <<'EOF'
SPOTIFY_CLIENT_ID=test-client
SPOTIFY_CLIENT_SECRET=test-secret
EOF
cat > "$TEST/app/bin/soloist" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$TEST/controls"
case "$*" in *' now') cat "$TEST/now_state";; esac
EOF
cat > "$TEST/app/bin/curl" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$TEST/calls"
out= headers= url=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -o|--output) out=$2; shift;;
        -D|--dump-header) headers=$2; shift;;
        http*) url=$1;;
    esac
    shift
done
code=200
mode=$(cat "$TEST/mode")
delay() {
    touch "$TEST/started"
    tries=0
    while [ ! -f "$TEST/release" ] && [ -d "$TEST" ] && [ "$tries" -lt 200 ]; do sleep 0.05; tries=$((tries+1)); done
}
case "$url" in
    *api/token*)
        [ ! -f "$TEST/auth-delay" ] || delay
        body='{"access_token":"fake-token","expires_in":3600}'
        if [ "$mode" = auth-limit ]; then code=429; body='{"error":"slow_down"}'; fi;;
    *)
        [ ! -f "$TEST/delay" ] || delay
        case "$mode" in
            transport) exit 28;;
            limit) code=429; body='{"error":{"status":429}}';;
            denied) code=401; body='{"error":{"status":401}}';;
            malformed) body='not JSON';;
            empty) body='{"tracks":{"items":[]}}';;
            censor) body='{"tracks":{"items":[{"album":{},"artists":[],"explicit":true,"name":"Test","track_number":1,"uri":"spotify:track:EXPLICIT"},{"album":{},"artists":[],"explicit":false,"name":"Test","track_number":1,"uri":"spotify:track:CLEAN"}]}}';;
            *) body='{"tracks":{"items":[{"album":{},"artists":[{"name":"Queen"}],"explicit":false,"name":"Don\u0027t Stop Me Now","track_number":1,"uri":"spotify:track:TEST123"}]}}';;
        esac;;
esac
if [ -n "$headers" ]; then printf 'HTTP/1.1 %s\r\nRetry-After: 30\r\n\r\n' "$code" > "$headers"; fi
if [ -n "$out" ]; then printf '%s' "$body" > "$out"; printf '%s' "$code"; else printf '%s' "$body"; fi
EOF
chmod +x "$TEST/app/bin/"*
SEARCH="$TEST/app/scripts/search_and_play.sh"
failures=0
check() { if "$@"; then printf 'PASS %s\n' "$label"; else printf 'FAIL %s\n' "$label"; failures=$((failures+1)); fi; }
reset() { rm -f "$TEST/controls" "$TEST/calls" "$TEST/delay" "$TEST/auth-delay" "$TEST/started" "$TEST/release" "$TEST/app/data/censorship"; rm -rf "$TEST/app/data/requests"; rm -f "$TEST/tmp/spotify_token"*; echo ok > "$TEST/mode"; echo paused > "$TEST/now_state"; }
wait_started() { tries=0; while [ ! -f "$TEST/started" ] && [ "$tries" -lt 100 ]; do sleep 0.05; tries=$((tries+1)); done; }
control() {
    [ -f "$TEST/app/scripts/control.sh" ] || return 0
    sh "$TEST/app/scripts/control.sh" "$@"
    expected=$1
    [ "$expected" != toggle ] || expected=play
    tries=0
    while ! grep -q " $expected" "$TEST/controls" 2>/dev/null && [ "$tries" -lt 100 ]; do sleep 0.05; tries=$((tries+1)); done
}
reset
echo denied > "$TEST/mode"
sh "$SEARCH" 'включи песню Test' || true
label='HTTP 401 must not resume old queue'; check test ! -s "$TEST/controls"
label='HTTP calls have connection and total timeouts'; check grep -q -- '--connect-timeout.*--max-time' "$TEST/calls"
reset
echo limit > "$TEST/mode"
sh "$SEARCH" 'включи песню Test' || true
before=$(wc -l < "$TEST/calls")
sh "$SEARCH" 'включи песню Other' || true
after=$(wc -l < "$TEST/calls")
label='Retry-After is shared across requests'; check test "$before" = "$after"
label='HTTP 429 must not resume old queue'; check test ! -s "$TEST/controls"
reset
echo malformed > "$TEST/mode"
sh "$SEARCH" 'включи песню Test' || true
label='malformed JSON must not resume old queue'; check test ! -s "$TEST/controls"
for mode in transport empty; do
    reset
    echo "$mode" > "$TEST/mode"
    sh "$SEARCH" 'включи песню Test' || true
    label="$mode response leaves playback unchanged"; check test ! -s "$TEST/controls"
done
reset
echo auth-limit > "$TEST/mode"
sh "$SEARCH" 'включи песню Test' || true
sh "$SEARCH" 'включи песню Other' || true
label='token endpoint shares rate-limit cooldown'; check test "$(wc -l < "$TEST/calls")" = 1
reset
echo censor > "$TEST/mode"
touch "$TEST/app/data/censorship"
sh "$SEARCH" 'включи песню Test' || true
label='censorship keeps clean track selection'; check grep -q 'play spotify:track:CLEAN' "$TEST/controls"
label='censorship skips explicit selection'; check sh -c '! grep -q "play spotify:track:EXPLICIT" "$TEST/controls"'
reset
echo playing > "$TEST/now_state"
sh "$SEARCH" 'включи музыку' || true
label='generic resume keeps existing playback'; check grep -q '9090 play$' "$TEST/controls"
label='generic resume does not require an API call'; check test ! -s "$TEST/calls"
reset
sh "$SEARCH" "включи песню Don't Stop Me Now" || true
label='quoted song title still resolves'; check grep -q 'play spotify:track:TEST123' "$TEST/controls"
label='apostrophe survives query encoding'; check grep -qi 'Don%27t' "$TEST/calls"
for action in pause next prev toggle; do
    reset
    touch "$TEST/delay"
    sh "$SEARCH" 'включи песню First' & older=$!
    wait_started
    control "$action"
    touch "$TEST/release"
    wait "$older" || true
    label="$action invalidates an in-flight search"; check sh -c '! grep -q "play spotify:" "$TEST/controls" 2>/dev/null'
done
for action in shuffle repeat; do
    reset
    touch "$TEST/delay"
    sh "$SEARCH" 'включи песню First' & older=$!
    wait_started
    option=on
    [ "$action" != repeat ] || option=context
    control "$action" "$option"
    touch "$TEST/release"
    wait "$older" || true
    label="$action preserves an in-flight search"; check grep -q 'play spotify:track:TEST123' "$TEST/controls"
done
for delayed in delay auth-delay; do
    reset
    touch "$TEST/$delayed"
    sh "$SEARCH" 'включи песню First' & older=$!
    wait_started
    sh "$SEARCH" 'включи песню Second' & newer=$!
    if [ -f "$TEST/app/scripts/control.sh" ]; then
        tries=0
        while [ "$(cat "$TEST/app/data/requests/generation")" != 2 ] && [ "$tries" -lt 100 ]; do sleep 0.05; tries=$((tries+1)); done
    else sleep 0.2; fi
    touch "$TEST/release"
    wait "$older" || true
    wait "$newer" || true
    label="new selection supersedes older $delayed worker"; check test "$(grep -c 'play spotify:' "$TEST/controls")" = 1
    label="concurrent $delayed workers share one token fetch"; check test "$(grep -c 'accounts.spotify.com/api/token' "$TEST/calls")" = 1
done
# Hold the first worker immediately before dispatch, while a newer command is
# admitted. Real flock still performs every lock operation; only scheduling is controlled.
REAL_FLOCK=$(command -v flock)
export REAL_FLOCK
mkdir -p "$TEST/tools"
cat > "$TEST/tools/flock" <<'EOF'
#!/bin/sh
if [ "$1" = -n ] && [ "$2" = 8 ]; then
    tries=0
    while [ ! -s "$TEST/first-controller" ] && [ -d "$TEST" ] && [ "$tries" -lt 100 ]; do
        sleep 0.05; tries=$((tries+1))
    done
    if [ "$PPID" = "$(cat "$TEST/first-controller")" ] && ! mkdir "$TEST/first-registered" 2>/dev/null && mkdir "$TEST/delayed-worker" 2>/dev/null; then
        tries=0
        while [ ! -f "$TEST/release-control" ] && [ -d "$TEST" ] && [ "$tries" -lt 200 ]; do
            sleep 0.05; tries=$((tries+1))
        done
    fi
fi
exec "$REAL_FLOCK" "$@"
EOF
chmod +x "$TEST/tools/flock"
# Inject only the scheduler hook; production keeps its system-first PATH.
cat >> "$TEST/app/scripts/request_state.sh" <<'EOF'
flock() { "$TEST/tools/flock" "$@"; }
EOF
for action in next shuffle; do
    reset
    rm -rf "$TEST/delayed-worker" "$TEST/first-registered"
    rm -f "$TEST/release-control" "$TEST/second-admitted" "$TEST/first-controller"
    if [ -f "$TEST/app/scripts/control.sh" ]; then
        if [ "$action" = next ]; then sh "$TEST/app/scripts/control.sh" next & first=$!
        else sh "$TEST/app/scripts/control.sh" shuffle on & first=$!; fi
        printf '%s\n' "$first" > "$TEST/first-controller"
        tries=0
        while [ ! -d "$TEST/delayed-worker" ] && [ "$tries" -lt 100 ]; do sleep 0.05; tries=$((tries+1)); done
        label="$action ordering fixture reaches contended dispatch"; check test -d "$TEST/delayed-worker"
        (
            if [ "$action" = next ]; then sh "$TEST/app/scripts/control.sh" next;
            else sh "$TEST/app/scripts/control.sh" shuffle off; fi
            touch "$TEST/second-admitted"
        ) & second=$!
        tries=0
        while [ ! -f "$TEST/second-admitted" ] && [ "$tries" -lt 20 ]; do sleep 0.05; tries=$((tries+1)); done
        touch "$TEST/release-control"
        wait "$first" || true
        wait "$second" || true
        tries=0
        while [ "$tries" -lt 100 ]; do
            count=$(grep -c "9090 $action" "$TEST/controls" 2>/dev/null || true)
            [ "${count:-0}" -lt 2 ] || break
            sleep 0.05; tries=$((tries+1))
        done
    fi
    if [ "$action" = next ]; then
        label='two rapid next commands both execute'; check test "${count:-0}" = 2
    else
        label='rapid option commands preserve admission order'; check sh -c 'tail -n 1 "$TEST/controls" | grep -q "shuffle off$"'
    fi
done
exit "$failures"
