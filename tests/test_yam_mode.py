#!/usr/bin/env python3
"""Verify YAM v2 path resolution across soloist-bridge C code and shell scripts."""
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/soloist-bridge.c").read_text()

part_paths = SOURCE[
    SOURCE.index("static int is_yam_mode = 0;") :
    SOURCE.index("/* Text and command classification helpers */")
]
part_mem = SOURCE[
    SOURCE.index("static int mem_equal") :
    SOURCE.index("/* Word-boundary helpers.")
]
part_init = SOURCE[
    SOURCE.index("static void join_path(") :
    SOURCE.index("void _start(void)")
]

C_PREFIX = r"""
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long sys_clone(unsigned long f, void *s) { (void)f; (void)s; return fork(); }
static long sys_wait4(int p, int *s, int o, void *r) { (void)r; long n = waitpid(p, s, o); return n < 0 ? -errno : n; }
static int sys_execve(const char *p, const char *const a[], const char *const e[]) { return execve(p, (char *const *)a, (char *const *)e); }
static void sys_exit(int n) { _exit(n); }
static int sys_open(const char *p, int f, int m) { return open(p, f, m); }
static long sys_read(int f, void *b, unsigned long n) { long r = read(f, b, n); return r < 0 ? -errno : r; }
static long sys_write(int f, const void *b, unsigned long n) { return write(f, b, n); }
static int sys_close(int f) { return close(f); }
static int sys_rename(const char *a, const char *b) { return rename(a, b); }
static int sys_mkdir(const char *p, int m) { return mkdir(p, m); }
static int sys_signal_default(int s) { return signal(s, SIG_DFL) == SIG_ERR ? -1 : 0; }
static int sys_pipe(int f[2]) { if (pipe(f) < 0) return -1; fcntl(f[0], F_SETFL, O_NONBLOCK); return 0; }
static int sys_dup3(int from, int to) { return dup2(from, to); }
static long sys_readlinkat(const char *path, char *buf, unsigned long sz) { return readlink(path, buf, sz); }
static unsigned long str_len(const char *s) { return s ? strlen(s) : 0; }
static int str_equal(const char *a, const char *b) { return !strcmp(a, b); }
static int str_contains(const char *h, const char *n) { return (h && n && strstr(h, n)) ? 1 : 0; }
static void log_msg(const char *s) { fputs(s, stderr); }
static int64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000 + t.tv_nsec / 1000000; }
"""

C_DRIVER = r"""
int main(void) {
    init_runtime_paths();
    printf("MODE=%d\n", is_yam_mode);
    printf("CTRL=%s\n", get_controller_sh());
    printf("CENSOR=%s\n", get_censor_path());
    printf("REQ=%s\n", get_requests_dir());
    printf("CONF=%s\n", get_conf_path());
    printf("LED_STATE=%s\n", get_led_state_path());
    printf("LED_REQ=%s\n", get_led_req_path());
    printf("TRACK=%s\n", g_track_path);
    run_controller("shuffle", "on");
    struct timespec tick = {0, 20000000};
    for (int i = 0; i < 40; i++) {
        service_controllers();
        nanosleep(&tick, 0);
    }
    return 0;
}
"""


def test_c_yam_paths():
    with tempfile.TemporaryDirectory(prefix="soloist-yam-c-") as d:
        tmp = pathlib.Path(d)
        yam_root = tmp / "yam"
        pkg_dir = yam_root / "pkgs/horohi/soloist/1.3.6"
        pkg_share = pkg_dir / "share/horohi.soloist"
        etc_dir = yam_root / "etc"
        var_dir = yam_root / "var"
        run_dir = tmp / "run"

        (pkg_share / "scripts").mkdir(parents=True)
        etc_dir.mkdir(parents=True)
        var_dir.mkdir(parents=True)
        run_dir.mkdir(parents=True)

        env_capture = tmp / "captured_env"
        (pkg_share / "scripts/control.sh").write_text(
            f"""#!/bin/sh
printf '%s\\n' \
  "YAM_ROOT=${{YAM_ROOT-}}" \
  "YAM_PKG=${{YAM_PKG-}}" \
  "YAM_ETC=${{YAM_ETC-}}" \
  "YAM_VAR=${{YAM_VAR-}}" \
  "YAM_RUN=${{YAM_RUN-}}" \
  "LD_LIBRARY_PATH=${{LD_LIBRARY_PATH-unset}}" \
  "LD_PRELOAD=${{LD_PRELOAD-unset}}" > "{env_capture}"
[ "${{SOLOIST_BRIDGE_ADMISSION:-}}" != 1 ] || printf A >&3
"""
        )

        # Strip unused helper stub lines beyond init_runtime_paths if present
        c_src = C_PREFIX + part_paths + part_mem + part_init + C_DRIVER
        (tmp / "test_yam.c").write_text(c_src)
        exe = tmp / "test_yam"
        subprocess.run(
            ["cc", "-std=c99", "-Wno-unused-function", str(tmp / "test_yam.c"), "-o", str(exe)],
            check=True,
        )

        env = {
            "PATH": os.environ.get("PATH", "/bin:/usr/bin"),
            "YAM_ROOT": str(yam_root),
            "YAM_PKG": str(pkg_dir),
            "YAM_ETC": str(etc_dir),
            "YAM_VAR": str(var_dir),
            "YAM_RUN": str(run_dir),
            "LD_LIBRARY_PATH": "/evil/lib64",
            "LD_PRELOAD": "/evil/duck.so",
        }
        res = subprocess.run([str(exe)], env=env, capture_output=True, text=True, check=True)
        lines = dict(line.split("=", 1) for line in res.stdout.strip().splitlines() if "=" in line)

        assert lines["MODE"] == "1", lines
        assert lines["CTRL"] == str(pkg_share / "scripts/control.sh"), lines
        assert lines["CENSOR"] == str(var_dir / "horohi.soloist/censorship"), lines
        assert lines["REQ"] == str(var_dir / "horohi.soloist/requests"), lines
        assert lines["CONF"] == str(etc_dir / "horohi.soloist/spotify_api.conf"), lines
        assert lines["LED_STATE"] == str(var_dir / "horohi.soloist/requests/led_state"), lines
        assert lines["LED_REQ"] == str(var_dir / "horohi.soloist/requests/led"), lines
        assert lines["TRACK"] == str(var_dir / "horohi.soloist/requests/track"), lines
        assert (var_dir / "horohi.soloist/requests").is_dir()

        captured = env_capture.read_text()
        assert f"YAM_PKG={pkg_dir}" in captured, captured
        assert f"YAM_VAR={var_dir}" in captured, captured
        assert f"YAM_ETC={etc_dir}" in captured, captured
        assert "LD_LIBRARY_PATH=unset" in captured, captured
        assert "LD_PRELOAD=unset" in captured, captured
        print("PASS C bridge YAM v2 /proc/self/environ path & child envp tests")


def test_shell_yam_layout():
    with tempfile.TemporaryDirectory(prefix="soloist-yam-sh-") as d:
        tmp = pathlib.Path(d)
        yam_root = tmp / "yam"
        pkg_dir = yam_root / "pkgs/horohi/soloist/1.3.6"
        pkg_share = pkg_dir / "share/horohi.soloist"
        (pkg_dir / "bin").mkdir(parents=True)
        (pkg_share / "bin").mkdir(parents=True)
        (pkg_share / "scripts").mkdir(parents=True)
        (pkg_share / "lib64").mkdir(parents=True)
        (yam_root / "bin").mkdir(parents=True)
        (yam_root / "share").mkdir(parents=True)

        for name in (
            "control.sh",
            "request_state.sh",
            "search_and_play.sh",
            "soloist-cli",
            "yam-postinstall.sh",
        ):
            dst = pkg_share / "scripts" / name
            dst.write_text((ROOT / "scripts" / name).read_text())
            dst.chmod(0o755)

        (pkg_share / "config.env.example").write_text((ROOT / "config.env.example").read_text())
        (pkg_share / "spotify_api.conf.example").write_text((ROOT / "spotify_api.conf.example").read_text())
        (pkg_share / "version").write_text("v1.3.6\n")

        # Mock soloist & curl inside pkg_share/bin
        mock_soloist = pkg_share / "bin/soloist"
        mock_soloist.write_text('#!/bin/sh\necho "$@" >> "${0%/bin/soloist}/calls"\n')
        mock_soloist.chmod(0o755)

        # Install YAM-style symlinks: bin/soloist -> cli, share/horohi.soloist -> pkg_share
        (pkg_dir / "bin/soloist").write_text((ROOT / "scripts/soloist-cli").read_text())
        (pkg_dir / "bin/soloist").chmod(0o755)
        (pkg_dir / "bin/soloist-postinstall").write_text((ROOT / "scripts/yam-postinstall.sh").read_text())
        (pkg_dir / "bin/soloist-postinstall").chmod(0o755)
        (yam_root / "bin/soloist").symlink_to(pkg_dir / "bin/soloist")
        (yam_root / "share/horohi.soloist").symlink_to(pkg_share)

        env = {
            "PATH": "/bin:/usr/bin",
            "YAM_ROOT": str(yam_root),
            "YAM_PKG": str(pkg_dir),
            "YAM_ETC": "/data/yam/etc",  # intentionally mimics stock platform YAML during --root sandbox
            "YAM_VAR": "/data/yam/var",
            "YAM_RUN": str(tmp / "run"),
        }
        # 1. Sandbox postinstall must honor YAM_ROOT instead of writing /data/yam
        subprocess.run([str(pkg_dir / "bin/soloist-postinstall")], env=env, check=True)
        assert (yam_root / "etc/horohi.soloist/config.env").is_file()
        assert (yam_root / "etc/horohi.soloist/spotify_api.conf.example").is_file()
        assert (yam_root / "var/horohi.soloist/requests").is_dir()

        # 2. Invoking symlinked $YAM_ROOT/bin/soloist without YAM_* env writes to $YAM_ROOT/var/horohi.soloist/requests/led
        clean_env = {"PATH": "/bin:/usr/bin"}
        subprocess.run([str(yam_root / "bin/soloist"), "led", "on"], env=clean_env, check=True)
        assert (yam_root / "var/horohi.soloist/requests/led").read_text() == "1\n"

        # 3. control.sh writes generation & lock into $YAM_ROOT/var/horohi.soloist/requests
        subprocess.run(
            ["/bin/sh", str(yam_root / "share/horohi.soloist/scripts/control.sh"), "shuffle", "on"],
            env=clean_env,
            check=True,
        )
        assert (yam_root / "var/horohi.soloist/requests/control.lock").exists()

        # 4. search_and_play.sh reads favorites.uri from $YAM_ROOT/var/horohi.soloist and spotify_api.conf from $YAM_ROOT/etc/horohi.soloist
        (yam_root / "var/horohi.soloist/favorites.uri").write_text("spotify:playlist:37i9dQZF1DXcBWIGoYBM5M\n")
        subprocess.run(
            ["/bin/sh", str(yam_root / "share/horohi.soloist/scripts/search_and_play.sh"), "включи любимые треки"],
            env=clean_env,
            check=True,
        )
        calls = (pkg_share / "calls").read_text()
        assert "play spotify:playlist:37i9dQZF1DXcBWIGoYBM5M" in calls, calls
        print("PASS shell YAM v2 sandbox postinstall, CLI symlink, control.sh, and favorites.uri tests")


if __name__ == "__main__":
    test_c_yam_paths()
    test_shell_yam_layout()
    print("ALL YAM V2 MODE TESTS PASSED!")
