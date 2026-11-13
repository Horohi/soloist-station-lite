#!/bin/sh
# Replace only the known generated guard blocks, preserving the device key and
# any other local changes in start.sh. Atomic rename keeps a running shell safe.
set -e
INSTALL_DIR="${1:-/data/opt/soloist}"
case "$INSTALL_DIR" in ''|*[!A-Za-z0-9_./-]*) exit 1;; esac
START="$INSTALL_DIR/start.sh"
[ -f "$START" ] || exit 0

if grep -Fxq '# Activation & RAM cache guard' "$START" && grep -Fq '    # 2. RAM guard:' "$START"; then
    MODE=combined
elif grep -Fxq '# Activation guard' "$START" && grep -Fxq '# RAM guard: keep tmpfs audio cache under 16MB' "$START"; then
    MODE=separate
else
    exit 0
fi

umask 077
TMP_START=$(mktemp "$INSTALL_DIR/.start.sh.XXXXXX") || exit 1
trap 'rm -f "$TMP_START"' EXIT
SOLOIST_INSTALL_DIR="$INSTALL_DIR" awk -v mode="$MODE" '
    function emit_guard() {
        if (emitted) return
        path = ENVIRON["SOLOIST_INSTALL_DIR"]
        print "# Managed activation and RAM cache guard"
        print "/bin/sh \"" path "/scripts/soloist-ram-guard.sh\" \"" path "\" &"
        print "echo $! > \"" path "/data/ram_guard.pid\""
        emitted = 1
    }
    !skip && mode == "combined" && $0 == "# Activation & RAM cache guard" {
        skip = 1; blocks++; emit_guard(); next
    }
    !skip && mode == "separate" && ($0 == "# Activation guard" || $0 == "# RAM guard: keep tmpfs audio cache under 16MB") {
        skip = 1; blocks++; emit_guard(); next
    }
    skip && $0 == ") &" { skip = 0; closed++; next }
    skip { next }
    { print }
    END {
        expected = mode == "combined" ? 1 : 2
        if (skip || blocks != expected || closed != expected) exit 1
    }
' "$START" > "$TMP_START"
chmod 700 "$TMP_START"
mv -f "$TMP_START" "$START"
