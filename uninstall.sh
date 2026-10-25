#!/bin/sh
set -e

echo "=== Удаление Spotify Soloist + Bridge ==="
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: Требуются права root!" >&2
    exit 1
fi

[ ! -x "/data/opt/soloist/stop.sh" ] || /data/opt/soloist/stop.sh
killall -9 soloist soloist-bridge 2>/dev/null || true

mount -o remount,rw / 2>/dev/null || true
rm -f /etc/init.d/S99spotify-connect /etc/init.d/S99spotify-soloist 2>/dev/null || true
mount -o remount,ro / 2>/dev/null || true

rm -rf /data/opt/soloist /tmp/soloist_cache /tmp/soloist.log /tmp/soloist-bridge.log
echo "Spotify Soloist и Bridge успешно удалены."
