#!/usr/bin/env python3
"""Verify YAM v2 path resolution across soloist-bridge C code and shell scripts."""
import os
import pathlib
import subprocess
import tarfile
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
    SOURCE.index("static void join_path(char *dst, unsigned long max, const char *a, const char *b) {") :
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


def test_soloist_setup():
    with tempfile.TemporaryDirectory(prefix="soloist-setup-test-") as d:
        tmp = pathlib.Path(d)
        yam_root = tmp / "yam"
        pkg_dir = yam_root / "pkgs/horohi/soloist/1.3.7"
        pkg_share = pkg_dir / "share/horohi.soloist"
        (pkg_dir / "bin").mkdir(parents=True)
        (pkg_share / "bin").mkdir(parents=True)
        (pkg_share / "scripts").mkdir(parents=True)
        (pkg_share / "lib64").mkdir(parents=True)
        (yam_root / "bin").mkdir(parents=True)
        (yam_root / "share").mkdir(parents=True)
        (yam_root / "etc/horohi.soloist").mkdir(parents=True)
        (yam_root / "var/horohi.soloist/requests").mkdir(parents=True)

        (pkg_share / "bin/soloist").write_text("#!/bin/sh\nexit 0\n")
        (pkg_share / "bin/soloist").chmod(0o755)
        (pkg_dir / "bin/soloist").write_text((ROOT / "scripts/soloist-cli").read_text())
        (pkg_dir / "bin/soloist").chmod(0o755)
        (pkg_share / "scripts/soloist-cli").write_text((ROOT / "scripts/soloist-cli").read_text())
        (pkg_share / "scripts/soloist-cli").chmod(0o755)
        (yam_root / "bin/soloist").symlink_to(pkg_dir / "bin/soloist")
        (yam_root / "share/horohi.soloist").symlink_to(pkg_share)

        hosts_file = tmp / "hosts"
        hosts_file.write_text("127.0.0.1 localhost\n")

        setup_env = {
            "PATH": "/bin:/usr/bin",
            "SOLOIST_HOSTS_FILE": str(hosts_file),
            "SOLOIST_SETUP_NO_RESTART": "1",
        }

        # 1. EOF with no configured key must exit non-zero cleanly without hanging
        res_eof = subprocess.run(
            [str(yam_root / "bin/soloist"), "setup"],
            env=setup_env,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=5,
        )
        assert res_eof.returncode != 0, res_eof.stdout + res_eof.stderr

        # 2. First interactive setup: invalid key rejected -> valid key accepted, Spotify URL normalized, hosts updated
        wizard_input = "\n".join([
            "bad key!",
            "spak_test123456",
            "Гостиная Станция",
            "cid_abc123",
            "sec_xyz789",
            "https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M?si=demo",
            "y",
            "",
        ])
        res = subprocess.run(
            [str(yam_root / "bin/soloist"), "setup"],
            env=setup_env,
            input=wizard_input,
            capture_output=True,
            text=True,
            check=True,
        )
        assert "Недопустимые символы" in res.stdout, res.stdout
        cfg = (yam_root / "etc/horohi.soloist/config.env").read_text()
        assert cfg == 'API_KEY="spak_test123456"\nDEVICE_NAME="Гостиная Станция"\n', cfg
        assert ((yam_root / "etc/horohi.soloist/config.env").stat().st_mode & 0o777) == 0o600
        api_cfg = (yam_root / "etc/horohi.soloist/spotify_api.conf").read_text()
        assert api_cfg == "SPOTIFY_CLIENT_ID='cid_abc123'\nSPOTIFY_CLIENT_SECRET='sec_xyz789'\n", api_cfg
        assert ((yam_root / "etc/horohi.soloist/spotify_api.conf").stat().st_mode & 0o777) == 0o600
        assert (yam_root / "var/horohi.soloist/favorites.uri").read_text() == "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M\n"
        hosts_txt = hosts_file.read_text()
        assert "5.175.161.40 accounts.spotify.com" in hosts_txt, hosts_txt

        # 3. Re-run keeping existing secrets via Enter, and disabling SNI proxy
        rerun_input = "\n".join(["", "", "", "", "n", ""])
        subprocess.run(
            [str(yam_root / "bin/soloist"), "setup"],
            env=setup_env,
            input=rerun_input,
            capture_output=True,
            text=True,
            check=True,
        )
        assert (yam_root / "etc/horohi.soloist/config.env").read_text() == cfg
        assert (yam_root / "etc/horohi.soloist/spotify_api.conf").read_text() == api_cfg
        assert (yam_root / "var/horohi.soloist/favorites.uri").read_text() == "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M\n"
        hosts_after = hosts_file.read_text()
        assert "5.175.161.40" not in hosts_after and "127.0.0.1 localhost" in hosts_after, hosts_after
        assert (hosts_file.stat().st_mode & 0o777) == 0o644

        # 4. Standalone /data/opt/soloist start.sh update mode
        sa_dir = tmp / "standalone"
        (sa_dir / "bin").mkdir(parents=True)
        (sa_dir / "scripts").mkdir(parents=True)
        (sa_dir / "data/requests").mkdir(parents=True)
        (sa_dir / "bin/soloist").write_text("#!/bin/sh\nexit 0\n")
        (sa_dir / "scripts/soloist-cli").write_text((ROOT / "scripts/soloist-cli").read_text())
        (sa_dir / "scripts/soloist-cli").chmod(0o755)
        (sa_dir / "start.sh").write_text(
            '#!/bin/sh\nAPI_KEY="spak_oldkey"\nnohup /data/opt/soloist/bin/soloist \\\n    -n "Old Model" \\\n    -k "$API_KEY" &\n'
        )
        (sa_dir / "start.sh").chmod(0o755)
        sa_env = dict(setup_env)
        sa_env["INSTALL_DIR"] = str(sa_dir)
        subprocess.run(
            [str(sa_dir / "scripts/soloist-cli"), "setup"],
            env=sa_env,
            input="spak_newkey\nNew Station\n\n\nn\n",
            capture_output=True,
            text=True,
            check=True,
        )
        new_start = (sa_dir / "start.sh").read_text()
        assert 'API_KEY="spak_newkey"' in new_start, new_start
        assert '-n "New Station" \\' in new_start, new_start
        assert '-k "$API_KEY"' in new_start, new_start
        print("PASS soloist setup interactive wizard (YAM v2 + Standalone + EOF + preserve)")


def test_auto_repeat_and_stale_pid_status():
    # 1. Verify C repeat_mode parsing from WS JSON options.repeat
    with tempfile.TemporaryDirectory(prefix="soloist-repeat-test-") as d:
        tmp = pathlib.Path(d)
        # Filter out run_controller/selection declarations if already in part_paths
        c_file = tmp / "test_rep.c"
        exe = tmp / "test_rep"
        # part_paths defines selection/run_controller helpers, extract through track_observation_revision or keep clean slice
        ws_start = SOURCE.index("static int spotify_is_active = 0;")
        ws_slice = SOURCE[ws_start : SOURCE.index("#ifdef __aarch64__", ws_start)]
        stub_head = C_PREFIX + part_mem + r"""
static int is_yam_mode = 0;
static int spotify_is_active_dummy = 0;
static const char *get_requests_dir(void) { return "/tmp"; }
static void run_controller(const char *a, const char *b) { (void)a; (void)b; }
static int selection_needs_observation(void) { return 0; }
static void publish_track(const char *u) { (void)u; }
""" + ws_slice + r"""
int main(void) {
    handle_soloist_ws_json("{\"type\":\"playback_state\",\"options\":{\"shuffle\":false,\"repeat\":\"track\"}}");
    if (repeat_mode != 2) return 1;
    handle_soloist_ws_json("{\"type\":\"playback_state\",\"options\":{\"shuffle\":false,\"repeat\":\"context\"}}");
    if (repeat_mode != 1) return 2;
    handle_soloist_ws_json("{\"type\":\"playback_state\",\"options\":{\"shuffle\":false,\"repeat\":\"off\"}}");
    if (repeat_mode != 0) return 3;
    printf("PASS C bridge options.repeat parser (track/context/off)\n");
    return 0;
}
"""
        c_file.write_text(stub_head)
        subprocess.run(["cc", "-std=c99", "-Wno-unused-function", str(c_file), "-o", str(exe)], check=True)
        res = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
        print(res.stdout, end="")

        # 2. Verify control.sh auto-repeat dispatches 'seek 0' instead of 'next'
        app = tmp / "app"
        (app / "bin").mkdir(parents=True)
        (app / "scripts").mkdir(parents=True)
        (app / "data/requests").mkdir(parents=True)
        (app / "bin/soloist").write_text('#!/bin/sh\necho "$@" >> "' + str(tmp / "ctl_calls") + '"\n')
        (app / "bin/soloist").chmod(0o755)
        for s in ("control.sh", "request_state.sh", "soloist-cli"):
            p = app / "scripts" / s
            p.write_text((ROOT / "scripts" / s).read_text())
            p.chmod(0o755)
        (app / "data/requests/track").write_text("spotify:track:1111111111111111111111\n1\n")
        (app / "data/requests/generation").write_text("0\n")
        subprocess.run(
            ["/bin/sh", str(app / "scripts/control.sh"), "auto-repeat", "spotify:track:1111111111111111111111", "0"],
            check=True,
        )
        ctl_out = (tmp / "ctl_calls").read_text()
        assert "seek 0" in ctl_out and " next" not in ctl_out, ctl_out
        print("PASS control.sh auto-repeat dispatches seek 0 instead of next")

        # 3. Verify soloist status ignores stale PID file pointing to unrelated alive process (this python process)
        (app / "data/soloist.pid").write_text(f"{os.getpid()}\n")
        (app / "data/bridge.pid").write_text(f"{os.getpid()}\n")
        st_env = {"PATH": "/bin:/usr/bin", "INSTALL_DIR": str(app)}
        st_res = subprocess.run([str(app / "scripts/soloist-cli"), "status"], env=st_env, capture_output=True, text=True, check=True)
        assert "Soloist Daemon: " in st_res.stdout and "STOPPED" in st_res.stdout and "RUNNING" not in st_res.stdout, st_res.stdout
        print("PASS soloist status does not falsely report reused/stale PID as RUNNING")

        # 4. Verify stop.sh does not kill a running shell process named 'soloist' (e.g. soloist update/restart)
        sh_copy = tmp / "soloist"
        sh_copy.symlink_to("/bin/sh")
        cli_proc = subprocess.Popen([str(sh_copy), "-c", "sleep 5"])
        try:
            subprocess.run(["/bin/sh", str(ROOT / "scripts/stop.sh")], env=st_env, check=True, capture_output=True)
            assert cli_proc.poll() is None, "stop.sh killed unrelated shell CLI process"
        finally:
            cli_proc.terminate()
            cli_proc.wait(timeout=5)
        print("PASS stop.sh does not kill soloist CLI shell processes")


def test_ram_guard_pid_reuse():
    with tempfile.TemporaryDirectory(prefix="soloist-guard-") as d:
        tmp = pathlib.Path(d)
        state = tmp / "var/horohi.soloist"
        state.mkdir(parents=True)
        # Isolate this test from the host's unrelated Soloist processes and files.
        script = (ROOT / "scripts/stop.sh").read_text()
        script = script.replace("killall -9 soloist-bridge", "true")
        script = script.replace("/proc/[0-9]*", str(tmp / "no-proc/[0-9]*"))
        script = script.replace("/data/opt/soloist", str(tmp / "standalone"))
        script = script.replace("/tmp/soloist.yield", str(tmp / "soloist.yield"))
        stop = tmp / "stop.sh"
        stop.write_text(script)
        env = {"PATH": "/bin:/usr/bin", "YAM_VAR": str(tmp / "var"), "YAM_RUN": str(tmp / "run")}
        unrelated = subprocess.Popen(["sleep", "30"])
        try:
            (state / "ram_guard.pid").write_text(f"{unrelated.pid}\n")
            subprocess.run(["sh", str(stop)], env=env, check=True, capture_output=True)
            assert unrelated.poll() is None, "reused RAM guard PID killed an unrelated process"
        finally:
            unrelated.terminate()
            unrelated.wait(timeout=5)

        guard = subprocess.Popen(["sh", "-c", "while :; do sleep 1; done", "soloist-ram-guard"])
        try:
            (state / "ram_guard.pid").write_text(f"{guard.pid}\n")
            subprocess.run(["sh", str(stop)], env=env, check=True, capture_output=True)
            guard.wait(timeout=5)
            assert guard.returncode != 0, "actual RAM guard was not stopped"
        finally:
            if guard.poll() is None:
                guard.terminate()
                guard.wait(timeout=5)
        print("PASS stop.sh checks RAM guard identity before killing a PID")


def test_standalone_update_download_failures():
    with tempfile.TemporaryDirectory(prefix="soloist-update-") as d:
        tmp = pathlib.Path(d)
        app = tmp / "app"
        for directory in ("bin", "scripts", "lib64"):
            (app / directory).mkdir(parents=True)
        cli_text = (ROOT / "scripts/soloist-cli").read_text()
        cli_text = cli_text.replace("/data/opt/bin", str(tmp / "opt-bin"))
        cli_text = cli_text.replace("/tmp/soloist_update* /data/local/tmp/soloist*", str(tmp / "update-debris-*"))
        cli = app / "bin/soloist-cli"
        cli.write_text(cli_text)
        (app / "version").write_text("v1.3.12\n")
        (app / "bin/soloist").write_text("old executable\n")
        (app / "spotify_api.conf").write_text("keep this configuration\n")
        (app / "data").mkdir()
        (app / "data/user.dat").write_text("keep this data\n")
        (app / "stop.sh").write_text(f'#!/bin/sh\ntouch "{tmp / "stopped"}"\n')
        (app / "stop.sh").chmod(0o755)
        (app / "start.sh").write_text(f'#!/bin/sh\ntouch "{tmp / "started"}"\n')
        curl = app / "bin/curl"
        curl.write_text('''#!/bin/sh
case "$*" in
    *api.github.com*) printf '%s\\n' '  "tag_name": "v1.3.13",';;
    *)
        while [ "$#" -gt 0 ]; do
            if [ "$1" = -o ]; then out=$2; shift; fi
            shift
        done
        cp "$TEST_ARCHIVE" "$out"
        [ "${TEST_DOWNLOAD_FAIL:-0}" != 1 ];;
esac
''')
        curl.chmod(0o755)

        payload = tmp / "payload"
        for path in ("bin/soloist", "bin/soloist-bridge", "bin/curl", "lib64/ld-linux-aarch64.so.1",
                     "scripts/soloist-cli", "scripts/control.sh", "scripts/request_state.sh",
                     "scripts/search_and_play.sh", "scripts/stop.sh", "scripts/status.sh",
                     "scripts/soloist-ram-guard.sh", "scripts/migrate-standalone-guard.sh"):
            file = payload / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(cli_text if path == "scripts/soloist-cli" else "new release\n")
        for name in ("start.sh", "spotify_api.conf", "data/user.dat"):
            file = payload / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("unexpected replacement\n")
        archive = tmp / "release.tar.gz"
        with tarfile.open(archive, "w:gz") as tf:
            tf.add(payload, arcname=".")
        env = {"PATH": "/bin:/usr/bin", "INSTALL_DIR": str(app), "TEST_ARCHIVE": str(archive)}

        def run_update(extra_env=None):
            return subprocess.run(["sh", str(cli), "update"], env={**env, **(extra_env or {})},
                                  capture_output=True, text=True, timeout=15)

        for extra in ({"TEST_DOWNLOAD_FAIL": "1"}, {"TEST_ARCHIVE": str(tmp / "truncated.tar.gz")}):
            (tmp / "truncated.tar.gz").write_bytes(archive.read_bytes()[:-16])
            result = run_update(extra)
            assert result.returncode != 0, result.stdout + result.stderr
            assert not (tmp / "stopped").exists(), result.stdout
            assert (app / "bin/soloist").read_text() == "old executable\n"
            assert (app / "version").read_text() == "v1.3.12\n"
            assert not list(app.glob(".soloist-update.*"))

        result = run_update()
        assert result.returncode == 0, result.stdout + result.stderr
        assert (tmp / "stopped").exists() and (tmp / "started").exists()
        assert (app / "bin/soloist").read_text() == "new release\n"
        assert (app / "spotify_api.conf").read_text() == "keep this configuration\n"
        assert (app / "data/user.dat").read_text() == "keep this data\n"
        assert "unexpected replacement" not in (app / "start.sh").read_text()
        assert (app / "version").read_text() == "v1.3.13\n"
        assert not list(app.glob(".soloist-update.*"))
        print("PASS standalone update rejects incomplete downloads before stopping the service")


def test_standalone_guard_migration():
    with tempfile.TemporaryDirectory(prefix="soloist-migrate-") as d:
        app = pathlib.Path(d)
        start = app / "start.sh"
        prefix = '#!/bin/sh\nAPI_KEY="test_private_value"\necho before\n'
        suffix = 'echo after\n'
        combined = '''# Activation & RAM cache guard
(
    # 2. RAM guard: keep audio stream cache under 16MB
    while true; do sleep 30; done
) &
'''
        separate = '''# Activation guard
(
    echo activate
) &

# RAM guard: keep tmpfs audio cache under 16MB
(
    while true; do sleep 30; done
) &
'''
        helper = ROOT / "scripts/migrate-standalone-guard.sh"
        for old in (combined, separate):
            start.write_text(prefix + old + suffix)
            subprocess.run(["sh", str(helper), str(app)], check=True)
            migrated = start.read_text()
            assert migrated.startswith(prefix) and migrated.endswith(suffix), migrated
            assert migrated.count("soloist-ram-guard.sh") == 1, migrated
            assert "ram_guard.pid" in migrated and "while true" not in migrated, migrated
            assert start.stat().st_mode & 0o777 == 0o700
            subprocess.run(["sh", str(helper), str(app)], check=True)
            assert start.read_text() == migrated

        # Refuse to overwrite an incomplete or user-modified matching block.
        original = prefix + combined.replace(") &\n", "") + suffix
        start.write_text(original)
        res = subprocess.run(["sh", str(helper), str(app)], capture_output=True)
        assert res.returncode != 0 and start.read_text() == original
        print("PASS standalone guard migration preserves settings and refuses incomplete blocks")


if __name__ == "__main__":
    test_c_yam_paths()
    test_shell_yam_layout()
    test_soloist_setup()
    test_auto_repeat_and_stale_pid_status()
    test_ram_guard_pid_reuse()
    test_standalone_update_download_failures()
    test_standalone_guard_migration()
    print("ALL YAM V2 MODE TESTS PASSED!")
