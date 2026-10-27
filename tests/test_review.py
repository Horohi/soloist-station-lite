"""Regression checks for bounded controls, stale recovery, signals and diagnostics."""
import os
import fcntl
import pathlib
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
failures = 0


def check(ok, name):
    global failures
    print(("PASS " if ok else "FAIL ") + name, flush=True)
    failures += not ok


with tempfile.TemporaryDirectory(prefix="soloist-review-") as temporary:
    app = pathlib.Path(temporary)
    shutil.copytree(ROOT / "scripts", app / "scripts")
    (app / "bin").mkdir()
    (app / "data").mkdir()
    (app / "spotify_api.conf").write_text("SPOTIFY_CLIENT_ID=test-client\nSPOTIFY_CLIENT_SECRET=test-secret\n")
    search = app / "scripts/search_and_play.sh"
    search.write_text(search.read_text().replace("/tmp/soloist-search.log", str(app / "search.log")))
    soloist = app / "bin/soloist"
    soloist.write_text('''#!/usr/bin/env python3
import os,pathlib,signal,sys,time
p=pathlib.Path(os.environ['FIXTURE'])
action=sys.argv[4]
(p/'cli-environment').write_text(os.environ.get('LD_LIBRARY_PATH','unset')+':'+os.environ.get('LD_PRELOAD','unset'))
with (p/'controls').open('a') as out: out.write(' '.join(sys.argv[1:])+'\\n')
if action=='activate':
    (p/'activating').touch()
    (p/'worker-pid').write_text(str(os.getppid()))
    if (p/'slow-activate').exists(): time.sleep(.5)
    if (p/'slow-success').exists(): time.sleep(2.5)
    if (p/'fail-activate').exists():
        print('SECRET-RESPONSE test-secret fake-token',file=sys.stderr)
        sys.exit(23)
    if (p/'stall').exists():
        signal.signal(signal.SIGTERM,signal.SIG_IGN)
        (p/'cli-pid').write_text(str(os.getpid()))
        time.sleep(8)
        (p/'late-activate').touch()
if action=='now': print((p/'now').read_text())
if action=='play':
    if (p/'hang-play').exists():
        signal.signal(signal.SIGTERM,signal.SIG_IGN)
        (p/'cli-pid').write_text(str(os.getpid()))
        time.sleep(8)
    (p/'played').touch()
''')
    soloist.chmod(0o755)
    curl = app / "bin/curl"
    curl.write_text('''#!/usr/bin/env python3
import os,pathlib,sys,time
p=pathlib.Path(os.environ['FIXTURE']); a=sys.argv[1:]
(p/'curl-environment').write_text(os.environ.get('LD_LIBRARY_PATH','unset')+':'+os.environ.get('LD_PRELOAD','unset'))
token=any('api/token' in x for x in a)
if not token and (p/'delay').exists():
    (p/'searching').touch()
    while not (p/'release').exists(): time.sleep(.02)
body='{"access_token":"fake-token","expires_in":3600}' if token else '{"tracks":{"items":[{"album":{},"explicit":false,"name":"Test","track_number":1,"uri":"spotify:track:NEW"}]}}'
pathlib.Path(a[a.index('-o')+1]).write_text(body)
pathlib.Path(a[a.index('-D')+1]).write_text('HTTP/1.1 200\\r\\n')
print('200',end='')
''')
    curl.chmod(0o755)
    env = dict(os.environ, FIXTURE=str(app), LD_LIBRARY_PATH="/foreign-library-sentinel", LD_PRELOAD="")

    def wait_for(name, timeout=5):
        until = time.monotonic() + timeout
        while not (app / name).exists() and time.monotonic() < until:
            time.sleep(.02)
        return (app / name).exists()

    def reset():
        shutil.rmtree(app / "data/requests", ignore_errors=True)
        for name in ("controls", "activating", "worker-pid", "fail-activate", "slow-activate", "slow-success", "hang-play", "stall", "cli-pid", "late-activate", "played", "delay", "searching", "release", "search.log"):
            (app / name).unlink(missing_ok=True)
        (app / "now").write_text('playing spotify:track:OLD')

    def run_search():
        return subprocess.Popen(["sh", str(search), "включи песню Test"], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def control(*args):
        return subprocess.run(["sh", str(app / "scripts/control.sh"), *args], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=7)

    reset()
    (app / "stall").touch()
    start = time.monotonic()
    child = run_search()
    wait_for("cli-pid")
    try:
        code = child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        code = None
    check(code is not None and code != 0 and time.monotonic()-start < 5.5,
          "TERM-ignoring CLI is bounded and search fails")
    pid = int((app / "cli-pid").read_text())
    try:
        os.kill(pid, 0)
        alive = True
    except ProcessLookupError:
        alive = False
    check(not alive, "timed-out CLI is dead and reaped before dispatch returns")
    if alive:
        os.kill(pid, signal.SIGKILL)
    child.wait(timeout=3)
    control("pause")
    time.sleep(.25)
    check(" pause" in (app / "controls").read_text(), "pause works after CLI timeout")
    check(not (app / "played").exists() and not (app / "late-activate").exists(), "timed-out activation cannot dispatch late play")

    reset()
    (app / "slow-success").touch()
    (app / "hang-play").touch()
    child = run_search()
    wait_for("activating")
    control("pause")
    child.wait(timeout=7)
    time.sleep(.3)
    check(" pause" in (app / "controls").read_text(), "slow activate plus hanging play cannot exhaust following pause admission")

    reset()
    (app / "delay").touch()
    child = run_search()
    wait_for("searching")
    generation = (app / "data/requests/generation").read_text().strip()
    (app / "data/requests/track").write_text("spotify:track:OLD")
    control("auto-next", "spotify:track:OLD", generation)
    time.sleep(.3)
    check(" next" in (app / "controls").read_text() if (app / "controls").exists() else False,
          "matching automatic recovery advances the original track")
    check((app / "data/requests/generation").read_text().strip() == generation,
          "automatic recovery does not invalidate explicit search")
    (app / "release").touch()
    check(child.wait(timeout=5) == 0 and (app / "played").exists(), "delayed explicit search survives automatic recovery")
    for changed in ("track", "generation"):
        reset()
        requests = app / "data/requests"
        requests.mkdir()
        (requests / "generation").write_text("1")
        (requests / "track").write_text("spotify:track:OLD")
        with (requests / "control.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            recovery = subprocess.Popen(["sh", str(app / "scripts/control.sh"), "auto-next", "spotify:track:OLD", "1"], env=env)
            time.sleep(.15)
            (requests / changed).write_text("2" if changed == "generation" else "spotify:track:NEW")
            fcntl.flock(lock, fcntl.LOCK_UN)
        recovery.wait(timeout=5)
        time.sleep(.25)
        check(not (app / "controls").exists() or " next" not in (app / "controls").read_text(),
              "queued automatic recovery rejects changed " + changed)

    for sig in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        reset()
        child = run_search()
        wait_for("activating")
        child.send_signal(sig)
        code = child.wait(timeout=5)
        time.sleep(.4)
        check(code != 0 and not (app / "played").exists(), "signal %s exits without delayed play" % sig.name)
        check(not list((app / "data/requests").glob("body.*")) and not list((app / "data/requests").glob("headers.*")), "signal %s cleans response files" % sig.name)

    reset()
    (app / "slow-activate").touch()
    child = run_search()
    wait_for("activating")
    requests = app / "data/requests"
    (requests / "track").write_text("spotify:track:OLD")
    generation = (requests / "generation").read_text().strip()
    recovery = subprocess.Popen(["sh", str(app / "scripts/control.sh"), "auto-next", "spotify:track:OLD", generation], env=env)
    child.wait(timeout=5)
    recovery.wait(timeout=5)
    time.sleep(.3)
    check(" next" not in (app / "controls").read_text(), "queued recovery cannot advance selection before WebSocket catches up")

    reset()
    (app / "fail-activate").touch()
    child = run_search()
    check(child.wait(timeout=5) != 0 and not (app / "played").exists(), "activation failure prevents play")
    logs = "\n".join(p.read_text() for p in app.rglob("*.log"))
    check((app / "cli-environment").read_text() == str(app / "lib64") + ":unset",
          "Soloist alone receives its bundled library path without inherited preload")
    check((app / "curl-environment").read_text() == "unset:unset", "HTTP helpers do not inherit Soloist or caller library overrides")
    check("action=play" in logs and "stage=activate" in logs and "status=23" in logs,
          "activation failure records action, stage and original status")
    check(not any(secret in logs for secret in ("test-secret", "fake-token", "SECRET-RESPONSE")), "diagnostics omit credentials and child responses")
    reset()
    (app / "fail-activate").touch()
    control("search", "включи песню Test")
    wait_for("data/requests/control.log")
    log = app / "data/requests/control.log"
    check(log.exists() and "action=play stage=activate status=23" in log.read_text(),
          "background controller search reports activation failure")

    for sig in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        reset()
        (app / "slow-activate").touch()
        worker = subprocess.Popen(["sh", str(app / "scripts/control.sh"), "search", "включи песню Test"], env=env)
        wait_for("worker-pid")
        os.kill(int((app / "worker-pid").read_text()), sig)
        code = worker.wait(timeout=5)
        time.sleep(.2)
        check(code != 0 and not (app / "played").exists(), "controller worker halts nonzero on " + sig.name)

raise SystemExit(bool(failures))
