#!/bin/sh
# Idempotently add missing runtime audio/yield/ducking exports to legacy
# standalone start.sh while keeping API_KEY, device name, and local edits intact.
set -e

INSTALL_DIR="${1:-/data/opt/soloist}"
case "$INSTALL_DIR" in ''|*[!A-Za-z0-9_./-]*) exit 1;; esac
START="$INSTALL_DIR/start.sh"
[ -f "$START" ] || exit 0

COMPAT="${SOLOIST_DT_COMPAT:-}"
if [ -z "$COMPAT" ] && [ -f /proc/device-tree/compatible ]; then
    COMPAT=$(tr '\0' ' ' < /proc/device-tree/compatible 2>/dev/null || true)
fi
case "$COMPAT" in
    *plum*) IS_PLUM=1; TARGET_EXTVOL=0 ;;
    *) IS_PLUM=0; TARGET_EXTVOL=1 ;;
esac

NEED_YIELD=0
NEED_EXTVOL=0
FIX_PLUM_EXTVOL=0
NEED_DUCK=0
NEED_ALSA_PLUGINS=0
FIX_CARD_NAME=0

LEGACY_CARD='CARD_NAME=$(cat /proc/asound/cards 2>/dev/null | awk '\''/\[/{print $2; exit}'\'' || echo "default")'

grep -q 'APULSE_YIELD_PATH=' "$START" 2>/dev/null || NEED_YIELD=1
if ! grep -q 'APULSE_EXTERNAL_VOLUME=' "$START" 2>/dev/null; then
    NEED_EXTVOL=1
elif [ "$IS_PLUM" = 1 ] && grep -Fxq 'export APULSE_EXTERNAL_VOLUME=1' "$START" 2>/dev/null; then
    FIX_PLUM_EXTVOL=1
fi
grep -q 'libsoloist_duck\.so' "$START" 2>/dev/null || NEED_DUCK=1
grep -q 'ALSA_PLUGIN_DIR=' "$START" 2>/dev/null || NEED_ALSA_PLUGINS=1
if grep -Fxq "$LEGACY_CARD" "$START" 2>/dev/null; then
    FIX_CARD_NAME=1
fi

HAS_NOHUP=0
if grep -Eq '^nohup[[:space:]].*/bin/soloist([[:space:]]|$)' "$START" 2>/dev/null; then
    HAS_NOHUP=1
fi

if [ "$HAS_NOHUP" != 1 ]; then
    NEED_YIELD=0
    NEED_EXTVOL=0
    NEED_DUCK=0
    NEED_ALSA_PLUGINS=0
fi

if [ "$NEED_YIELD" = 0 ] && [ "$NEED_EXTVOL" = 0 ] && [ "$FIX_PLUM_EXTVOL" = 0 ] &&
   [ "$NEED_DUCK" = 0 ] && [ "$NEED_ALSA_PLUGINS" = 0 ] && [ "$FIX_CARD_NAME" = 0 ]; then
    exit 0
fi

umask 077
TMP_START=$(mktemp "$INSTALL_DIR/.start.sh.XXXXXX") || exit 1
trap 'rm -f "$TMP_START"' EXIT

SOLOIST_INSTALL_DIR="$INSTALL_DIR" \
SOLOIST_LEGACY_CARD="$LEGACY_CARD" \
awk \
    -v need_yield="$NEED_YIELD" \
    -v need_extvol="$NEED_EXTVOL" \
    -v target_extvol="$TARGET_EXTVOL" \
    -v fix_plum_extvol="$FIX_PLUM_EXTVOL" \
    -v need_duck="$NEED_DUCK" \
    -v need_alsa_plugins="$NEED_ALSA_PLUGINS" \
    -v fix_card="$FIX_CARD_NAME" '
{
    lines[NR] = $0
}
END {
    dir = ENVIRON["SOLOIST_INSTALL_DIR"]
    legacy_card = ENVIRON["SOLOIST_LEGACY_CARD"]

    nohup_i = 0
    for (i = 1; i <= NR; i++) {
        if (lines[i] ~ /^nohup[[:space:]].*\/bin\/soloist([[:space:]]|$)/) {
            nohup_i = i
            break
        }
    }

    block_start = nohup_i
    while (block_start > 1 && lines[block_start - 1] ~ /\\$/) {
        block_start--
    }

    for (i = 1; i <= NR; i++) {
        if (nohup_i > 0 && i == block_start) {
            if (need_extvol) print "export APULSE_EXTERNAL_VOLUME=" target_extvol
            if (need_yield) print "export APULSE_YIELD_PATH=/tmp/soloist.yield"
            if (need_alsa_plugins) print "export ALSA_PLUGIN_DIR=\"" dir "/lib64/alsa-lib\""
            if (need_duck) {
                print "DUCK_PRELOAD=\"\""
                print "[ -f \"" dir "/lib64/libsoloist_duck.so\" ] && DUCK_PRELOAD=\"" dir "/lib64/libsoloist_duck.so\""
            }
        }
        if (nohup_i > 0 && i == nohup_i && need_duck) {
            print "LD_PRELOAD=\"$DUCK_PRELOAD\" \\"
        }
        if (fix_card && lines[i] == legacy_card) {
            print "CARD_NAME=$(awk -F\047[][]\047 \047/\\[/{gsub(/[[:space:]]/, \"\", $2); if ($2 != \"\") {print $2; exit}}\047 /proc/asound/cards 2>/dev/null || true)"
            print "[ -n \"$CARD_NAME\" ] || CARD_NAME=\"default\""
            continue
        }
        if (fix_plum_extvol && lines[i] == "export APULSE_EXTERNAL_VOLUME=1") {
            print "export APULSE_EXTERNAL_VOLUME=0"
            continue
        }
        print lines[i]
    }
}
' "$START" > "$TMP_START"

sh -n "$TMP_START"
chmod 700 "$TMP_START"
mv -f "$TMP_START" "$START"
