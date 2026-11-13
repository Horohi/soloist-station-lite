#!/bin/sh
export PATH="/bin:/usr/bin:/sbin:/usr/sbin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD

# Kill the PID from a pid-file only if it still belongs to the expected process
# (pid-files live in /data and survive reboots, so the PID may have been reused).
kill_pidfile() {
    pidfile="$1"; expected="$2"
    [ -f "$pidfile" ] || return 0
    pid=$(cat "$pidfile" 2>/dev/null || true)
    if [ -n "$pid" ]; then
        comm=$(cat "/proc/$pid/comm" 2>/dev/null || true)
        if [ "$comm" = "$expected" ]; then
            kill -9 "$pid" 2>/dev/null || true
        elif [ "$expected" = "soloist" ] && [ "$comm" = "ld-linux-aarch6" ]; then
            case "$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)" in
                *soloist*) kill -9 "$pid" 2>/dev/null || true ;;
            esac
        fi
    fi
    rm -f "$pidfile"
}

YAM_DATA="${YAM_VAR:-${YAM_ROOT:-/data/yam}/var}/horohi.soloist"
YAM_CACHE="${YAM_RUN:-/tmp/yam}/soloist_cache"

for pidfile in \
    "$YAM_DATA/ram_guard.pid" \
    "$YAM_CACHE/ram_guard.pid" \
    /data/opt/soloist/data/ram_guard.pid; do
    if [ -f "$pidfile" ]; then
        rpid=$(cat "$pidfile" 2>/dev/null || true)
        case "$rpid" in
            ''|*[!0-9]*) ;;
            *)
                # The PID file survives reboot. A recycled PID must not kill
                # an unrelated process (or a whole process group, PID 0).
                rcomm=$(cat "/proc/$rpid/comm" 2>/dev/null || true)
                case "$rcomm" in
                    sh|ash|bash|busybox)
                        rcmd=$(tr '\0' ' ' < "/proc/$rpid/cmdline" 2>/dev/null || true)
                        case "$rcmd" in
                            *soloist-ram-guard*|*'MEM_AVAIL='*'/soloist.pid'*)
                                [ "$rpid" -gt 1 ] && kill -9 "$rpid" 2>/dev/null || true ;;
                        esac
                        ;;
                esac
                ;;
        esac
        rm -f "$pidfile"
    fi
done

for pidfile in "$YAM_DATA/bridge.pid" "$YAM_CACHE/bridge.pid" /data/opt/soloist/data/bridge.pid; do
    kill_pidfile "$pidfile" soloist-bridge
done
killall -9 soloist-bridge 2>/dev/null || true

for pidfile in "$YAM_DATA/soloist.pid" "$YAM_CACHE/soloist.pid" /data/opt/soloist/data/soloist.pid; do
    kill_pidfile "$pidfile" soloist
done

# Kill only actual bin/soloist daemon/crashpad binaries; never kill CLI scripts named 'soloist'
for proc_dir in /proc/[0-9]*; do
    p="${proc_dir#/proc/}"
    [ "$p" != "$$" ] && [ "$p" != "${PPID:-0}" ] || continue
    exe=$(readlink "$proc_dir/exe" 2>/dev/null || true)
    case "$exe" in
        */busybox|*/sh|*/ash|*/dash|*/bash) continue ;;
        */bin/soloist|*/bin/soloist\ *)
            kill -9 "$p" 2>/dev/null || true
            ;;
        */ld-linux-aarch64.so.1*)
            case "$(tr '\0' ' ' < "$proc_dir/cmdline" 2>/dev/null || true)" in
                */bin/soloist*) kill -9 "$p" 2>/dev/null || true ;;
            esac
            ;;
    esac
done

rm -f /tmp/soloist.yield 2>/dev/null || true
# Older standalone start.sh files embed an untracked guard. Migrate known
# generated blocks after the helper has been installed, including on update.
if [ -f /data/opt/soloist/scripts/migrate-standalone-guard.sh ]; then
    sh /data/opt/soloist/scripts/migrate-standalone-guard.sh /data/opt/soloist || true
fi
echo "Spotify Soloist and Bridge stopped."
