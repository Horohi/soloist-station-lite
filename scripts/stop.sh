#!/bin/sh
if [ -f /data/opt/soloist/data/bridge.pid ]; then
    kill -9 $(cat /data/opt/soloist/data/bridge.pid) 2>/dev/null || true
    rm -f /data/opt/soloist/data/bridge.pid
fi
killall -9 soloist-bridge 2>/dev/null || true

if [ -f /data/opt/soloist/data/soloist.pid ]; then
    kill -9 $(cat /data/opt/soloist/data/soloist.pid) 2>/dev/null || true
    rm -f /data/opt/soloist/data/soloist.pid
fi
killall -9 soloist 2>/dev/null || true

echo "Spotify Soloist and Bridge stopped."
