#!/usr/bin/env python3
"""Exercise Spotify proxy switching without changing host DNS or using a network."""
import os
import pathlib
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
HELPER = ROOT / "scripts/spotify-network.sh"


def main():
    with tempfile.TemporaryDirectory(prefix="soloist-network-") as d:
        tmp = pathlib.Path(d)
        hosts = tmp / "hosts"
        config = tmp / "spotify-network.conf"
        health = tmp / "healthy"
        calls = tmp / "calls"
        fake_curl = tmp / "curl"
        fake_curl.write_text('''#!/bin/sh
ip=direct
while [ "$#" -gt 0 ]; do
    if [ "$1" = --resolve ]; then ip=${2##*:}; shift; fi
    shift
done
printf '%s\\n' "$ip" >> "$TEST_CALLS"
if [ "${TEST_PAUSE_IP:-}" = "$ip" ]; then
    touch "$TEST_STARTED"
    while [ ! -f "$TEST_RELEASE" ]; do sleep 0.05; done
fi
if grep -Fxq "$ip" "$TEST_HEALTH"; then printf 401; else printf 403; fi
''')
        fake_curl.chmod(0o755)
        config.write_text('SOLOIST_NETWORK_MODE="auto"\n')
        env = {**os.environ, "SOLOIST_HOSTS_FILE": str(hosts), "SOLOIST_NET_CONF": str(config),
               "SOLOIST_NET_STATE": str(tmp / "state"), "SOLOIST_NET_CURL": str(fake_curl),
               "SOLOIST_NET_CANDIDATES": "5.175.161.40 37.221.93.141 193.233.112.67",
               "TEST_HEALTH": str(health), "TEST_CALLS": str(calls)}

        def invoke(action="check"):
            return subprocess.run(["sh", str(HELPER), action], env=env, text=True, capture_output=True)

        legacy = ("127.0.0.1 localhost\n# Spotify SNI Proxy (Bypass 403 geoblock)\n"
                  "5.175.161.40 accounts.spotify.com\n5.175.161.40 api.spotify.com\n"
                  "5.175.161.40 open.spotify.com\n5.175.161.40 spclient.wg.spotify.com\n"
                  "10.0.0.4 my-home-device\n")
        hosts.write_text(legacy)
        health.write_text("5.175.161.40\n37.221.93.141\n")
        assert invoke().returncode == 0 and hosts.read_text() == legacy
        assert calls.read_text().splitlines() == ["5.175.161.40"]

        health.write_text("37.221.93.141\n")
        assert invoke().returncode == 0
        selected = hosts.read_text()
        assert "37.221.93.141 api.spotify.com" in selected
        assert "5.175.161.40 api.spotify.com" not in selected
        assert "10.0.0.4 my-home-device" in selected
        assert "# BEGIN SOLOIST SPOTIFY PROXY" in selected

        health.write_text("")
        assert invoke().returncode != 0 and hosts.read_text() == selected
        health.write_text("direct\n37.221.93.141\n")
        hosts.write_text("127.0.0.1 localhost\n")
        assert invoke().returncode == 0 and hosts.read_text() == "127.0.0.1 localhost\n"
        assert invoke("recommend").stdout.strip() == "current"
        health.write_text("37.221.93.141\n")
        assert invoke("recommend").stdout.strip() == "proxy"
        health.write_text("")
        assert invoke("recommend").stdout.strip() == "unavailable"

        # A third party's explicit mapping is never overwritten on failover.
        hosts.write_text("127.0.0.1 localhost\n192.0.2.10 api.spotify.com\n")
        health.write_text("37.221.93.141\n")
        before = hosts.read_text()
        assert invoke().returncode != 0 and hosts.read_text() == before

        config.write_text('SOLOIST_NETWORK_MODE="off"\n')
        calls.write_text("")
        assert invoke().returncode == 0 and calls.read_text() == ""
        untouched_inode = hosts.stat().st_ino
        assert invoke("off").returncode == 0 and hosts.stat().st_ino == untouched_inode
        hosts.write_text(selected)
        assert invoke("off").returncode == 0
        assert "api.spotify.com" not in hosts.read_text() and "my-home-device" in hosts.read_text()

        # Disabling auto while a probe is in progress must cancel the pending switch.
        hosts.write_text(legacy)
        config.write_text('SOLOIST_NETWORK_MODE="auto"\n')
        health.write_text("37.221.93.141\n")
        race_env = {**env, "TEST_PAUSE_IP": "37.221.93.141",
                    "TEST_STARTED": str(tmp / "started"), "TEST_RELEASE": str(tmp / "release")}
        child = subprocess.Popen(["sh", str(HELPER), "check"], env=race_env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 5
            while not (tmp / "started").exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            assert (tmp / "started").exists(), "probe did not start"
            config.write_text('SOLOIST_NETWORK_MODE="off"\n')
            (tmp / "release").touch()
            stdout, stderr = child.communicate(timeout=5)
            assert child.returncode == 0 and hosts.read_text() == legacy, (stdout, stderr)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()

        # A real setup invocation, with fake Spotify probes and sandboxed files,
        # selects auto only when a working proxy was actually found.
        app = tmp / "app"
        (app / "scripts").mkdir(parents=True)
        (app / "bin").mkdir()
        (app / "scripts/spotify-network.sh").write_text(HELPER.read_text())
        (app / "scripts/soloist-cli").write_text((ROOT / "scripts/soloist-cli").read_text())
        (app / "start.sh").write_text('#!/bin/sh\nAPI_KEY="spak_existing"\n    -n "Station" \\\n')
        hosts.write_text("127.0.0.1 localhost\n")
        health.write_text("37.221.93.141\n")
        setup_env = {**env, "INSTALL_DIR": str(app), "SOLOIST_SETUP_NO_RESTART": "1"}
        setup = subprocess.run(["sh", str(app / "scripts/soloist-cli"), "setup"],
                               input="\n" * 5, env=setup_env, text=True, capture_output=True, timeout=10)
        assert setup.returncode == 0, setup.stdout + setup.stderr
        assert 'SOLOIST_NETWORK_MODE="auto"' in (app / "spotify-network.conf").read_text()
        assert "37.221.93.141 api.spotify.com" in hosts.read_text(), hosts.read_text()
        assert "найден рабочий прокси" in setup.stdout

    print("PASS Spotify proxy selection preserves current, handles failures and protects custom hosts")


if __name__ == "__main__":
    main()
