#!/bin/sh
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
CONFIG_ENV="$BASE_DIR/config.env"
[ -f "$CONFIG_ENV" ] || CONFIG_ENV="/data/opt/soloist/config.env"
API_CONF="$BASE_DIR/spotify_api.conf"
[ -f "$API_CONF" ] || API_CONF="/data/opt/soloist/spotify_api.conf"
CENSOR_FILE="$BASE_DIR/data/censorship"
[ -f "$CENSOR_FILE" ] || CENSOR_FILE="/data/opt/soloist/data/censorship"

# Load configs (config.env overrides or supplements spotify_api.conf)
[ -f "$CONFIG_ENV" ] && . "$CONFIG_ENV"
[ -f "$API_CONF" ] && . "$API_CONF"

ANNOUNCE_TRACKS="${ANNOUNCE_TRACKS:-true}"

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

echo "[search] Cleaned search term: '$TERM' (preferred type: $TYPE)" >> "$LOG"

# If search term is empty or generic 'музыку' -> resume or play default
if [ -z "$TERM" ] || [ "$TERM" = "музыку" ] || [ "$TERM" = "что-нибудь" ]; then
    echo "[search] Generic play request, resuming..." >> "$LOG"
    "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 || true
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
if [ -z "$SPOTIFY_CLIENT_ID" ] || [ -z "$SPOTIFY_CLIENT_SECRET" ]; then
    echo "[search] Spotify API keys not set in $API_CONF or $CONFIG_ENV" >> "$LOG"
    exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play
fi

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
        rm -f /tmp/spotify_token /tmp/spotify_token_exp
        (umask 077; echo "$TOKEN" > /tmp/spotify_token; echo $((NOW + 3300)) > /tmp/spotify_token_exp)
        echo "[search] Access token successfully updated" >> "$LOG"
    else
        echo "[search] ERROR: Failed to get token: $AUTH_RESP" >> "$LOG"
        exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play
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

# Rank track results by query words hit in title (x5) or artists (x2).
# Outputs: URI<tab>ANNOUNCE_PHRASE
pick_track() {
    SEARCH_TERM="$TERM" awk -v censor="$CENSORSHIP" '
    BEGIN {
        RS = "\\{\"album\":"
        best_score = -1
        best_uri = ""
        best_ann = ""
        nwords = split(tolower(ENVIRON["SEARCH_TERM"]), qwords, " ")
    }
    NR > 1 {
        if (censor == "1" && $0 ~ /"explicit":true/) next
        uri = ""
        if (match($0, /"uri":"spotify:track:[a-zA-Z0-9]+/)) {
            uri = substr($0, RSTART + 7, RLENGTH - 7)
        }
        if (uri == "") next

        rest = $0
        pos_artists = index(rest, "},\"artists\":[")
        if (pos_artists > 0) {
            rest = substr(rest, pos_artists + 2)
        }

        artists_str = ""
        art_count = 0
        t_art = rest
        pos_end_art = index(t_art, "],")
        if (pos_end_art > 0) {
            art_block = substr(t_art, 1, pos_end_art)
            while (art_count < 3 && match(art_block, /"name":"[^"]+"/)) {
                a_name = substr(art_block, RSTART + 8, RLENGTH - 9)
                if (artists_str == "") artists_str = a_name
                else artists_str = artists_str ", " a_name
                art_count++
                art_block = substr(art_block, RSTART + RLENGTH)
            }
            rest = substr(t_art, pos_end_art + 2)
        }

        track_name = ""
        if (match(rest, /"name":"[^"]+"/)) {
            track_name = substr(rest, RSTART + 8, RLENGTH - 9)
        }

        t_clean = track_name
        sub(/[ \t]*[\(\[-][ \t]*[Rr][Ee][Mm][Aa][Ss][Tt].*/, "", t_clean)
        sub(/[ \t]*[\(\[-][ \t]*[Ll][Ii][Vv][Ee].*/, "", t_clean)
        sub(/[ \t]*[\(\[-][ \t]*[Dd][Ee][Ll][Uu][Xx][Ee].*/, "", t_clean)
        sub(/[ \t]*[\(\[-][ \t]*[Vv][Ee][Rr][Ss][Ii][Oo][Nn].*/, "", t_clean)
        sub(/[ \t]*[\(\[-][ \t]*[Bb][Oo][Nn][Uu][Ss].*/, "", t_clean)
        sub(/[ \t]*[\(\[-][ \t]*[Aa][Cc][Oo][Uu][Ss][Tt][Ii][Cc].*/, "", t_clean)
        sub(/[ \t]*[\(\[-][ \t]*[Rr][Aa][Dd][Ii][Oo].*/, "", t_clean)
        sub(/[ \t]*[\(\[-][ \t]*[Дд][Ее][Мм][Оо].*/, "", t_clean)
        track_name = t_clean

        ann = ""
        if (artists_str != "" && track_name != "") {
            ann = "Включаю " artists_str " — " track_name
        } else if (track_name != "") {
            ann = "Включаю " track_name
        }

        score = 0
        l_name = tolower(track_name)
        l_art = tolower(artists_str)
        for (i = 1; i <= nwords; i++) {
            w = qwords[i]
            if (length(w) == 0) continue
            if (index(l_name, w) > 0) score += 5
            if (index(l_art, w) > 0) score += 2
        }

        if (score > best_score) {
            best_score = score
            best_uri = uri
            best_ann = ann
        } else if (best_uri == "") {
            best_uri = uri
            best_ann = ann
        }
    }
    END {
        if (best_uri != "") print best_uri "\t" best_ann
    }'
}

pick_album() {
    awk '
    BEGIN {
        RS = "\\{\"album_type\":"
    }
    NR > 1 {
        uri = ""
        if (match($0, /"uri":"spotify:album:[a-zA-Z0-9]+/)) {
            uri = substr($0, RSTART + 7, RLENGTH - 7)
        }
        if (uri == "") next

        album_name = ""
        if (match($0, /"name":"[^"]+"/)) {
            album_name = substr($0, RSTART + 8, RLENGTH - 9)
        }

        artists_str = ""
        art_count = 0
        pos_art = index($0, "\"artists\":[")
        if (pos_art > 0) {
            art_block = substr($0, pos_art)
            pos_end = index(art_block, "],")
            if (pos_end > 0) art_block = substr(art_block, 1, pos_end)
            while (art_count < 3 && match(art_block, /"name":"[^"]+"/)) {
                a_name = substr(art_block, RSTART + 8, RLENGTH - 9)
                if (artists_str == "") artists_str = a_name
                else artists_str = artists_str ", " a_name
                art_count++
                art_block = substr(art_block, RSTART + RLENGTH)
            }
        }

        ann = ""
        if (artists_str != "" && album_name != "") {
            ann = "Включаю альбом " artists_str " — " album_name
        } else if (album_name != "") {
            ann = "Включаю альбом " album_name
        }

        print uri "\t" ann
        exit
    }'
}

pick_artist() {
    awk '
    BEGIN {
        RS = "\\{\"external_urls\":"
    }
    NR > 1 {
        uri = ""
        if (match($0, /"uri":"spotify:artist:[a-zA-Z0-9]+/)) {
            uri = substr($0, RSTART + 7, RLENGTH - 7)
        }
        if (uri == "") next

        art_name = ""
        if (match($0, /"name":"[^"]+"/)) {
            art_name = substr($0, RSTART + 8, RLENGTH - 9)
        }

        ann = ""
        if (art_name != "") {
            ann = "Включаю " art_name
        }

        print uri "\t" ann
        exit
    }'
}

# Speak track title before playback via on-device Google TTS + mp32wav + aplay
play_tts_announcement() {
    TEXT="$1"
    [ -n "$TEXT" ] || return 0
    [ "$ANNOUNCE_TRACKS" = "true" ] || [ "$ANNOUNCE_TRACKS" = "1" ] || return 0

    MP32WAV_BIN="$BASE_DIR/bin/mp32wav"
    [ -x "$MP32WAV_BIN" ] || MP32WAV_BIN="/data/opt/soloist/bin/mp32wav"
    [ -x "$MP32WAV_BIN" ] || return 0

    # Pause soloist first so it releases the ALSA audio device
    "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 pause >/dev/null 2>&1 || true
    sleep 0.1

    ENCODED_TEXT=$(printf '%s\n' "$TEXT" | awk 'BEGIN {
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

    TTS_MP3="/tmp/soloist_tts.mp3"
    TTS_WAV="/tmp/soloist_tts.wav"

    echo "[search] Announcing: '$TEXT'..." >> "$LOG"

    if $CURL_BIN -s --connect-timeout 2 --max-time 3 \
        "https://translate.google.com/translate_tts?ie=UTF-8&tl=ru&client=tw-ob&q=$ENCODED_TEXT" \
        -o "$TTS_MP3" 2>/dev/null; then
        if [ -s "$TTS_MP3" ]; then
            if "$MP32WAV_BIN" "$TTS_MP3" "$TTS_WAV" 2>/dev/null; then
                aplay -q "$TTS_WAV" 2>/dev/null || true
            fi
        fi
    fi
    rm -f "$TTS_MP3" "$TTS_WAV"
}

MATCH_LINE=""
if [ "$TYPE" = "track" ]; then
    MATCH_LINE=$(printf '%s' "$SEARCH_RESP" | pick_track)
elif [ "$TYPE" = "album" ]; then
    MATCH_LINE=$(printf '%s' "$SEARCH_RESP" | pick_album)
elif [ "$TYPE" = "artist" ]; then
    MATCH_LINE=$(printf '%s' "$SEARCH_RESP" | pick_artist)
fi

URI=$(printf '%s\n' "$MATCH_LINE" | cut -f1)
ANNOUNCE_TEXT=$(printf '%s\n' "$MATCH_LINE" | cut -f2-)

# Fallback: if preferred type had no matches, search any playable type
if [ -z "$URI" ]; then
    echo "[search] No match for type=$TYPE, trying general search..." >> "$LOG"
    SEARCH_RESP=$($CURL_BIN $CURL_OPTS "https://api.spotify.com/v1/search?q=$ENCODED&type=track,artist,album&limit=5" \
        -H "Authorization: Bearer $TOKEN")
    MATCH_LINE=$(printf '%s' "$SEARCH_RESP" | pick_track)
    URI=$(printf '%s\n' "$MATCH_LINE" | cut -f1)
    ANNOUNCE_TEXT=$(printf '%s\n' "$MATCH_LINE" | cut -f2-)

    if [ -z "$URI" ]; then
        MATCH_LINE=$(printf '%s' "$SEARCH_RESP" | pick_artist)
        URI=$(printf '%s\n' "$MATCH_LINE" | cut -f1)
        ANNOUNCE_TEXT=$(printf '%s\n' "$MATCH_LINE" | cut -f2-)
    fi
    if [ -z "$URI" ]; then
        MATCH_LINE=$(printf '%s' "$SEARCH_RESP" | pick_album)
        URI=$(printf '%s\n' "$MATCH_LINE" | cut -f1)
        ANNOUNCE_TEXT=$(printf '%s\n' "$MATCH_LINE" | cut -f2-)
    fi
fi

if [ -n "$URI" ]; then
    echo "[search] SUCCESS: Found URI '$URI' -> sending to Soloist" >> "$LOG"
    play_tts_announcement "$ANNOUNCE_TEXT"
    "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 || true
    exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play "$URI"
else
    echo "[search] No matches found for '$TERM' in Spotify API" >> "$LOG"
    "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 || true
    exec "$SOLOIST_BIN" ctl -w 127.0.0.1:9090 play
fi
