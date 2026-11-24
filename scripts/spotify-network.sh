#!/bin/sh
# Spotify-only SNI proxy selection. Never changes the system DNS resolver.
set -u
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD

BASE_DIR="${SOLOIST_NET_BASE:-$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)}"
STATE_DIR="${SOLOIST_NET_STATE:-$BASE_DIR/data}"
CONF_FILE="${SOLOIST_NET_CONF:-$BASE_DIR/spotify-network.conf}"
HOSTS_FILE="${SOLOIST_HOSTS_FILE:-/etc/hosts}"
CURL_BIN="${SOLOIST_NET_CURL:-$BASE_DIR/bin/curl}"
[ -x "$CURL_BIN" ] || CURL_BIN=curl
BEGIN='# BEGIN SOLOIST SPOTIFY PROXY'
END='# END SOLOIST SPOTIFY PROXY'
CANDIDATES="${SOLOIST_NET_CANDIDATES:-5.175.161.40 107.175.76.134 192.255.159.240 192.255.159.241 192.3.233.253 193.233.112.67 193.233.112.88}"

valid_ip() {
    printf '%s\n' "$1" | awk -F. 'NF != 4 { exit 1 } {for (i=1; i<=4; i++) if ($i !~ /^[0-9]+$/ || $i > 255) exit 1}'
}

probe_endpoint() {
    ip=${1:-}
    host=$2
    url=$3
    expected=$4
    if [ -n "$ip" ]; then
        valid_ip "$ip" || return 1
        status=$("$CURL_BIN" --noproxy '*' -sS -o /dev/null --connect-timeout 2 --max-time 4 \
            --resolve "$host:443:$ip" -w '%{http_code}' "$url" 2>/dev/null) || return 1
    else
        status=$("$CURL_BIN" --noproxy '*' -sS -o /dev/null --connect-timeout 2 --max-time 4 \
            -w '%{http_code}' "$url" 2>/dev/null) || return 1
    fi
    case "$expected" in
        *"|$status|"*) return 0 ;;
        *) return 1 ;;
    esac
}

probe() {
    ip=${1:-}
    probe_endpoint "$ip" api.spotify.com 'https://api.spotify.com/v1/me' '|200|401|' &&
    probe_endpoint "$ip" accounts.spotify.com 'https://accounts.spotify.com/api/token' '|200|400|401|405|' &&
    probe_endpoint "$ip" spclient.wg.spotify.com 'https://spclient.wg.spotify.com/' '|200|401|404|'
}

current_ip() {
    awk -v begin="$BEGIN" -v end="$END" '
        $0==begin {managed=1; next} $0==end {managed=0; next}
        /^# Spotify SNI Proxy \(Bypass 403 geoblock\)$/ {legacy=1; next}
        (managed || (legacy && $1=="5.175.161.40")) && $2=="api.spotify.com" {print $1; exit}
    ' "$HOSTS_FILE" 2>/dev/null
}

read_mode() {
    sed -n 's/^SOLOIST_NETWORK_MODE="\{0,1\}\([a-z]*\)"\{0,1\}$/\1/p' "$CONF_FILE" 2>/dev/null | head -n 1
}

write_hosts() {
    selected=${1:-}
    [ -z "$selected" ] || valid_ip "$selected" || return 1
    [ -f "$HOSTS_FILE" ] || return 1
    remounted=0
    if [ "$HOSTS_FILE" = /etc/hosts ] && [ "${SOLOIST_NET_ROOT_WRITABLE:-0}" != 1 ]; then
        root_mode=$(awk '$2=="/" {print "," $4 ","; exit}' /proc/mounts)
        case "$root_mode" in *,ro,*) mount -o remount,rw / 2>/dev/null && remounted=1 || true;; esac
    fi
    if [ "$remounted" = 1 ]; then
        trap 'mount -o remount,ro / 2>/dev/null || true' EXIT
        trap 'exit 1' HUP INT TERM
    fi
    temp=$(mktemp "${HOSTS_FILE}.soloist.XXXXXX") || {
        [ "$remounted" = 0 ] || mount -o remount,ro / 2>/dev/null || true
        return 1
    }
    if ! awk -v begin="$BEGIN" -v end="$END" '
        $0==begin {if (managed || seen) exit 2; managed=1; seen=1; next}
        $0==end {if (!managed) exit 2; managed=0; next}
        managed {next}
        /^# Spotify SNI Proxy \(Bypass 403 geoblock\)$/ {legacy=1; next}
        legacy && $1=="5.175.161.40" && NF==2 && ($2=="accounts.spotify.com" || $2=="api.spotify.com" || $2=="open.spotify.com" || $2=="spclient.wg.spotify.com") {next}
        {print}
        END {if (managed) exit 2}
    ' "$HOSTS_FILE" > "$temp"; then
        rm -f "$temp"
        [ "$remounted" = 0 ] || mount -o remount,ro / 2>/dev/null || true
        return 1
    fi
    if [ -n "$selected" ]; then
        # Do not override custom user mappings for these names.
        if awk '($1 !~ /^#/ && NF>=2) {for(i=2;i<=NF;i++) if ($i=="accounts.spotify.com" || $i=="api.spotify.com" || $i=="open.spotify.com" || $i=="spclient.wg.spotify.com") exit 1}' "$temp"; then :
        else
            echo '[network] Existing custom Spotify hosts entries; leaving them unchanged.' >&2
            rm -f "$temp"
            [ "$remounted" = 0 ] || mount -o remount,ro / 2>/dev/null || true
            return 1
        fi
        {
            printf '%s\n' "$BEGIN"
            for domain in accounts.spotify.com api.spotify.com open.spotify.com spclient.wg.spotify.com; do
                printf '%s %s\n' "$selected" "$domain"
            done
            printf '%s\n' "$END"
        } >> "$temp"
    fi
    chmod 644 "$temp" 2>/dev/null || true
    if ! mv -f "$temp" "$HOSTS_FILE"; then
        rm -f "$temp"
        [ "$remounted" = 0 ] || mount -o remount,ro / 2>/dev/null || true
        return 1
    fi
    [ "$remounted" = 0 ] || mount -o remount,ro / 2>/dev/null || true
}

check() {
    mkdir -p "$STATE_DIR" || return 1
    exec 9>"$STATE_DIR/spotify-network.lock" || return 1
    flock -n 9 || return 0
    mode=$(read_mode)
    [ "$mode" = auto ] || return 0
    existing=$(current_ip)
    if [ -n "$existing" ] && probe "$existing"; then
        echo "[network] Current Spotify proxy $existing is available."
        return 0
    fi
    if [ -z "$existing" ] && probe; then
        echo '[network] Current network reaches Spotify; existing settings are unchanged.'
        return 0
    fi
    for ip in $CANDIDATES; do
        [ "$(read_mode)" = auto ] || return 0
        [ "$ip" != "$existing" ] || continue
        if probe "$ip"; then
            [ "$(read_mode)" = auto ] || return 0
            if write_hosts "$ip"; then
                echo "[network] Selected Spotify proxy $ip."
                return 0
            fi
            return 1
        fi
    done
    if [ -n "$existing" ]; then
        [ "$(read_mode)" = auto ] || return 0
        if write_hosts ''; then
            if probe; then
                echo "[network] Cleared dead Spotify proxy $existing; direct network reaches Spotify."
                return 0
            fi
            [ "$(read_mode)" != auto ] || write_hosts "$existing" >/dev/null 2>&1 || true
        fi
    fi
    echo '[network] No Spotify proxy passed the check; existing entries are unchanged.' >&2
    return 1
}

status_cmd() {
    mode=$(read_mode)
    [ -n "$mode" ] || mode=off
    ip=$(current_ip || true)
    printf 'mode=%s\nproxy=%s\n' "$mode" "${ip:-none}"
}

case "${1:-check}" in
    check) check ;;
    probe) probe "${2:-}" ;;
    current) current_ip ;;
    status) status_cmd ;;
    recommend)
        if probe; then echo current; exit 0; fi
        for ip in $CANDIDATES; do
            if probe "$ip"; then echo proxy; exit 0; fi
        done
        echo unavailable
        ;;
    off)
        if ! grep -Fxq "$BEGIN" "$HOSTS_FILE" 2>/dev/null &&
           ! grep -Fxq '# Spotify SNI Proxy (Bypass 403 geoblock)' "$HOSTS_FILE" 2>/dev/null; then
            exit 0
        fi
        mkdir -p "$STATE_DIR" || exit 1
        exec 9>"$STATE_DIR/spotify-network.lock" || exit 1
        flock -w 10 9 || { echo '[network] Network check is busy; retry disabling.' >&2; exit 1; }
        write_hosts '' ;;
    *) echo 'Usage: spotify-network.sh {check|probe [IP]|current|recommend|status|off}' >&2; exit 2 ;;
esac
