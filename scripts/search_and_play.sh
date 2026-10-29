#!/bin/sh
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD

QUERY="$1"
if [ -z "$QUERY" ]; then
    exit 0
fi

LOG="/tmp/soloist-search.log"
printf "[search] ===== Incoming query: '%s' =====\n" "$QUERY" >> "$LOG"

# Base directory detection
BASE_DIR="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
[ -d "$BASE_DIR/bin" ] || BASE_DIR="/data/opt/soloist"

SOLOIST_BIN="$BASE_DIR/bin/soloist"
CURL_BIN="$BASE_DIR/bin/curl"
[ -x "$CURL_BIN" ] || CURL_BIN="curl"
API_CONF="$BASE_DIR/spotify_api.conf"
[ -f "$API_CONF" ] || API_CONF="/data/opt/soloist/spotify_api.conf"
CENSOR_FILE="$BASE_DIR/data/censorship"
[ -f "$CENSOR_FILE" ] || CENSOR_FILE="/data/opt/soloist/data/censorship"

if [ -f "$BASE_DIR/scripts/request_state.sh" ]; then
    . "$BASE_DIR/scripts/request_state.sh"
elif [ -f "$BASE_DIR/bin/request_state.sh" ]; then
    . "$BASE_DIR/bin/request_state.sh"
else
    . /data/opt/soloist/scripts/request_state.sh
fi
GENERATION="$2"
[ -n "$GENERATION" ] || request_begin || exit 1
request_current || exit 0

# Normalize whitespace (replaces `xargs`, which chokes on quotes: "Don't Stop Me Now")
squeeze() {
    sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' -e 's/[[:space:]][[:space:]]*/ /g'
}

# Clean Alice greetings and filler words. Only LEADING fillers (and a trailing "пожалуйста")
# are removed, so titles like "Давай поговорим" or the band "Алиса" survive.
CLEAN=$(printf '%s\n' "$QUERY" | tr '[:upper:]' '[:lower:]' | squeeze | \
    sed -E -e 's/^((алиса,?|давай|пожалуйста)[[:space:]]+)+//' -e 's/[[:space:]]+пожалуйста$//')

# Strip leading action verb
TERM=$(printf '%s\n' "$CLEAN" | sed -E 's/^(включи|поставь|сыграй|запусти|найди|слушать|вруби)[[:space:]]+//' | squeeze)

personal_error() {
    printf '[search] %s; playback unchanged\n' "$1" >> "$LOG"
    exit 1
}

valid_id() {
    case "$1" in ''|*[!A-Za-z0-9]*) return 1;; esac
    [ "${#1}" -eq 22 ]
}

RADIO=0
case "$TERM" in
    'любимые треки'|'мои любимые треки'|'понравившиеся треки'|'мне нравится'|'избранное'|'мое избранное'|'моё избранное'|'избранные треки')
        FAVORITES="$BASE_DIR/data/favorites.uri"
        [ -f "$FAVORITES" ] || personal_error 'Liked Songs not configured (data/favorites.uri)'
        [ "$(wc -c < "$FAVORITES")" -le 128 ] || personal_error 'Invalid Liked Songs URI'
        URI=$(cat "$FAVORITES") || personal_error 'Could not read Liked Songs URI'
        case "$URI" in spotify:playlist:*) valid_id "${URI#spotify:playlist:}" || personal_error 'Invalid Liked Songs URI';;
            *) personal_error 'Invalid Liked Songs URI';; esac
        request_dispatch play "$URI"
        exit $?;;
    'волну'|'радио'|'волну по этому треку'|'волну по текущему треку'|'радио по этому треку'|'радио по текущему треку')
        NOW_STATE=$(request_cli now --json) || { request_report radio now "$?"; exit 1; }
        # shortcut: Soloist emits item.uri first; recheck this shape when upgrading its CLI.
        URI=$(printf '%s\n' "$NOW_STATE" | sed -n 's/.*"item"[[:space:]]*:[[:space:]]*{[[:space:]]*"uri"[[:space:]]*:[[:space:]]*"\(spotify:track:[A-Za-z0-9]*\)".*/\1/p')
        valid_id "${URI#spotify:track:}" || personal_error 'No current track for radio'
        request_dispatch play "spotify:station:track:${URI#spotify:track:}"
        exit $?;;
    'волну по'|'радио по') personal_error 'Missing radio seed track';;
    'волну по '*|'радио по '*)
        RADIO=1
        TERM=$(printf '%s\n' "$TERM" | sed -E 's/^(волну|радио) по (треку |песне |песни )?//')
        case "$TERM" in ''|треку|песне|песни) personal_error 'Missing radio seed track';; esac;;
esac

# Detect search type hint from the FIRST word only ("песню Группа крови" is a track, not an artist)
TYPE="track"
case "$TERM" in
    "альбом "*)
        TYPE="album"; TERM="${TERM#* }" ;;
    "группу "*|"группа "*|"исполнителя "*|"исполнитель "*|"артиста "*|"артист "*|"певца "*|"певицу "*)
        TYPE="artist"; TERM="${TERM#* }" ;;
    "песню "*|"песня "*|"трек "*)
        TYPE="track"; TERM="${TERM#* }" ;;
esac
[ "$RADIO" = 0 ] || TYPE=track

echo "[search] Cleaned search term: '$TERM' (preferred type: $TYPE)" >> "$LOG"

# If search term is empty or generic 'музыку' -> resume or play default
if [ "$RADIO" = 0 ] && { [ -z "$TERM" ] || [ "$TERM" = "музыку" ] || [ "$TERM" = "что-нибудь" ]; }; then
    echo "[search] Generic play request, resuming..." >> "$LOG"
    request_dispatch play || exit 1
    sleep 0.5
    request_current || exit 0
    NOW_STATE=$(request_cli now) || { request_report play now "$?"; exit 1; }
    if echo "$NOW_STATE" | grep -qi "playing" && ! echo "$NOW_STATE" | grep -qi "nothing"; then
        exit 0
    fi
    # If nothing was playing, fall through and search popular hits
    TERM="top hits"
    TYPE="track"
    echo "[search] Nothing was active, searching '$TERM'..." >> "$LOG"
fi

# Spotify API Configuration
if [ ! -f "$API_CONF" ]; then
    echo "[search] Missing API configuration; playback unchanged" >> "$LOG"
    exit 1
fi

. "$API_CONF"
if [ -z "$SPOTIFY_CLIENT_ID" ] || [ -z "$SPOTIFY_CLIENT_SECRET" ]; then
    echo "[search] Spotify API keys not set in $API_CONF" >> "$LOG"
    exit 1
fi

CURL_OPTS="-sS --connect-timeout 5 --max-time 15"
if grep -q "5.175.161.40" /etc/hosts 2>/dev/null; then
    CURL_OPTS="--resolve api.spotify.com:443:5.175.161.40 --resolve accounts.spotify.com:443:5.175.161.40 $CURL_OPTS"
fi

# A separate API lock serializes token refresh and cooldown without blocking pause.
exec 9>"$STATE_DIR/api.lock"
request_lock 9 600 || exit 1
request_current || exit 0

api_request() {
    request_current || return 1
    NOW=$(date +%s) || return 1
    UNTIL=$(cat "$STATE_DIR/cooldown" 2>/dev/null)
    case "$UNTIL" in ''|*[!0-9]*) UNTIL=0;; esac
    if [ "$NOW" -lt "$UNTIL" ]; then
        echo "[search] Spotify cooldown active; request discarded" >> "$LOG"
        return 1
    fi
    HTTP_STATUS=$("$CURL_BIN" $CURL_OPTS -D "$STATE_DIR/headers.$$" -o "$STATE_DIR/body.$$" -w '%{http_code}' "$@") || {
        echo "[search] HTTP transport failed; playback unchanged" >> "$LOG"
        return 1
    }
    if [ "$HTTP_STATUS" = 429 ]; then
        NOW=$(date +%s) || return 1
        RETRY=$(awk 'tolower($1)=="retry-after:" {gsub(/\r/,"",$2); value=$2}
            END {if(value ~ /^[0-9]+$/ && length(value)<=9) printf "%.0f",value+0; else print 60}' "$STATE_DIR/headers.$$")
        printf '%s\n' "$((NOW + RETRY))" > "$STATE_DIR/cooldown"
    fi
    case "$HTTP_STATUS" in
        2??)
            if grep -qE '"error"[[:space:]]*:' "$STATE_DIR/body.$$"; then
                echo "[search] API error body; playback unchanged" >> "$LOG"
                return 1
            fi
            cat "$STATE_DIR/body.$$";;
        *)
            [ "$HTTP_STATUS" != 401 ] || rm -f "$STATE_DIR/token" "$STATE_DIR/token_exp"
            echo "[search] HTTP $HTTP_STATUS; playback unchanged" >> "$LOG"
            return 1;;
    esac
}

# The cache is read only after taking the API lock; racing workers share one refresh.
NOW=$(date +%s) || exit 1
EXP=$(cat "$STATE_DIR/token_exp" 2>/dev/null)
case "$EXP" in ''|*[!0-9]*) EXP=0;; esac
TOKEN=""
[ "$NOW" -ge "$EXP" ] || TOKEN=$(cat "$STATE_DIR/token" 2>/dev/null)
if [ -z "$TOKEN" ]; then
    AUTH_RESP=$(api_request -X POST "https://accounts.spotify.com/api/token" \
        -H "Content-Type: application/x-www-form-urlencoded" \
        --data-urlencode "grant_type=client_credentials" \
        --data-urlencode "client_id=$SPOTIFY_CLIENT_ID" \
        --data-urlencode "client_secret=$SPOTIFY_CLIENT_SECRET") || exit 1
    TOKEN=$(printf '%s' "$AUTH_RESP" | sed -n 's/.*"access_token"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')
    case "$TOKEN" in ''|*[!A-Za-z0-9._~+/=-]*)
        echo "[search] Invalid token response; playback unchanged" >> "$LOG"; exit 1;;
    esac
    EXPIRES=$(printf '%s' "$AUTH_RESP" | grep -oE '"expires_in"[[:space:]]*:[[:space:]]*[0-9]+' | grep -oE '[0-9]+$' |
        awk '{value=$0+0} END {if(value>0 && value<=86400) printf "%.0f",value; else print 3600}')
    [ "$EXPIRES" -gt 60 ] && EXPIRES=$((EXPIRES - 60))
    printf '%s\n' "$TOKEN" > "$STATE_DIR/token"
    printf '%s\n' "$((NOW + EXPIRES))" > "$STATE_DIR/token_exp"
fi
request_current || exit 0

# URL encode function
ENCODED=$(echo "$TERM" | awk 'BEGIN {
    for (i = 0; i < 256; i++) {
        c = sprintf("%c", i);
        if (c ~ /[a-zA-Z0-9._~-]/) table[c] = c;
        else table[c] = sprintf("%%%02X", i);
    }
}
{
    out = "";
    len = length($0);
    for (i = 1; i <= len; i++) {
        c = substr($0, i, 1);
        out = out table[c];
    }
    print out;
}')

echo "[search] Querying Spotify API: term='$TERM', type='$TYPE'..." >> "$LOG"

CENSORSHIP=0
if [ -f "$CENSOR_FILE" ]; then
    CENSORSHIP=1
    echo "[search] Censorship filter ACTIVE (filtering explicit tracks)" >> "$LOG"
fi

LIMIT=5

SEARCH_RESP=$(api_request "https://api.spotify.com/v1/search?q=$ENCODED&type=$TYPE&limit=$LIMIT" \
    -H "Authorization: Bearer $TOKEN") || exit 1
request_current || exit 0

# Rank track results by how many query words hit the title (x5) or the artists (x2).
# The term is passed via the environment: awk -v would interpret backslash escapes.
pick_track() {
    SEARCH_TERM="$TERM" awk -v censor="$CENSORSHIP" '
    BEGIN {
        RS = "\\{\"album\":"
        best_score = -1
        best_uri = ""
        nwords = split(tolower(ENVIRON["SEARCH_TERM"]), qwords, " ")
    }
    NR > 1 {
        if (censor == "1" && $0 ~ /"explicit":true/) next
        uri = ""
        if (match($0, /"uri":"spotify:track:[a-zA-Z0-9]+/)) {
            uri = substr($0, RSTART + 7, RLENGTH - 7)
        }
        if (uri == "") next

        name = ""
        if (match($0, /"name":"[^"]+","track_number"/)) {
            name = tolower(substr($0, RSTART + 8, RLENGTH - 24))
        }

        artists = ""
        if (match($0, /"artists":/)) {
            artists = tolower(substr($0, RSTART))
        }

        score = 0
        for (i = 1; i <= nwords; i++) {
            w = qwords[i]
            if (length(w) == 0) continue
            if (index(name, w) > 0) score += 5
            if (index(artists, w) > 0) score += 2
        }

        if (score > best_score) {
            best_score = score
            best_uri = uri
        } else if (best_uri == "") {
            best_uri = uri
        }
    }
    END {
        if (best_uri != "") print best_uri
    }'
}

if [ "$TYPE" = "track" ]; then
    URI=$(printf '%s' "$SEARCH_RESP" | pick_track)
else
    URI=$(echo "$SEARCH_RESP" | grep -oE "spotify:$TYPE:[a-zA-Z0-9]+" | head -n 1)
fi

# Fallback: if preferred type had no matches, search any playable type
if [ -z "$URI" ] && [ "$RADIO" = 0 ]; then
    echo "[search] No match for type=$TYPE, trying general search..." >> "$LOG"
    SEARCH_RESP=$(api_request "https://api.spotify.com/v1/search?q=$ENCODED&type=track,artist,album&limit=5" \
        -H "Authorization: Bearer $TOKEN") || exit 1
    request_current || exit 0
    URI=$(printf '%s' "$SEARCH_RESP" | pick_track)
    # Prioritize track > artist > album
    [ -n "$URI" ] || URI=$(echo "$SEARCH_RESP" | grep -oE 'spotify:artist:[a-zA-Z0-9]+' | head -n 1)
    [ -n "$URI" ] || URI=$(echo "$SEARCH_RESP" | grep -oE 'spotify:album:[a-zA-Z0-9]+' | head -n 1)
fi

flock -u 9
if [ "$RADIO" = 1 ]; then
    case "$URI" in spotify:track:*) valid_id "${URI#spotify:track:}" || personal_error 'No usable radio seed track';;
        *) personal_error 'No usable radio seed track';; esac
    # shortcut: station URIs work with the bundled Soloist; recheck after player upgrades.
    URI="spotify:station:track:${URI#spotify:track:}"
fi
if [ -n "$URI" ]; then
    echo "[search] Found a catalog URI; checking current request before playback" >> "$LOG"
    request_dispatch play "$URI"
else
    echo "[search] No usable match; playback unchanged" >> "$LOG"
    exit 1
fi
