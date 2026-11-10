#!/bin/sh
# Post-install hook executed by YAM v2 after unpacking horohi/soloist.
set -e

YAM_ROOT="${YAM_ROOT:-/data/yam}"
if [ -n "${YAM_PKG:-}" ] && [ -d "$YAM_PKG/share/horohi.soloist" ]; then
    PKG_SHARE="$YAM_PKG/share/horohi.soloist"
else
    PKG_SHARE="$YAM_ROOT/share/horohi.soloist"
fi

YAM_ETC="${YAM_ETC:-$YAM_ROOT/etc}"
YAM_VAR="${YAM_VAR:-$YAM_ROOT/var}"
if [ "$YAM_ROOT" != "/data/yam" ]; then
    [ "$YAM_ETC" != "/data/yam/etc" ] || YAM_ETC="$YAM_ROOT/etc"
    [ "$YAM_VAR" != "/data/yam/var" ] || YAM_VAR="$YAM_ROOT/var"
fi
YAM_RUN="${YAM_RUN:-/tmp/yam}"

CONF_DIR="$YAM_ETC/horohi.soloist"
DATA_ROOT="$YAM_VAR/horohi.soloist"
CACHE_DIR="$YAM_RUN/soloist_cache"

mkdir -p "$CONF_DIR" "$DATA_ROOT/requests" "$DATA_ROOT/cache" "$CACHE_DIR/cache" "$CACHE_DIR/Storage"
chmod 700 "$DATA_ROOT/requests" 2>/dev/null || true
if [ ! -L "$DATA_ROOT/cache/Storage" ]; then
    rm -rf "$DATA_ROOT/cache/Storage" 2>/dev/null || true
    ln -sfn "$CACHE_DIR/Storage" "$DATA_ROOT/cache/Storage" 2>/dev/null || true
fi

if [ -f "$PKG_SHARE/config.env.example" ] && [ ! -f "$CONF_DIR/config.env.example" ]; then
    cp "$PKG_SHARE/config.env.example" "$CONF_DIR/config.env.example" 2>/dev/null || true
fi
if [ -f "$PKG_SHARE/config.env.example" ] && [ ! -f "$CONF_DIR/config.env" ]; then
    cp "$PKG_SHARE/config.env.example" "$CONF_DIR/config.env" 2>/dev/null || true
    chmod 600 "$CONF_DIR/config.env" 2>/dev/null || true
fi
if [ -f "$PKG_SHARE/spotify_api.conf.example" ] && [ ! -f "$CONF_DIR/spotify_api.conf.example" ]; then
    cp "$PKG_SHARE/spotify_api.conf.example" "$CONF_DIR/spotify_api.conf.example" 2>/dev/null || true
fi

# System-level initialization runs only for the real station YAM installation,
# never during offline `yam --root <sandbox> install` verification on a host machine.
if [ "$YAM_ROOT" = "/data/yam" ] && [ -d /etc ]; then
    # Migrate settings & secrets from legacy standalone /data/opt/soloist installation if present
    if [ -d /data/opt/soloist ]; then
        if [ -f /data/opt/soloist/start.sh ] && grep -q '^API_KEY=""' "$CONF_DIR/config.env" 2>/dev/null; then
            OLD_KEY=$(grep -o 'API_KEY="[^"]*"' /data/opt/soloist/start.sh 2>/dev/null | cut -d'"' -f2 || true)
            OLD_NAME=$(grep -o -- '-n "[^"]*"' /data/opt/soloist/start.sh 2>/dev/null | head -n 1 | cut -d'"' -f2 || true)
            if [ -n "$OLD_KEY" ] && [ "$OLD_KEY" != "dummy" ]; then
                printf 'API_KEY="%s"\nDEVICE_NAME="%s"\n' "$OLD_KEY" "$OLD_NAME" > "$CONF_DIR/config.env"
                chmod 600 "$CONF_DIR/config.env" 2>/dev/null || true
            fi
        fi
        if [ -f /data/opt/soloist/spotify_api.conf ] && [ ! -s "$CONF_DIR/spotify_api.conf" ]; then
            cp /data/opt/soloist/spotify_api.conf "$CONF_DIR/spotify_api.conf" 2>/dev/null || true
            chmod 600 "$CONF_DIR/spotify_api.conf" 2>/dev/null || true
        fi
        for item in favorites.uri censorship; do
            if [ -f "/data/opt/soloist/data/$item" ] && [ ! -f "$DATA_ROOT/$item" ]; then
                cp "/data/opt/soloist/data/$item" "$DATA_ROOT/$item" 2>/dev/null || true
            fi
        done
        if [ -f /data/opt/soloist/data/requests/led_state ] && [ ! -f "$DATA_ROOT/requests/led_state" ]; then
            cp /data/opt/soloist/data/requests/led_state "$DATA_ROOT/requests/led_state" 2>/dev/null || true
        fi
        [ -x /data/opt/soloist/stop.sh ] && /data/opt/soloist/stop.sh >/dev/null 2>&1 || true
    fi

    # Install BusyBox init hook & ensure CA certificates symlink exists
    mount -o remount,rw / 2>/dev/null || true
    trap 'mount -o remount,ro / 2>/dev/null || true' EXIT

    if [ ! -f /etc/ssl/certs/ca-certificates.crt ] && [ -f /system/vendor/quasar/ca-certificates.crt ]; then
        mkdir -p /etc/ssl/certs
        ln -sf /system/vendor/quasar/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt 2>/dev/null || true
    fi

    if [ -f /etc/init.d/S99spotify-connect ]; then
        chmod 644 /etc/init.d/S99spotify-connect 2>/dev/null || true
    fi

    mkdir -p /etc/init.d
    cat > /etc/init.d/S98soloist << 'INIT_EOF'
#!/bin/sh
export YAM_ROOT=/data/yam
export YAM_ETC=/data/yam/etc
export YAM_VAR=/data/yam/var
export YAM_RUN=/tmp/yam
INIT_SCRIPT="$YAM_ROOT/share/horohi.soloist/scripts/yam-init.sh"
[ -x "$INIT_SCRIPT" ] || exit 0
case "${1:-start}" in
    start|stop|restart|status)
        exec sh "$INIT_SCRIPT" "$1"
        ;;
esac
INIT_EOF
    chmod 755 /etc/init.d/S98soloist 2>/dev/null || true

    mount -o remount,ro / 2>/dev/null || true
    trap - EXIT

    if [ -x "$PKG_SHARE/scripts/yam-init.sh" ]; then
        YAM_ROOT="$YAM_ROOT" YAM_PKG="${YAM_PKG:-}" YAM_ETC="$YAM_ETC" YAM_VAR="$YAM_VAR" YAM_RUN="$YAM_RUN" \
            sh "$PKG_SHARE/scripts/yam-init.sh" restart || true
    fi

    if grep -q '^API_KEY=""' "$CONF_DIR/config.env" 2>/dev/null; then
        echo "[horohi/soloist] Запустите интерактивную настройку: soloist setup"
        echo "[horohi/soloist] (или вручную пропишите API_KEY в $CONF_DIR/config.env и выполните: soloist restart)"
    fi
fi

exit 0
