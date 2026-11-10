#!/bin/sh
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD

if [ -n "${YAM_PKG:-}" ] && [ -d "$YAM_PKG/share/horohi.soloist/bin" ]; then
    BASE_DIR="$YAM_PKG/share/horohi.soloist"
else
    BASE_DIR="$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)"
    [ -f "$BASE_DIR/scripts/control.sh" ] || BASE_DIR="${YAM_ROOT:-/data/yam}/share/horohi.soloist"
    [ -f "$BASE_DIR/scripts/control.sh" ] || BASE_DIR="/data/opt/soloist"
fi
exec sh "$BASE_DIR/scripts/control.sh" toggle
