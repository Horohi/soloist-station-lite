#!/bin/sh
# Isolated fake transports: no credentials, network, playback, or shared /tmp cache.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TEST=$(mktemp -d)
trap 'rm -rf "$TEST"' EXIT HUP INT TERM
mkdir -p "$TEST/app/bin" "$TEST/app/scripts" "$TEST/app/data" "$TEST/tmp"
cp "$ROOT"/scripts/*.sh "$ROOT/scripts/spotify-search.awk" "$TEST/app/scripts/"
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
case "$*" in *' now'|*' now --json')
    [ ! -f "$TEST/now-failed" ] || exit 19
    [ ! -f "$TEST/now-delay" ] || { touch "$TEST/started"; while [ ! -f "$TEST/release" ]; do sleep 0.05; done; }
    cat "$TEST/now_state";; esac
EOF
cat > "$TEST/app/bin/curl" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$TEST/calls"
out= headers= url= query=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -o|--output) out=$2; shift;;
        -D|--dump-header) headers=$2; shift;;
        --data-urlencode) case "$2" in q=*) query=${2#q=};; esac; shift;;
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
            radio) case "$query" in музыку) title=Музыку;; *) title=Test;; esac
                   body='{"tracks":{"items":[{"explicit":false,"name":"'"$title"'","uri":"spotify:track:AAAAAAAAAAAAAAAAAAAAAA"}]}}';;
            radio-censor) body='{"tracks":{"items":[{"album":{},"explicit":true,"name":"Test","track_number":1,"uri":"spotify:track:AAAAAAAAAAAAAAAAAAAAAA"},{"album":{},"explicit":false,"name":"Test","track_number":1,"uri":"spotify:track:BBBBBBBBBBBBBBBBBBBBBB"}]}}';;
            radio-empty) body='{"tracks":{"items":[]},"artists":{"items":[{"uri":"spotify:artist:BBBBBBBBBBBBBBBBBBBBBB"}]}}';;
            censor) body='{"tracks":{"items":[{"album":{},"artists":[],"explicit":true,"name":"Test","track_number":1,"uri":"spotify:track:EXPLICIT"},{"album":{},"artists":[],"explicit":false,"name":"Test","track_number":1,"uri":"spotify:track:CLEAN"}]}}';;
            formatted) body='{ "tracks": { "items": [ { "album": {}, "explicit": true, "name": "Test", "track_number": 1, "uri": "spotify:track:EXPLICIT" }, { "album": {}, "explicit": false, "name": "Test", "track_number": 1, "uri": "spotify:track:CLEAN" } ] } }';;
            explicit-only) body='{ "tracks": { "items": [ { "album": {}, "artists": [{"uri":"spotify:artist:ARTIST"}], "explicit": true, "name": "Test", "track_number": 1, "uri": "spotify:track:EXPLICIT" } ] } }';;
            catalog) body=$(cat "$TEST/catalog.json");;
            *) case "$query" in
                   first|second|test) body='{"tracks":{"items":[{"name":"'"$query"'","explicit":false,"uri":"spotify:track:TEST123"}]}}';;
                   "любимые треки") body='{"tracks":{"items":[{"name":"Любимые треки","explicit":false,"uri":"spotify:track:TEST123"}]}}';;
                   *) body='{"tracks":{"items":[{"album":{},"artists":[{"name":"Queen"}],"explicit":false,"name":"Don\u0027t Stop Me Now","track_number":1,"uri":"spotify:track:TEST123"}]}}';;
               esac;;
        esac;;
esac
if [ -n "$headers" ]; then printf 'HTTP/1.1 %s\r\nRetry-After: 30\r\n\r\n' "$code" > "$headers"; fi
if [ -n "$out" ]; then printf '%s' "$body" > "$out"; printf '%s' "$code"; else printf '%s' "$body"; fi
EOF
chmod +x "$TEST/app/bin/"*
SEARCH="$TEST/app/scripts/search_and_play.sh"
failures=0
check() { if "$@"; then printf 'PASS %s\n' "$label"; else printf 'FAIL %s\n' "$label"; failures=$((failures+1)); fi; }
reset() { rm -f "$TEST/controls" "$TEST/calls" "$TEST/delay" "$TEST/auth-delay" "$TEST/started" "$TEST/release" "$TEST/app/data/censorship" "$TEST/app/data/favorites.uri" "$TEST/now-failed" "$TEST/now-delay"; rm -rf "$TEST/app/data/requests"; rm -f "$TEST/tmp/spotify_token"*; echo ok > "$TEST/mode"; echo paused > "$TEST/now_state"; }
wait_started() { tries=0; while [ ! -f "$TEST/started" ] && [ "$tries" -lt 100 ]; do sleep 0.05; tries=$((tries+1)); done; }
control() {
    [ -f "$TEST/app/scripts/control.sh" ] || return 0
    sh "$TEST/app/scripts/control.sh" "$@"
    expected=$1
    [ "$expected" != toggle ] || expected=play
    [ "$expected" != enqueue ] || expected=add-to-queue
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
echo formatted > "$TEST/mode"
touch "$TEST/app/data/censorship"
sh "$SEARCH" 'включи песню Test' || true
label='formatted JSON selects clean track'; check grep -q 'play spotify:track:CLEAN' "$TEST/controls"
reset
echo explicit-only > "$TEST/mode"
touch "$TEST/app/data/censorship"
sh "$SEARCH" 'включи песню Test' || true
label='censorship does not fall back to unfiltered artist'; check test ! -s "$TEST/controls"
reset
echo playing > "$TEST/now_state"
sh "$SEARCH" 'включи музыку' || true
label='generic resume keeps existing playback'; check grep -q '9090 play$' "$TEST/controls"
label='generic resume does not require an API call'; check test ! -s "$TEST/calls"
reset
echo paused > "$TEST/now_state"
(sleep 0.7; [ ! -d "$TEST" ] || echo playing > "$TEST/now_state") &
_delayed_resume_pid=$!
sh "$SEARCH" 'включи музыку' || true
wait "$_delayed_resume_pid" 2>/dev/null || true
label='generic resume waits for slow >0.5s device activation without falling back to top hits'; check test ! -s "$TEST/calls"
reset
sh "$SEARCH" "включи песню Don't Stop Me Now" || true
label='quoted song title still resolves'; check grep -q 'play spotify:track:TEST123' "$TEST/controls"
label='apostrophe is handed intact to curl for URL encoding'; check grep -qi -- "--data-urlencode q=don't stop me now" "$TEST/calls"
reset
echo catalog > "$TEST/mode"
cat > "$TEST/catalog.json" <<'EOF'
{"tracks":{"items":[
    {"name":"Brain","uri":"spotify:track:WRONG","explicit":false},
    {"uri":"spotify:track:RIGHT","name":"Rain","artists":[],"album":{},"explicit":false}
]}}
EOF
sh "$SEARCH" 'включи трек Rain' || true
label='exact title wins over a substring even with reordered JSON keys'
check grep -q 'play spotify:track:RIGHT$' "$TEST/controls"

reset
echo catalog > "$TEST/mode"
cat > "$TEST/catalog.json" <<'EOF'
{"tracks":{"items":[
    {"name":"Кукушка","uri":"spotify:track:OTHER","artists":[{"name":"Полина Гагарина"}],"explicit":false},
    {"name":"Кукушка","uri":"spotify:track:UNAVAILABLE","artists":[{"name":"Кино"}],"explicit":false,"is_playable":false},
    {"name":"Кукушка","uri":"spotify:track:EXPLICIT","artists":[{"name":"Кино"}],"album":{"explicit":false},"explicit":true},
    {"name":"\u041a\u0443\u043a\u0443\u0448\u043a\u0430","uri":"spotify:track:KINO","explicit":false,"artists":[{"name":"Kino"}]}
]}}
EOF
touch "$TEST/app/data/censorship"
SPOTIFY_MARKET=de sh "$SEARCH" 'включи песню Кукушка группы Кино' || true
label='Cyrillic, escaped names, artist filter, market and explicit flag work together'
check sh -c 'grep -q "play spotify:track:KINO$" "$TEST/controls" &&
    grep -Fq -- "q=track:\"Кукушка\" artist:\"Кино\"" "$TEST/calls" &&
    grep -Fq -- "--data-urlencode market=DE" "$TEST/calls"'

for phrase in 'включи песню Кукушка' 'включи радио по треку Кукушка'; do
    reset
    echo catalog > "$TEST/mode"
    echo '{"tracks":{"items":[{"name":"Unrelated","uri":"spotify:track:AAAAAAAAAAAAAAAAAAAAAA"}]},"artists":{"items":[{"name":"Кукушка","uri":"spotify:artist:OTHER"}]}}' > "$TEST/catalog.json"
    sh "$SEARCH" "$phrase" || true
    label="$phrase refuses unrelated tracks and context fallback"
    check test ! -s "$TEST/controls"
done

reset
echo catalog > "$TEST/mode"
echo '{"tracks":{"items":[{"name":"Rain","uri":"spotify:track:RIGHT"}]}} trailing text' > "$TEST/catalog.json"
sh "$SEARCH" 'включи трек Rain' || true
label='a valid track inside a malformed response is not played'
check test ! -s "$TEST/controls"

reset
echo catalog > "$TEST/mode"
cat > "$TEST/catalog.json" <<'EOF'
{"playlists":{"items":[null,
    {"name":"Вечерний джаз и блюз","uri":"spotify:playlist:CCCCCCCCCCCCCCCCCCCCCC"},
    {"name":"ВЕЧЕРНИЙ ДЖАЗ","uri":"spotify:playlist:AAAAAAAAAAAAAAAAAAAAAA"},
    {"name":"Вечерний джаз","uri":"spotify:playlist:AAAAAAAAAAAAAAAAAAAAAA"},
    {"name":"Вечерний джаз","uri":"spotify:playlist:SHORT"}
]}}
EOF
sh "$SEARCH" 'алиса, включи плейлист «Вечерний джаз»' || true
label='named playlist uses a single exact public match and requests only playlists'
check sh -c 'grep -q "play spotify:playlist:AAAAAAAAAAAAAAAAAAAAAA$" "$TEST/controls" &&
    grep -q -- "--data-urlencode type=playlist --data-urlencode limit=10" "$TEST/calls"'

for mode in ambiguous no-exact; do
    reset
    echo catalog > "$TEST/mode"
    case "$mode" in
        ambiguous) title='Вечерний джаз' ;;
        no-exact) title='Вечерний джаз и блюз' ;;
    esac
    printf '%s\n' '{"playlists":{"items":[{"name":"'"$title"'","uri":"spotify:playlist:AAAAAAAAAAAAAAAAAAAAAA"},{"name":"'"$title"'","uri":"spotify:playlist:BBBBBBBBBBBBBBBBBBBBBB"}]},"tracks":{"items":[{"name":"Вечерний джаз","uri":"spotify:track:OTHER"}]}}' > "$TEST/catalog.json"
    sh "$SEARCH" 'включи плейлист Вечерний джаз' || true
    label="$mode playlist does not change playback or fall back to another type"
    check sh -c '[ ! -s "$TEST/controls" ] && [ "$(grep -c "api.spotify.com/v1/search" "$TEST/calls")" = 1 ]'
done

reset
touch "$TEST/app/data/censorship"
sh "$SEARCH" 'включи плейлист Вечерний джаз' || true
label='censorship blocks named playlist before any catalog request'
check sh -c '[ ! -s "$TEST/controls" ] && [ ! -s "$TEST/calls" ]'

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
# Personal commands must be resolved before ordinary title search.
for phrase in 'любимые треки' 'мои любимые треки' 'понравившиеся треки' 'мне нравится' 'избранное' 'мое избранное' 'моё избранное' 'избранные треки'; do
    reset
    echo spotify:playlist:BBBBBBBBBBBBBBBBBBBBBB > "$TEST/app/data/favorites.uri"
    sh "$SEARCH" "включи $phrase" || true
    label="$phrase plays configured Liked Songs without catalog lookup"
    check sh -c 'grep -q "play spotify:playlist:BBBBBBBBBBBBBBBBBBBBBB$" "$TEST/controls" && [ ! -s "$TEST/calls" ]'
done
reset
printf '%s\n' 'spotify:user:account_123-example.4:collection' > "$TEST/app/data/favorites.uri"
printf '%s\n' '{"context":{"uri":"spotify:user:account_123-example.4:collection"},"item":{"uri":"spotify:track:AAAAAAAAAAAAAAAAAAAAAA"}}' > "$TEST/now_state"
sh "$SEARCH" 'включи понравившиеся треки'
label='Spotify personal collection resumes its loaded queue without sending a URI'
check sh -c 'grep -q " play$" "$TEST/controls" && [ ! -s "$TEST/calls" ]'
for state in '{"context":{"uri":"spotify:user:other:collection"},"item":{"uri":"spotify:track:AAAAAAAAAAAAAAAAAAAAAA"}}' \
             '{"context":{"uri":"spotify:user:account_123-example.4:collection"},"item":{"uri":""}}' \
             '{"context":{"uri":"spotify:user:account_123-example.4:collection"},"item":null}' 'invalid'; do
    reset
    printf '%s\n' 'spotify:user:account_123-example.4:collection' > "$TEST/app/data/favorites.uri"
    printf '%s\n' "$state" > "$TEST/now_state"
    result=0; sh "$SEARCH" 'включи понравившиеся треки' || result=$?
    label='Unloaded Spotify personal collection leaves existing playback unchanged'
    check sh -c '[ "$1" != 0 ] && [ ! -s "$TEST/calls" ] && ! grep -Eq " (activate|play)( |$)" "$TEST/controls"' -- "$result"
done
for value in missing empty spotify:track:AAAAAAAAAAAAAAAAAAAAAA spotify:playlist:short spotify:user::collection spotify:user:user:collection:extra spotify:user:user:top:collection multiline; do
    reset
    case "$value" in
        missing) :;;
        empty) : > "$TEST/app/data/favorites.uri";;
        multiline) printf 'spotify:playlist:BBBBBBBBBBBBBBBBBBBBBB\nextra\n' > "$TEST/app/data/favorites.uri";;
        *) printf '%s\n' "$value" > "$TEST/app/data/favorites.uri";;
    esac
    result=0; sh "$SEARCH" 'включи любимые треки' || result=$?
    label="$value favorites configuration fails without catalog fallback or playback"
    check sh -c '[ "$1" != 0 ] && [ ! -s "$TEST/calls" ] && [ ! -s "$TEST/controls" ]' -- "$result"
done
reset
sh "$SEARCH" 'включи песню любимые треки' || true
label='explicit song title retains ordinary catalog search'; check grep -q 'play spotify:track:TEST123' "$TEST/controls"

for phrase in 'волну' 'радио' 'волну по этому треку' 'волну по текущему треку' 'радио по этому треку' 'радио по текущему треку'; do
    reset
    printf '%s\n' '{"type":"playback_state","status":"paused","item":{"uri":"spotify:track:AAAAAAAAAAAAAAAAAAAAAA"},"context":{"uri":"spotify:playlist:BBBBBBBBBBBBBBBBBBBBBB"}}' > "$TEST/now_state"
    sh "$SEARCH" "включи $phrase" || true
    label="$phrase starts station for actual current item without HTTP"
    check sh -c 'grep -q "play spotify:station:track:AAAAAAAAAAAAAAAAAAAAAA$" "$TEST/controls" && [ ! -s "$TEST/calls" ]'
done
for state in '{"item":null,"context":{"uri":"spotify:track:AAAAAAAAAAAAAAAAAAAAAA"}}' '{"item":{"uri":"spotify:episode:AAAAAAAAAAAAAAAAAAAAAA"}}' '{"item":{"uri":"spotify:track:short"}}' 'not JSON'; do
    reset
    printf '%s\n' "$state" > "$TEST/now_state"
    result=0; sh "$SEARCH" 'включи волну по этому треку' || result=$?
    label='missing/non-track/invalid current item cannot start radio or catalog search'
    check sh -c '[ "$1" != 0 ] && ! grep -q " play" "$TEST/controls" && [ ! -s "$TEST/calls" ]' -- "$result"
done
reset
touch "$TEST/now-failed"
result=0; sh "$SEARCH" 'включи радио' || result=$?
label='current-state failure is nonzero without playback'; check sh -c '[ "$1" != 0 ] && ! grep -q " play" "$TEST/controls" && [ ! -s "$TEST/calls" ]' -- "$result"

for phrase in 'волну по треку Test' 'радио по песне Test' 'волну по Test'; do
    reset
    echo radio > "$TEST/mode"
    sh "$SEARCH" "включи $phrase" || true
    label="$phrase resolves a track and plays its station"
    check sh -c 'grep -q "q=test --data-urlencode type=track" "$TEST/calls" && grep -q "play spotify:station:track:AAAAAAAAAAAAAAAAAAAAAA$" "$TEST/controls"'
done
reset
echo radio-empty > "$TEST/mode"
result=0; sh "$SEARCH" 'включи волну по треку Test' || result=$?
label='named radio never falls back to an artist/album'; check sh -c '[ "$1" != 0 ] && [ ! -s "$TEST/controls" ] && [ "$(wc -l < "$TEST/calls")" = 2 ]' -- "$result"
reset
echo radio-censor > "$TEST/mode"
touch "$TEST/app/data/censorship"
sh "$SEARCH" 'включи радио по треку Test' || true
label='named radio preserves censorship when selecting its seed'; check grep -q 'play spotify:station:track:BBBBBBBBBBBBBBBBBBBBBB$' "$TEST/controls"
for phrase in 'волну по' 'радио по треку'; do
    reset
    result=0; sh "$SEARCH" "включи $phrase" || result=$?
    label='missing radio title fails without search or playback'; check sh -c '[ "$1" != 0 ] && [ ! -s "$TEST/calls" ] && [ ! -s "$TEST/controls" ]' -- "$result"
done
reset
echo radio > "$TEST/mode"
sh "$SEARCH" 'включи радио по музыку' || true
label='radio seed named music does not trigger generic resume'; check grep -q 'play spotify:station:track:AAAAAAAAAAAAAAAAAAAAAA$' "$TEST/controls"

for kind in current named; do
    reset
    if [ "$kind" = current ]; then
        touch "$TEST/now-delay"
        echo '{"item":{"uri":"spotify:track:AAAAAAAAAAAAAAAAAAAAAA"}}' > "$TEST/now_state"
        phrase='включи волну по этому треку'
    else
        echo radio > "$TEST/mode"; touch "$TEST/delay"
        phrase='включи радио по треку Test'
    fi
    sh "$SEARCH" "$phrase" & older=$!
    wait_started
    control pause
    touch "$TEST/release"
    wait "$older" || true
    label="pause supersedes $kind radio before playback"; check sh -c '! grep -q "play spotify:" "$TEST/controls"'
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

# Cross-script, phonetic, digit-collapse, leetspeak and artist-corroboration regression checks.
run_catalog_case() {
    case_label=$1
    phrase=$2
    payload=$3
    expected_uri=$4
    reset
    echo catalog > "$TEST/mode"
    printf '%s\n' "$payload" > "$TEST/catalog.json"
    sh "$SEARCH" "$phrase" || true
    label="$case_label"
    if [ -n "$expected_uri" ]; then
        check grep -q "play ${expected_uri}$" "$TEST/controls"
    else
        check test ! -s "$TEST/controls"
    fi
}

run_catalog_case \
    'norman grayson resolves Cyrillic artist норман грейсон' \
    'включи norman grayson' \
    '{"artists":{"items":[{"name":"норман грейсон","uri":"spotify:artist:NORMANAAAAAAAAAAAAAAAA"},{"name":"Norman Matt","uri":"spotify:artist:OTHERAAAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"монстр","artists":[{"name":"норман грейсон","uri":"spotify:artist:NORMANAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:TRACKAAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:artist:NORMANAAAAAAAAAAAAAAAA'

run_catalog_case \
    'norman grayson этюды picks track этюды by норман грейсон' \
    'включи norman grayson этюды' \
    '{"artists":{"items":[{"name":"норман грейсон","uri":"spotify:artist:NORMANAAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"этюды","artists":[{"name":"норман грейсон","uri":"spotify:artist:NORMANAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:ETUDYAAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:track:ETUDYAAAAAAAAAAAAAAAAA'

run_catalog_case \
    'nine mice resolves artist 9mice instead of 9 Lives tracks' \
    'включи nine mice' \
    '{"artists":{"items":[{"name":"9mice","uri":"spotify:artist:NINEMICEAAAAAAAAAAAAAA"},{"name":"Nine Vicious","uri":"spotify:artist:OTHERAAAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"9 Lives","artists":[{"name":"Fury","uri":"spotify:artist:FURYAAAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:LIVESAAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:artist:NINEMICEAAAAAAAAAAAAAA'

run_catalog_case \
    'найн майс resolves phonetic artist 9mice' \
    'включи найн майс' \
    '{"artists":{"items":[{"name":"9mice","uri":"spotify:artist:NINEMICEAAAAAAAAAAAAAA"},{"name":"ARTIK & ASTI","uri":"spotify:artist:ARTIKAAAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"Пьяный май","artists":[{"name":"VEIGEL","uri":"spotify:artist:VEIGELAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:MAYAAAAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:artist:NINEMICEAAAAAAAAAAAAAA'

run_catalog_case \
    'spaced digits кретин 5 2 8 matches artist КРЕТИН 528' \
    'включи кретин 5 2 8' \
    '{"artists":{"items":[{"name":"КРЕТИН 528","uri":"spotify:artist:KRETINAAAAAAAAAAAAAAAA"},{"name":"5sta Family","uri":"spotify:artist:FIVESTAAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"ВИЗИТ К УРОЛОГУ","artists":[{"name":"КРЕТИН 528","uri":"spotify:artist:KRETINAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:VISITAAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:artist:KRETINAAAAAAAAAAAAAAAA'

run_catalog_case \
    'spaced digits fallen 7 7 7 angel picks fallen777angel over Fallen Angel track' \
    'включи fallen 7 7 7 angel' \
    '{"artists":{"items":[{"name":"fallen777angel","uri":"spotify:artist:FALLENAAAAAAAAAAAAAAAA"},{"name":"fallen angel","uri":"spotify:artist:FALLENTRAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"гладиатор/рыцарь","artists":[{"name":"fallen777angel","uri":"spotify:artist:FALLENAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:GLADAAAAAAAAAAAAAAAAAA"},{"name":"Fallen Angel - Slowed","artists":[{"name":"xlout","uri":"spotify:artist:XLOUTAAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:SLOWEDAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:artist:FALLENAAAAAAAAAAAAAAAA'

run_catalog_case \
    'мэдкид prefers corroborated Russian madk1d over Japanese MADKID' \
    'включи мэдкид' \
    '{"artists":{"items":[{"name":"MADKID","uri":"spotify:artist:JPMADKIDAAAAAAAAAAAAAA"},{"name":"madk1d","uri":"spotify:artist:RUMADKIDAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"ДИНАСТИЯ","artists":[{"name":"VILLIAN","uri":"spotify:artist:VILLIANAAAAAAAAAAAAAAA"},{"name":"madk1d","uri":"spotify:artist:RUMADKIDAAAAAAAAAAAAAA"}],"uri":"spotify:track:DYNAAAAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:artist:RUMADKIDAAAAAAAAAAAAAA'

run_catalog_case \
    'пятёрка prefers corroborated artist 5opka over random track ПЯТЁРКА' \
    'включи пятёрка' \
    '{"artists":{"items":[{"name":"5opka","uri":"spotify:artist:FIVOPKAAAAAAAAAAAAAAAA"},{"name":"Пятёрка-мясокомбинат","uri":"spotify:artist:MEATAAAAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"ПЯТЁРКА","artists":[{"name":"Figurant","uri":"spotify:artist:FIGURANTAAAAAAAAAAAAAA"}],"uri":"spotify:track:FIGTRACKAAAAAAAAAAAAAA"},{"name":"Жми Share","artists":[{"name":"5opka","uri":"spotify:artist:FIVOPKAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:SHAREAAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:artist:FIVOPKAAAAAAAAAAAAAAAA'

for pair in \
    'виллиан|VILLIAN' \
    'токсис|Toxi$' \
    'фараон|PHARAOH' \
    'херонвотер|Heronwater' \
    'бушидо жо|BUSHIDO ZHO' \
    'сода лав|SODA LUV' \
    'лил кристалл|LIL KRYSTALLL' \
    'биг бейби тейп|Big Baby Tape' \
    'кай энджел|Kai Angel' \
    'моргенштерн|MORGENSHTERN' \
    'слава марлоу|SLAVA MARLOW' \
    'френдли таг|FRIENDLY THUG 52 NGG'
do
    ru_q=${pair%|*}
    lat_a=${pair#*|}
    run_catalog_case \
        "Cyrillic phonetic artist $ru_q matches $lat_a" \
        "включи $ru_q" \
        '{"artists":{"items":[{"name":"френдли таго","uri":"spotify:artist:CLONEAAAAAAAAAAAAAAAAA"},{"name":"'"$lat_a"'","uri":"spotify:artist:TARGETAAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"хит","artists":[{"name":"'"$lat_a"'","uri":"spotify:artist:TARGETAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:HITAAAAAAAAAAAAAAAAAAA"}]}}' \
        'spotify:artist:TARGETAAAAAAAAAAAAAAAA'
done

run_catalog_case \
    'русский канья matches track Русский Канье' \
    'включи русский канья' \
    '{"artists":{"items":[{"name":"Канги","uri":"spotify:artist:KANGIAAAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"Русский Канье","artists":[{"name":"5opka","uri":"spotify:artist:FIVOPKAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:KANYEAAAAAAAAAAAAAAAAA"},{"name":"Русская красавица","artists":[{"name":"HANNA","uri":"spotify:artist:HANNAAAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:KRASAVAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:track:KANYEAAAAAAAAAAAAAAAAA'

run_catalog_case \
    'one percent badtrip girl matches 1% Battery Girl in top track slot' \
    'включи one percent badtrip girl' \
    '{"artists":{"items":[{"name":"Sān-Z","uri":"spotify:artist:SANZAAAAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"1% Battery Girl","artists":[{"name":"Sān-Z","uri":"spotify:artist:SANZAAAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:BATTERYAAAAAAAAAAAAAAA"},{"name":"one percent","artists":[{"name":"And One","uri":"spotify:artist:ANDONEAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:ONEPCTAAAAAAAAAAAAAAAA"}]}}' \
    'spotify:track:BATTERYAAAAAAAAAAAAAAA'

run_catalog_case \
    'unmatched sun z leaves playback unchanged instead of playing random artist' \
    'включи sun z' \
    '{"artists":{"items":[{"name":"Sub Zero Project","uri":"spotify:artist:SUBZEROAAAAAAAAAAAAAAA"}]},"tracks":{"items":[{"name":"Sun to Me","artists":[{"name":"mgk","uri":"spotify:artist:MGKAAAAAAAAAAAAAAAAAAA"}],"uri":"spotify:track:SUNTOAAAAAAAAAAAAAAAAA"}]}}' \
    ''

# LED FX signaling: error on no-match, play on match, silent on superseded search
reset
echo empty > "$TEST/mode"
sh "$SEARCH" 'включи несуществующий трек 999' || true
label='catalog no-match signals err to led_fx'; check sh -c '[ "$(cat "$TEST/app/data/requests/led_fx" 2>/dev/null || true)" = "err" ]'

reset
sh "$SEARCH" 'включи песню Test' || true
label='catalog match signals play to led_fx'; check sh -c '[ "$(cat "$TEST/app/data/requests/led_fx" 2>/dev/null || true)" = "play" ]'

reset
echo empty > "$TEST/mode"
touch "$TEST/delay"
sh "$SEARCH" 'включи несуществующий трек 999' & older=$!
wait_started
control pause
rm -f "$TEST/app/data/requests/led_fx"
touch "$TEST/release"
wait "$older" || true
label='superseded failing search does not flash led_fx err'; check sh -c '[ ! -e "$TEST/app/data/requests/led_fx" ]'

# Voice queue, restart (seek 0), clean deactivate, and hard-error TTS signaling
reset
control enqueue 'песню test'
label='enqueue searches with --queue and dispatches add-to-queue without activate'
check sh -c 'grep -q "add-to-queue spotify:track:TEST123" "$TEST/controls" && ! grep -q " activate" "$TEST/controls" && [ "$(cat "$TEST/app/data/requests/led_fx" 2>/dev/null || true)" = "play" ]'

reset
control seek 0
label='seek 0 dispatches seek 0 cleanly'; check grep -q " seek 0" "$TEST/controls"
label='non-numeric seek is rejected with exit 2'; check sh -c 'rc=0; sh "$TEST/app/scripts/control.sh" seek bad || rc=$?; [ "$rc" -eq 2 ]'

reset
control deactivate
label='deactivate pauses and deactivates Soloist cleanly'
check sh -c 'grep -q " pause" "$TEST/controls" && grep -q " deactivate" "$TEST/controls"'

reset
mv "$TEST/app/spotify_api.conf" "$TEST/app/spotify_api.conf.bak"
sh "$SEARCH" 'включи песню Test' || true
mv "$TEST/app/spotify_api.conf.bak" "$TEST/app/spotify_api.conf"
label='missing API config writes cfg to requests/tts'
check sh -c '[ "$(cat "$TEST/app/data/requests/tts" 2>/dev/null || true)" = "cfg" ]'

reset
echo limit > "$TEST/mode"
sh "$SEARCH" 'включи песню Test' || true
label='HTTP 429 writes limit to requests/tts'
check sh -c '[ "$(cat "$TEST/app/data/requests/tts" 2>/dev/null || true)" = "limit" ]'

reset
echo transport > "$TEST/mode"
sh "$SEARCH" 'включи песню Test' || true
label='HTTP transport failure writes net to requests/tts'
check sh -c '[ "$(cat "$TEST/app/data/requests/tts" 2>/dev/null || true)" = "net" ]'

reset
echo empty > "$TEST/mode"
sh "$SEARCH" 'включи несуществующий трек 999' || true
label='ordinary catalog no-match never triggers requests/tts'
check sh -c '[ ! -e "$TEST/app/data/requests/tts" ]'

exit "$failures"
