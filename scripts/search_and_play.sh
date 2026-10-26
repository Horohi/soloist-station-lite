#!/bin/sh
QUERY="$1"
if [ -z "$QUERY" ]; then
    exit 0
fi

LOG="/tmp/soloist-search.log"
echo "[search] ===== Incoming query: '$QUERY' =====" >> "$LOG"

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

# Clean Alice greetings and filler words
CLEAN=$(echo "$QUERY" | tr '[:upper:]' '[:lower:]' | \
    sed -e 's/алиса,//g' -e 's/алиса//g' \
        -e 's/Алиса,//g' -e 's/Алиса//g' \
        -e 's/пожалуйста//g' -e 's/Пожалуйста//g' \
        -e 's/давай//g' -e 's/Давай//g' | xargs)

# Strip leading action verbs
TERM=$(echo "$CLEAN" | sed -E 's/^(включи|поставь|сыграй|запусти|найди|слушать|вруби|Включи|Поставь|Сыграй|Запусти|Найди|Слушать|Вруби)[[:space:]]+//g' | xargs)

# Detect search type hint
TYPE="track"
case "$TERM" in
    *альбом*|*Альбом*)
        TYPE="album"
        TERM=$(echo "$TERM" | sed -E 's/^(альбом|Альбом)[[:space:]]+//g' | xargs)
        ;;
    *групп*|*исполнител*|*артист*|*певц*|*певиц*)
        TYPE="artist"
        TERM=$(echo "$TERM" | sed -E 's/^(группу|исполнителя|артиста|певца|певицу|Группу|Исполнителя)[[:space:]]+//g' | xargs)
        ;;
    *песн*|*трек*)
        TYPE="track"
        TERM=$(echo "$TERM" | sed -E 's/^(песню|песня|трек|Песню|Песня|Трек)[[:space:]]+//g' | xargs)
        ;;
esac

echo "[search] Cleaned search term: '$TERM' (preferred type: $TYPE)" >> "$LOG"

# If search term is empty or generic 'музыку' -> resume or play default
if [ -z "$TERM" ] || [ "$TERM" = "музыку" ] || [ "$TERM" = "что-нибудь" ]; then
    echo "[search] Generic play request, resuming..." >> "$LOG"
    "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play
    sleep 0.5
    NOW_STATE=$("$SOLOIST_BIN" ctl -w 127.0.0.1:9090 now 2>/dev/null || true)
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
    echo "[search] Missing $API_CONF, resuming existing playlist" >> "$LOG"
    exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play
fi

. "$API_CONF"
if [ -z "$SPOTIFY_CLIENT_ID" ] || [ -z "$SPOTIFY_CLIENT_SECRET" ]; then
    echo "[search] Spotify API keys not set in $API_CONF" >> "$LOG"
    exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play
fi

CURL_BIN="/data/opt/soloist/bin/curl"
[ -x "$CURL_BIN" ] || CURL_BIN="curl"
CURL_OPTS="--resolve api.spotify.com:443:5.175.161.40 --resolve accounts.spotify.com:443:5.175.161.40 -s"

# Token management
NOW=$(date +%s 2>/dev/null || echo 0)
TOKEN=""
if [ -f /tmp/spotify_token_exp ] && [ -f /tmp/spotify_token ]; then
    EXP=$(cat /tmp/spotify_token_exp 2>/dev/null || echo 0)
    if [ "$NOW" -lt "$EXP" ]; then
        TOKEN=$(cat /tmp/spotify_token)
    fi
fi

if [ -z "$TOKEN" ]; then
    echo "[search] Fetching new access token from Spotify..." >> "$LOG"
    AUTH_RESP=$($CURL_BIN $CURL_OPTS -X POST "https://accounts.spotify.com/api/token" \
        -H "Content-Type: application/x-www-form-urlencoded" \
        -d "grant_type=client_credentials&client_id=$SPOTIFY_CLIENT_ID&client_secret=$SPOTIFY_CLIENT_SECRET")
    TOKEN=$(echo "$AUTH_RESP" | grep -oE '"access_token":"[^"]+"' | cut -d'"' -f4)
    if [ -n "$TOKEN" ]; then
        echo "$TOKEN" > /tmp/spotify_token
        echo $((NOW + 3300)) > /tmp/spotify_token_exp
        echo "[search] Access token successfully updated" >> "$LOG"
    else
        echo "[search] ERROR: Failed to get token: $AUTH_RESP" >> "$LOG"
        exec /data/opt/soloist/bin/soloist ctl -w 127.0.0.1:9090 play
    fi
fi

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

SEARCH_RESP=$($CURL_BIN $CURL_OPTS "https://api.spotify.com/v1/search?q=$ENCODED&type=$TYPE&limit=$LIMIT" \
    -H "Authorization: Bearer $TOKEN")

if [ "$TYPE" = "track" ]; then
    URI=$(echo "$SEARCH_RESP" | awk -v query="$TERM" -v censor="$CENSORSHIP" '
    BEGIN {
        RS = "\\{\"album\":"
        best_score = -1
        best_uri = ""
        nwords = split(tolower(query), qwords, " ")
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
    }')
else
    URI=$(echo "$SEARCH_RESP" | grep -oE "spotify:$TYPE:[a-zA-Z0-9]+" | head -n 1)
fi

# Fallback: if preferred type had no matches, search any playable type
if [ -z "$URI" ]; then
    echo "[search] No match for type=$TYPE, trying general search..." >> "$LOG"
    SEARCH_RESP=$($CURL_BIN $CURL_OPTS "https://api.spotify.com/v1/search?q=$ENCODED&type=track,artist,album&limit=5" \
        -H "Authorization: Bearer $TOKEN")
    URI=$(echo "$SEARCH_RESP" | awk -v query="$TERM" -v censor="$CENSORSHIP" '
    BEGIN {
        RS = "\\{\"album\":"
        best_score = -1
        best_uri = ""
        nwords = split(tolower(query), qwords, " ")
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
    }')
    # Prioritize track > artist > album
    [ -n "$URI" ] || URI=$(echo "$SEARCH_RESP" | grep -oE 'spotify:artist:[a-zA-Z0-9]+' | head -n 1)
    [ -n "$URI" ] || URI=$(echo "$SEARCH_RESP" | grep -oE 'spotify:album:[a-zA-Z0-9]+' | head -n 1)
fi

if [ -n "$URI" ]; then
    echo "[search] SUCCESS: Found URI '$URI' -> sending to Soloist" >> "$LOG"
    "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 || true
    exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play "$URI"
else
    echo "[search] No matches found for '$TERM' in Spotify API" >> "$LOG"
    "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 || true
    exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play
fi
