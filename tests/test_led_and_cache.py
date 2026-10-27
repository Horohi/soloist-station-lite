#!/usr/bin/env python3
"""Test LED visualizer triggers and cache cleanup mechanisms."""
import os
import pathlib
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/soloist-bridge.c").read_text(encoding="utf-8")

failures = 0
def check(ok, name):
    global failures
    print(("PASS " if ok else "FAIL ") + name, flush=True)
    failures += not ok

# 1. Voice classification tests for LED commands
prefix = '''#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_led_state = -1; // -1=none, 0=off, 1=on
static int g_suppressed = 0;
static int spotify_is_active = 0;
static void trigger_alice_suppression(int fd, int64_t ms) { (void)fd; (void)ms; g_suppressed++; }
#define set_led_visualizer(enable) do { g_led_state = (enable); } while(0)
static void log_msg(const char *s) { (void)s; }
static int is_explicit_search(const char *s) { (void)s; return 0; }
static void run_soloist_cmd(const char *s) { (void)s; }
static void run_soloist_cmd2(const char *s, const char *a) { (void)s; (void)a; }
static void run_search_and_play(const char *s) { (void)s; }
static const char *get_censor_path(void) { return "/tmp/censor"; }
static int sys_open(const char *p, int f, int m) { (void)p; (void)f; (void)m; return -1; }
static void sys_close(int fd) { (void)fd; }
static int sys_unlink(const char *p) { (void)p; return 0; }
static long sys_write(int fd, const void *b, unsigned long n) { (void)fd; (void)b; return n; }
static unsigned long str_len(const char *s) { return s ? strlen(s) : 0; }
'''

# Extract helpers and handle_voice_recognition
helpers = source[source.index("static void to_lower_ru"):source.index("static int is_explicit_search")]
voice_func = source[source.index("static void handle_voice_recognition"):source.index("/* =========================================================================")]

test_c = prefix + helpers + voice_func + '''
int main(void) {
    // 1. ON triggers (strictly without "включи")
    g_led_state = -1; handle_voice_recognition(0, "зажигай", 14);
    if (g_led_state != 1) return 1;

    g_led_state = -1; handle_voice_recognition(0, "зажги", 10);
    if (g_led_state != 1) return 2;

    g_led_state = -1; handle_voice_recognition(0, "покажи эквалайзер", 33);
    if (g_led_state != 1) return 3;

    g_led_state = -1; handle_voice_recognition(0, "режим дискотеки", 29);
    if (g_led_state != 1) return 4;

    g_led_state = -1; handle_voice_recognition(0, "светомузыка", 22);
    if (g_led_state != 1) return 5;

    g_led_state = -1; handle_voice_recognition(0, "эквалайзер", 20);
    if (g_led_state != 1) return 6;

    // 2. OFF triggers (strictly without "выключи")
    g_led_state = -1; handle_voice_recognition(0, "стоп дискотека", 27);
    if (g_led_state != 0) return 7;

    g_led_state = -1; handle_voice_recognition(0, "останови дискотеку", 35);
    if (g_led_state != 0) return 8;

    g_led_state = -1; handle_voice_recognition(0, "стоп эквалайзер", 29);
    if (g_led_state != 0) return 9;

    g_led_state = -1; handle_voice_recognition(0, "погаси подсветку", 31);
    if (g_led_state != 0) return 10;

    g_led_state = -1; handle_voice_recognition(0, "хватит светить", 27);
    if (g_led_state != 0) return 11;

    // 3. MUST NOT match smart home lighting or phrases with "включи" / "выключи"
    g_led_state = -1; handle_voice_recognition(0, "выключи свет", 23);
    if (g_led_state != -1) return 12; // Must NOT touch LED state!

    g_led_state = -1; handle_voice_recognition(0, "выключи свет в спальне", 41);
    if (g_led_state != -1) return 13; // Must NOT touch LED state!

    g_led_state = -1; handle_voice_recognition(0, "включи свет", 21);
    if (g_led_state != -1) return 14; // Must NOT touch LED state!

    g_led_state = -1; handle_voice_recognition(0, "включи дискотеку", 31);
    if (g_led_state != -1) return 15; // Ignored by LED because it contains "включи"

    g_led_state = -1; handle_voice_recognition(0, "выключи дискотеку", 33);
    if (g_led_state != -1) return 16; // Ignored by LED because it contains "выключи"

    g_led_state = -1; handle_voice_recognition(0, "включи эквалайзер", 33);
    if (g_led_state != -1) return 17; // Ignored by LED because it contains "включи"

    g_led_state = -1; handle_voice_recognition(0, "выключи эквалайзер", 35);
    if (g_led_state != -1) return 18; // Ignored by LED because it contains "выключи"

    return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    src_file = pathlib.Path(td) / "test_led.c"
    bin_file = pathlib.Path(td) / "test_led"
    src_file.write_text(test_c)
    res = subprocess.run(["cc", "-O1", "-Wall", str(src_file), "-o", str(bin_file)], capture_output=True, text=True)
    check(res.returncode == 0, "LED voice recognition compiles cleanly")
    if res.returncode == 0:
        run_res = subprocess.run([str(bin_file)])
        check(run_res.returncode == 0, "LED voice triggers match correctly and preserve smart home lighting")

# 2. Control script cache cleanup tests
with tempfile.TemporaryDirectory() as td:
    tpath = pathlib.Path(td)
    cache_dir = tpath / "tmp_cache" / "cache"
    cache_dir.mkdir(parents=True)
    old_file = cache_dir / "old_chunk.dat"
    old_file.write_text("old")
    os.utime(old_file, (os.path.getatime(old_file) - 120, os.path.getmtime(old_file) - 120))

    ctrl_sh = ROOT / "scripts/control.sh"
    env = os.environ.copy()
    proc = subprocess.run(["sh", str(ctrl_sh), "clean-cache"], env=env, capture_output=True)
    check(proc.returncode == 0, "control.sh clean-cache exits with code 0")

    proc_full = subprocess.run(["sh", str(ctrl_sh), "clean-cache-full"], env=env, capture_output=True)
    check(proc_full.returncode == 0, "control.sh clean-cache-full exits with code 0")

# 3. Soloist CLI tests
cli_sh = ROOT / "scripts/soloist-cli"
proc_cli_help = subprocess.run(["sh", str(cli_sh), "help"], capture_output=True, text=True)
check("led" in proc_cli_help.stdout, "soloist-cli help lists led command")

with tempfile.TemporaryDirectory() as td:
    env = os.environ.copy()
    env["INSTALL_DIR"] = td
    proc_led_on = subprocess.run(["sh", str(cli_sh), "led", "on"], env=env, capture_output=True, text=True)
    check(proc_led_on.returncode == 0, "soloist-cli led on executes successfully")
    req_f = pathlib.Path(td) / "data/requests/led"
    check(req_f.exists() and req_f.read_text().strip() == "1", "soloist-cli led on writes IPC request")

    proc_led_off = subprocess.run(["sh", str(cli_sh), "led", "off"], env=env, capture_output=True, text=True)
    check(proc_led_off.returncode == 0, "soloist-cli led off executes successfully")
    check(req_f.exists() and req_f.read_text().strip() == "0", "soloist-cli led off writes IPC request")

    proc_clean = subprocess.run(["sh", str(cli_sh), "clean"], env=env, capture_output=True, text=True)
    check(proc_clean.returncode == 0, "soloist-cli clean executes successfully")

if failures == 0:
    print("ALL LED & CACHE TESTS PASSED!", flush=True)
    sys.exit(0)
else:
    print(f"FAILED {failures} checks", flush=True)
    sys.exit(1)
