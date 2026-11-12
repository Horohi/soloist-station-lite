#!/bin/sh
set -e

# YAM v2 provides YAM_PKG, YAM_ROOT, YAM_ETC, YAM_VAR, YAM_RUN, YAM_PLATFORM.
if [ -n "${YAM_PKG:-}" ] && [ -d "$YAM_PKG/share/horohi.soloist/bin" ]; then
    PKG_DIR="$YAM_PKG/share/horohi.soloist"
else
    PKG_DIR="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
fi
[ -d "$PKG_DIR/bin" ] || PKG_DIR="${YAM_ROOT:-/data/yam}/share/horohi.soloist"
[ -d "$PKG_DIR/bin" ] || PKG_DIR="/data/opt/soloist"

if [ -z "${YAM_ROOT:-}" ]; then
    case "$PKG_DIR" in
        */pkgs/horohi/soloist/*/share/horohi.soloist) YAM_ROOT="${PKG_DIR%/pkgs/horohi/soloist/*/share/horohi.soloist}" ;;
        */share/horohi.soloist) YAM_ROOT="${PKG_DIR%/share/horohi.soloist}" ;;
        *) YAM_ROOT="/data/yam" ;;
    esac
fi

_etc_base="${YAM_ETC:-$YAM_ROOT/etc}"
_var_base="${YAM_VAR:-$YAM_ROOT/var}"
if [ "$YAM_ROOT" != "/data/yam" ]; then
    [ "$_etc_base" != "/data/yam/etc" ] || _etc_base="$YAM_ROOT/etc"
    [ "$_var_base" != "/data/yam/var" ] || _var_base="$YAM_ROOT/var"
fi

if [ -n "${YAM_VAR:-}" ] || [ -n "${YAM_ETC:-}" ] || [ -n "${YAM_PKG:-}" ] || [ "$PKG_DIR" != "/data/opt/soloist" ]; then
    CONFIG_DIR="$_etc_base/horohi.soloist"
    DATA_DIR="$_var_base/horohi.soloist"
else
    CONFIG_DIR="/data/opt/soloist"
    DATA_DIR="/data/opt/soloist/data"
fi
CACHE_DIR="${YAM_RUN:-/tmp}/soloist_cache"

mkdir -p "$CONFIG_DIR" "$DATA_DIR/requests" "$DATA_DIR/cache" "$CACHE_DIR/cache" "$CACHE_DIR/Storage"
chmod 700 "$DATA_DIR/requests" 2>/dev/null || true
if [ ! -L "$DATA_DIR/cache/Storage" ]; then
    rm -rf "$DATA_DIR/cache/Storage" 2>/dev/null || true
    ln -sfn "$CACHE_DIR/Storage" "$DATA_DIR/cache/Storage" 2>/dev/null || true
fi

# Enable zram compressed swap (64MB) if available and inactive
if [ -e /dev/zram0 ] && [ "$(cat /sys/block/zram0/disksize 2>/dev/null)" = "0" ]; then
    echo 67108864 > /sys/block/zram0/disksize 2>/dev/null || true
    mkswap /dev/zram0 >/dev/null 2>&1 || true
    swapon /dev/zram0 >/dev/null 2>&1 || true
    sysctl -w vm.swappiness=20 >/dev/null 2>&1 || true
    sysctl -w vm.vfs_cache_pressure=150 >/dev/null 2>&1 || true
fi

CONFIG_FILE="$CONFIG_DIR/config.env"
if [ ! -f "$CONFIG_FILE" ] && [ -f "$PKG_DIR/config.env.example" ]; then
    cp "$PKG_DIR/config.env.example" "$CONFIG_FILE" 2>/dev/null || true
fi

API_KEY=""
DEVICE_NAME=""
if [ -f "$CONFIG_FILE" ]; then
    . "$CONFIG_FILE"
fi

# Fallback: check legacy start.sh
if [ -z "$API_KEY" ] && [ -f "/data/opt/soloist/start.sh" ]; then
    API_KEY=$(grep -o 'API_KEY="[^"]*"' /data/opt/soloist/start.sh 2>/dev/null | cut -d'"' -f2 || true)
fi

if [ -z "$API_KEY" ]; then
    echo "[service] Warning: API_KEY is not set in $CONFIG_FILE (using dummy placeholder)" >&2
    API_KEY="dummy"
fi

COMPAT=""
if [ -z "$DEVICE_NAME" ]; then
    DEVICE_NAME="Yandex Station"
    if [ -f /proc/device-tree/compatible ]; then
        COMPAT=$(tr '\0' ' ' < /proc/device-tree/compatible 2>/dev/null || true)
        case "$COMPAT" in
            *station-micro*|*micro*) DEVICE_NAME="Yandex Station Lite" ;;
            *plum*) DEVICE_NAME="Yandex Station Lite 2" ;;
            *station-mini2*) DEVICE_NAME="Yandex Station Mini 2" ;;
            *station-mini3*|*bergamot*) DEVICE_NAME="Yandex Station Mini 3" ;;
            *station-mini*) DEVICE_NAME="Yandex Station Mini" ;;
            *station-midi*|*midi*) DEVICE_NAME="Yandex Station 2" ;;
            *cucumber*|*amlogic,a5*|*amlogic,\ a5*) DEVICE_NAME="Yandex Station Midi" ;;
            *orion*) DEVICE_NAME="Yandex Station Max" ;;
            *saturn*) DEVICE_NAME="Yandex Station Max Zigbee" ;;
        esac
    fi
elif [ -f /proc/device-tree/compatible ]; then
    COMPAT=$(tr '\0' ' ' < /proc/device-tree/compatible 2>/dev/null || true)
fi

# Audio and memory allocation tuning
CARD_NAME=$(cat /proc/asound/cards 2>/dev/null | awk '/\[/{print $2; exit}' || echo "default")
case "$COMPAT" in
    *plum*)
        export APULSE_PLAYBACK_DEVICE="sysdefault:CARD=$CARD_NAME"
        ;;
    *)
        export APULSE_PLAYBACK_DEVICE=plug:default
        ;;
esac
export APULSE_DIAG=0
export APULSE_EXTERNAL_VOLUME=1
export APULSE_YIELD_PATH=/tmp/soloist.yield
export ALSA_PLUGIN_DIR="$PKG_DIR/lib64/alsa-lib"
export CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
export MALLOC_ARENA_MAX=1
export MALLOC_TRIM_THRESHOLD_=131072
export MALLOC_MMAP_THRESHOLD_=131072

# Sync initial Soloist volume with current speaker/Alice ALSA Master level
INIT_VOL=$(amixer get Master 2>/dev/null | awk -F'[][]' '/%/ { gsub(/%/, "", $2); print $2; exit }' || true)
if [ -z "$INIT_VOL" ]; then
    INIT_VOL=$(amixer get 'Master Playback Volume' 2>/dev/null | awk -F'[][]' '/%/ { gsub(/%/, "", $2); print $2; exit }' || true)
fi
case "$INIT_VOL" in ''|*[!0-9]*) INIT_VOL=20 ;; esac
[ "$INIT_VOL" -le 100 ] || INIT_VOL=100

export LD_LIBRARY_PATH="$PKG_DIR/lib64"
DUCK_LIB=""
[ -f "$PKG_DIR/lib64/libsoloist_duck.so" ] && DUCK_LIB="$PKG_DIR/lib64/libsoloist_duck.so"

LD_SO="$PKG_DIR/lib64/ld-linux-aarch64.so.1"
if [ -x "$LD_SO" ] && ! "$PKG_DIR/bin/soloist" --help >/dev/null 2>&1; then
    if [ -n "$DUCK_LIB" ]; then
        exec "$LD_SO" --library-path "$PKG_DIR/lib64" --preload "$DUCK_LIB" --argv0 "$PKG_DIR/bin/soloist" \
            "$PKG_DIR/bin/soloist" \
            -n "$DEVICE_NAME" \
            -k "$API_KEY" \
            -D "$DATA_DIR" \
            -C "$CACHE_DIR" \
            -w 127.0.0.1:9090 \
            --initial-volume "$INIT_VOL"
    else
        exec "$LD_SO" --library-path "$PKG_DIR/lib64" --argv0 "$PKG_DIR/bin/soloist" \
            "$PKG_DIR/bin/soloist" \
            -n "$DEVICE_NAME" \
            -k "$API_KEY" \
            -D "$DATA_DIR" \
            -C "$CACHE_DIR" \
            -w 127.0.0.1:9090 \
            --initial-volume "$INIT_VOL"
    fi
fi

[ -n "$DUCK_LIB" ] && export LD_PRELOAD="$DUCK_LIB"
exec "$PKG_DIR/bin/soloist" \
    -n "$DEVICE_NAME" \
    -k "$API_KEY" \
    -D "$DATA_DIR" \
    -C "$CACHE_DIR" \
    -w 127.0.0.1:9090 \
    --initial-volume "$INIT_VOL"
