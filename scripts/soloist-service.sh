#!/bin/sh
set -e

# YAM v2 provides:
# YAM_PKG: path to immutable package version directory (/data/yam/pkgs/horohi/soloist/1.2.0)
# YAM_ROOT: package manager root (/data/yam)
# YAM_ETC: configuration directory (/data/yam/etc)
# YAM_VAR: mutable data directory (/data/yam/var)
# YAM_RUN: runtime directory (/tmp/yam)
# YAM_PLATFORM: current platform profile (e.g. yandexmicro)

PKG_DIR="${YAM_PKG:-$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)}"
[ -d "$PKG_DIR/bin" ] || PKG_DIR="/data/opt/soloist"

CONFIG_DIR="${YAM_ETC:-$PKG_DIR/data}/horohi.soloist"
DATA_DIR="${YAM_VAR:-$PKG_DIR/data}/horohi.soloist"
CACHE_DIR="${YAM_RUN:-/tmp}/soloist_cache"

mkdir -p "$CONFIG_DIR" "$DATA_DIR" "$CACHE_DIR/cache" "$CACHE_DIR/Storage" "$DATA_DIR/cache"
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
if [ ! -f "$CONFIG_FILE" ]; then
    if [ -f "$PKG_DIR/share/horohi.soloist/config.env.example" ]; then
        cp -n "$PKG_DIR/share/horohi.soloist/config.env.example" "$CONFIG_FILE" 2>/dev/null || true
    fi
fi

API_KEY=""
DEVICE_NAME=""
if [ -f "$CONFIG_FILE" ]; then
    . "$CONFIG_FILE"
fi

# Fallback: check legacy start.sh or station config
if [ -z "$API_KEY" ] && [ -f "/data/opt/soloist/start.sh" ]; then
    API_KEY=$(grep -o 'API_KEY="[^"]*"' /data/opt/soloist/start.sh 2>/dev/null | cut -d'"' -f2 || true)
fi

[ -n "$API_KEY" ] || API_KEY="dummy"

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
        esac
    fi
fi

# Audio and memory allocation tuning
export APULSE_PLAYBACK_DEVICE=plug:default
export APULSE_DIAG=0
export ALSA_PLUGIN_DIR="$PKG_DIR/lib64/alsa-lib"
export CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
export MALLOC_ARENA_MAX=1
export MALLOC_TRIM_THRESHOLD_=131072
export MALLOC_MMAP_THRESHOLD_=131072

export LD_LIBRARY_PATH="$PKG_DIR/lib64"
exec "$PKG_DIR/bin/soloist" \
    -n "$DEVICE_NAME" \
    -k "$API_KEY" \
    -D "$DATA_DIR" \
    -C "$CACHE_DIR" \
    -w 127.0.0.1:9090 \
    --initial-volume 100
