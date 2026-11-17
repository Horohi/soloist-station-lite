#!/bin/sh
# Standalone activation and cache guard, launched separately so stop.sh can
# identify and stop it across service restarts.
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD

INSTALL_DIR="${1:-/data/opt/soloist}"
CACHE_DIR="${2:-/tmp/soloist_cache/cache}"

for attempt in $(seq 1 15); do
    sleep 2
    STATUS=$(LD_LIBRARY_PATH="$INSTALL_DIR/lib64" "$INSTALL_DIR/bin/soloist" ctl -w 127.0.0.1:9090 status 2>/dev/null || true)
    if echo "$STATUS" | grep -qi "logged in: yes"; then
        LD_LIBRARY_PATH="$INSTALL_DIR/lib64" "$INSTALL_DIR/bin/soloist" ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 || true
        break
    fi
done

network_next=0
while true; do
    sleep 30
    network_now=$(date +%s 2>/dev/null || echo 0)
    if [ "$network_now" -ge "$network_next" ] && [ -f "$INSTALL_DIR/scripts/spotify-network.sh" ]; then
        network_next=$((network_now + 3600))
        SOLOIST_NET_BASE="$INSTALL_DIR" SOLOIST_NET_STATE="$INSTALL_DIR/data" \
            SOLOIST_NET_CONF="$INSTALL_DIR/spotify-network.conf" \
            sh "$INSTALL_DIR/scripts/spotify-network.sh" check >> /tmp/soloist-network.log 2>&1 &
    fi
    if [ -f "$INSTALL_DIR/scripts/control.sh" ]; then
        sh "$INSTALL_DIR/scripts/control.sh" clean-cache >/dev/null 2>&1 || true
    elif [ -d "$CACHE_DIR" ]; then
        find "$CACHE_DIR" -type f -mmin +1 -exec rm -f {} + 2>/dev/null || true
        CACHE_KB=$(du -sk "$CACHE_DIR" 2>/dev/null | awk '{print $1}')
        if [ "${CACHE_KB:-0}" -gt 16384 ]; then
            rm -rf "$CACHE_DIR"/* 2>/dev/null || true
        fi
    fi
done
