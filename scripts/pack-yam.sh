#!/bin/sh
# Stage payload (resolving symlinks and permissions) and build YAM v2 .yampkg archive(s).
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TARGET="${1:-yandexmicro}"
case "${1:-}" in
    ""|-*) TARGET="yandexmicro" ;;
    *) shift ;;
esac

YAM_PUBLISH_BIN="${YAM_PUBLISH:-$(command -v yam-publish 2>/dev/null || true)}"
[ -n "$YAM_PUBLISH_BIN" ] || [ ! -x /tmp/yam_src/target/release/yam-publish ] || YAM_PUBLISH_BIN="/tmp/yam_src/target/release/yam-publish"

PROFILES_DIR="${YAM_PROFILES:-}"
if [ -z "$PROFILES_DIR" ]; then
    if [ -d /tmp/yam_src/platforms ]; then
        PROFILES_DIR="/tmp/yam_src/platforms"
    elif [ -d "$REPO_DIR/platforms" ]; then
        PROFILES_DIR="$REPO_DIR/platforms"
    fi
fi

OUT_DIR="${YAM_OUT_DIR:-$REPO_DIR/dist}"

while [ $# -gt 0 ]; do
    case "$1" in
        --yam-publish) YAM_PUBLISH_BIN="$2"; shift 2 ;;
        --profiles)    PROFILES_DIR="$2"; shift 2 ;;
        --out-dir)     OUT_DIR="$2"; shift 2 ;;
        *)
            echo "Unknown option: $1" >&2
            exit 2
            ;;
    esac
done

if [ -z "$YAM_PUBLISH_BIN" ] || [ ! -x "$YAM_PUBLISH_BIN" ]; then
    echo "ERROR: yam-publish binary not found. Set YAM_PUBLISH=/path/to/yam-publish" >&2
    exit 1
fi
if [ -z "$PROFILES_DIR" ] || [ ! -d "$PROFILES_DIR" ]; then
    echo "ERROR: YAM platform profiles directory not found. Set YAM_PROFILES=/path/to/platforms" >&2
    exit 1
fi

VERSION="$(awk -F: '/^version:[[:space:]]*/ { gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2); print $2; exit }' "$REPO_DIR/yam.yaml")"
[ -n "$VERSION" ] || { echo "ERROR: Could not parse version from yam.yaml" >&2; exit 1; }

# Ensure native cross-compiled components are up to date
if [ ! -x "$REPO_DIR/bin/soloist-bridge" ] || [ ! -f "$REPO_DIR/lib64/libsoloist_duck.so" ]; then
    make -C "$REPO_DIR" all
fi

STAGE_DIR="$(mktemp -d /tmp/soloist-yam-stage-XXXXXX)"
trap 'rm -rf "$STAGE_DIR"' EXIT

# Base binaries/runtime libraries (`soloist`, `curl`, glibc/alsa libs) live in soloist-station-lite.tar.gz
if [ -f "$REPO_DIR/soloist-station-lite.tar.gz" ]; then
    tar -xzf "$REPO_DIR/soloist-station-lite.tar.gz" -C "$STAGE_DIR" ./bin ./lib64 2>/dev/null || \
        tar -xzf "$REPO_DIR/soloist-station-lite.tar.gz" -C "$STAGE_DIR" bin lib64 2>/dev/null || true
fi

mkdir -p "$STAGE_DIR/bin" "$STAGE_DIR/scripts" "$STAGE_DIR/lib64/alsa-lib"

# Overlay freshly built binaries, libraries (dereferencing symlinks cleanly), scripts, and metadata
for f in soloist soloist-bridge curl; do
    if [ -f "$REPO_DIR/bin/$f" ]; then
        cp -fL "$REPO_DIR/bin/$f" "$STAGE_DIR/bin/$f"
    fi
done

if [ ! -x "$STAGE_DIR/bin/soloist" ]; then
    echo "ERROR: bin/soloist missing (provide bin/soloist or soloist-station-lite.tar.gz)" >&2
    exit 1
fi

if [ -d "$REPO_DIR/lib64" ]; then
    cp -RfL "$REPO_DIR/lib64/." "$STAGE_DIR/lib64/"
fi

# Ensure zero symlinks remain inside staging tree (yam-publish pack rejects symlinks)
find "$STAGE_DIR" -type l | while IFS= read -r link; do
    target_file="$(readlink -f "$link" 2>/dev/null || true)"
    rm -f "$link"
    [ -n "$target_file" ] && [ -f "$target_file" ] && cp -f "$target_file" "$link"
done

cp -f "$REPO_DIR/scripts/"* "$STAGE_DIR/scripts/"
cp -f "$REPO_DIR/config.env.example" "$STAGE_DIR/config.env.example"
cp -f "$REPO_DIR/spotify_api.conf.example" "$STAGE_DIR/spotify_api.conf.example"
cp -f "$REPO_DIR/yam.yaml" "$STAGE_DIR/yam.yaml"
printf 'v%s\n' "$VERSION" > "$STAGE_DIR/version"

# YAM v2 preserves execute bits from staged payload into .yampkg
chmod 755 "$STAGE_DIR/bin/"* "$STAGE_DIR/scripts/"*.sh "$STAGE_DIR/scripts/soloist-cli"
find "$STAGE_DIR/lib64" -type f -exec chmod 755 {} +
chmod 644 "$STAGE_DIR/config.env.example" "$STAGE_DIR/spotify_api.conf.example" "$STAGE_DIR/yam.yaml" "$STAGE_DIR/version"

mkdir -p "$OUT_DIR"

AARCH64_TARGETS="yandexmicro plum yandexplum yandexmini yandexmini_2 bergamot pickle cucumber yandexmidi yandexstation yandexstation_2 orion"
if [ "$TARGET" = "all" ]; then
    TARGETS_TO_BUILD="$AARCH64_TARGETS"
else
    TARGETS_TO_BUILD="$TARGET"
fi

for t in $TARGETS_TO_BUILD; do
    OUT_PKG="$OUT_DIR/horohi-soloist-${VERSION}-${t}.yampkg"
    echo "[pack-yam] Building $OUT_PKG..."
    "$YAM_PUBLISH_BIN" pack \
        --source "$STAGE_DIR" \
        --platform "$t" \
        --profiles "$PROFILES_DIR" \
        --output "$OUT_PKG"
done

echo "[pack-yam] Done."
