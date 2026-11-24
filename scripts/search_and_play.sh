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
if [ -n "${YAM_PKG:-}" ] && [ -d "$YAM_PKG/share/horohi.soloist/bin" ]; then
    BASE_DIR="$YAM_PKG/share/horohi.soloist"
else
    BASE_DIR="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
    if [ ! -d "$BASE_DIR/bin" ] && [ ! -f "$BASE_DIR/scripts/request_state.sh" ]; then
        if [ -d "${YAM_ROOT:-/data/yam}/share/horohi.soloist/bin" ]; then
            BASE_DIR="${YAM_ROOT:-/data/yam}/share/horohi.soloist"
        else
            BASE_DIR="/data/opt/soloist"
        fi
    fi
fi

SOLOIST_BIN="$BASE_DIR/bin/soloist"
CURL_BIN="$BASE_DIR/bin/curl"
[ -x "$CURL_BIN" ] || CURL_BIN="curl"

if [ -f "$BASE_DIR/scripts/request_state.sh" ]; then
    . "$BASE_DIR/scripts/request_state.sh"
elif [ -f "$BASE_DIR/bin/request_state.sh" ]; then
    . "$BASE_DIR/bin/request_state.sh"
else
    . /data/opt/soloist/scripts/request_state.sh
fi

API_CONF="$YAM_CONF_DIR/spotify_api.conf"
[ -f "$API_CONF" ] || API_CONF="$BASE_DIR/spotify_api.conf"
[ -f "$API_CONF" ] || API_CONF="/data/opt/soloist/spotify_api.conf"
CENSOR_FILE="$YAM_DATA_ROOT/censorship"
[ -f "$CENSOR_FILE" ] || CENSOR_FILE="$BASE_DIR/data/censorship"
[ -f "$CENSOR_FILE" ] || CENSOR_FILE="/data/opt/soloist/data/censorship"
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
        FAVORITES="$YAM_DATA_ROOT/favorites.uri"
        [ -f "$FAVORITES" ] || FAVORITES="$BASE_DIR/data/favorites.uri"
        [ -f "$FAVORITES" ] || personal_error 'Liked Songs not configured (favorites.uri)'
        [ "$(wc -c < "$FAVORITES")" -le 128 ] || personal_error 'Invalid Liked Songs URI'
        URI=$(cat "$FAVORITES") || personal_error 'Could not read Liked Songs URI'
        case "$URI" in
            spotify:playlist:*)
                valid_id "${URI#spotify:playlist:}" || personal_error 'Invalid Liked Songs URI';;
            spotify:user:*:collection)
                collection_user=${URI#spotify:user:}
                collection_user=${collection_user%:collection}
                case "$collection_user" in
                    ''|*[!A-Za-z0-9._-]*) personal_error 'Invalid Liked Songs URI';;
                esac
                NOW_STATE=$(request_cli now --json) || personal_error 'Could not read the current Spotify context'
                if ! printf '%s\n' "$NOW_STATE" |
                    LC_ALL=C awk -v collection="$URI" -f "$BASE_DIR/scripts/spotify-search.awk"; then
                    personal_error 'Start Liked Songs on this device in the Spotify app first'
                fi
                request_dispatch play
                exit $?;;
            *) personal_error 'Invalid Liked Songs URI';;
        esac
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
EXPLICIT_TYPE=0
ARTIST=""
ALLOW_GENERIC=0
case "$TERM" in
    "плейлист")
        personal_error 'Missing playlist name' ;;
    "плейлист "*)
        [ "$RADIO" = 0 ] || personal_error 'Radio needs a track name'
        TYPE="playlist"; EXPLICIT_TYPE=1; TERM="${TERM#* }" ;;
    "альбом "*)
        TYPE="album"; EXPLICIT_TYPE=1; TERM="${TERM#* }" ;;
    "группу "*|"группа "*|"исполнителя "*|"исполнитель "*|"артиста "*|"артист "*|"певца "*|"певицу "*)
        TYPE="artist"; EXPLICIT_TYPE=1; TERM="${TERM#* }" ;;
    "песню "*|"песня "*|"трек "*)
        TYPE="track"; EXPLICIT_TYPE=1; TERM="${TERM#* }" ;;
esac
[ "$RADIO" = 0 ] || TYPE=track
if [ "$TYPE" = track ] && { [ "$EXPLICIT_TYPE" = 1 ] || [ "$RADIO" = 1 ]; }; then
    case "$TERM" in
        *' исполнителя '*)
            ARTIST=${TERM#* исполнителя }
            TERM=${TERM%% исполнителя *}
            ;;
        *' группы '*)
            ARTIST=${TERM#* группы }
            TERM=${TERM%% группы *}
            ;;
    esac
fi

echo "[search] Cleaned search term: '$TERM' (preferred type: $TYPE)" >> "$LOG"

# If search term is empty or generic 'музыку' -> resume or play default
if [ "$RADIO" = 0 ] && [ "$EXPLICIT_TYPE" = 0 ] &&
   { [ -z "$TERM" ] || [ "$TERM" = "музыку" ] || [ "$TERM" = "что-нибудь" ]; }; then
    echo "[search] Generic play request, resuming..." >> "$LOG"
    request_dispatch play || exit 1
    _resume_tries=0
    while [ "$_resume_tries" -lt 8 ]; do
        sleep 0.3
        request_current || exit 0
        NOW_STATE=$(request_cli now) || { request_report play now "$?"; exit 1; }
        if printf '%s\n' "$NOW_STATE" | grep -qiE 'playing|buffering' && ! printf '%s\n' "$NOW_STATE" | grep -qi 'nothing'; then
            exit 0
        fi
        _resume_tries=$((_resume_tries + 1))
    done
    # If device is active with a loaded track still transitioning, do not clobber it with 'top hits'
    NOW_JSON=$(request_cli now --json 2>/dev/null || true)
    if printf '%s' "$NOW_JSON" | grep -q '"is_active":true' && printf '%s' "$NOW_JSON" | grep -q '"uri":"spotify:track:'; then
        exit 0
    fi
    # If nothing was playing, fall through and search popular hits
    TERM="top hits"
    TYPE="track"
    ALLOW_GENERIC=1
    echo "[search] Nothing was active, searching '$TERM'..." >> "$LOG"
fi

# The catalog search cannot guarantee the contents of a full context.
CENSORSHIP=0
if [ -f "$CENSOR_FILE" ]; then
    CENSORSHIP=1
    echo "[search] Censorship filter ACTIVE (filtering explicit tracks)" >> "$LOG"
    case "$TYPE" in
        playlist) personal_error 'Cannot filter explicit tracks in a playlist context' ;;
        album|artist) personal_error 'Cannot filter explicit tracks in an album or artist context' ;;
    esac
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
_network_helper="$BASE_DIR/scripts/spotify-network.sh"
_proxy_ip=""
if [ -f "$_network_helper" ]; then
    _proxy_ip=$(SOLOIST_NET_BASE="$BASE_DIR" sh "$_network_helper" current 2>/dev/null || true)
elif grep -q "5.175.161.40" /etc/hosts 2>/dev/null; then
    _proxy_ip=5.175.161.40
fi
case "$_proxy_ip" in
    ''|*[!0-9.]*) ;;
    *) CURL_OPTS="--resolve api.spotify.com:443:$_proxy_ip --resolve accounts.spotify.com:443:$_proxy_ip $CURL_OPTS" ;;
esac

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

echo "[search] Querying Spotify API: term='$TERM', type='$TYPE'..." >> "$LOG"

MARKET=$(printf '%s' "${SPOTIFY_MARKET:-}" | LC_ALL=C tr '[:lower:]' '[:upper:]')
case "$MARKET" in
    [A-Z][A-Z]) ;;
    *) MARKET="" ;;
esac

search_catalog() {
    query=$1
    types=$2
    contexts=$3
    set -- -G --max-filesize 524288 --data-urlencode "q=$query" --data-urlencode "type=$types" --data-urlencode 'limit=10'
    if [ -n "$MARKET" ]; then
        set -- "$@" --data-urlencode "market=$MARKET"
    fi
    response=$(api_request "$@" -H "Authorization: Bearer $TOKEN" 'https://api.spotify.com/v1/search') ||
        return 1
    request_current || return 1
    printf '%s' "$response" |
        LC_ALL=C SOLOIST_SEARCH_TERM="$TERM" SOLOIST_SEARCH_ARTIST="$ARTIST" \
            awk -v type="$TYPE" -v contexts="$contexts" -v censor="$CENSORSHIP" \
            -v allow_generic="$ALLOW_GENERIC" -f "$BASE_DIR/scripts/spotify-search.awk"
}

CONTEXTS=0
TYPES=$TYPE
if [ "$EXPLICIT_TYPE" = 0 ] && [ "$RADIO" = 0 ] && [ "$CENSORSHIP" = 0 ] &&
   [ "$ALLOW_GENERIC" = 0 ] && [ -z "$ARTIST" ]; then
    CONTEXTS=1
    TYPES=track,artist,album
fi

SEARCH_QUERY=$TERM
if [ -n "$ARTIST" ]; then
    case "$TERM$ARTIST" in
        *'"'*) SEARCH_QUERY="$TERM $ARTIST" ;;
        *) SEARCH_QUERY="track:\"$TERM\" artist:\"$ARTIST\"" ;;
    esac
fi
URI=$(search_catalog "$SEARCH_QUERY" "$TYPES" "$CONTEXTS") || exit 1
if [ -z "$URI" ] && [ -n "$ARTIST" ] && [ "$SEARCH_QUERY" != "$TERM $ARTIST" ]; then
    URI=$(search_catalog "$TERM $ARTIST" "$TYPES" "$CONTEXTS") || exit 1
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
    [ "$TYPE" != playlist ] || personal_error 'Playlist name not found or ambiguous'
    echo "[search] No usable match; playback unchanged" >> "$LOG"
    exit 1
fi
