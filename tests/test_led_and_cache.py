#!/usr/bin/env python3
"""Test LED visualizer triggers and cache cleanup mechanisms."""
import os
import pathlib
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/soloist-bridge.c").read_text(encoding="utf-8")
text_json_src = (ROOT / "src/text-json.h").read_text(encoding="utf-8")
led_hdr_src = (ROOT / "src/led-subsystem.h").read_text(encoding="utf-8")

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
static int g_last_fx = 0;
static int g_fx_cleared = 0;
#define LED_FX_NONE   0
#define LED_FX_PLAY   1
#define LED_FX_NEXT   2
#define LED_FX_PREV   3
#define LED_FX_SEARCH 4
#define LED_FX_ERROR  5
static void led_fx_trigger(int fx_type) { g_last_fx = fx_type; }
static void led_fx_clear(void) { g_fx_cleared++; g_last_fx = 0; }
static int g_suppressed = 0;
static int g_searches = 0;
static int spotify_is_active = 0;
static int spotify_device_is_active = 0;
static int yandex_music_active = 0;
static int64_t yandex_music_handoff_ms = 0;
static int64_t now_ms(void) { return 10000; }
static void trigger_alice_suppression(int fd, int64_t ms) { (void)fd; (void)ms; g_suppressed++; }
#define set_led_visualizer(enable) do { g_led_state = (enable); } while(0)
static void log_msg(const char *s) { (void)s; }
static int is_explicit_search(const char *s) { (void)s; return 0; }
static void run_controller(const char *a, const char *b) { (void)a; (void)b; }
static void run_soloist_cmd(const char *s) { (void)s; }
static void run_soloist_cmd2(const char *s, const char *a) { (void)s; (void)a; }
static void run_search_and_play(const char *s) { (void)s; g_searches++; }
static const char *get_censor_path(void) { return "/tmp/censor"; }
static int is_web_or_culinary_intent(const char *s) { (void)s; return 0; }
static int is_yandex_music_preference(const char *s) { (void)s; return 0; }
static int send_alice_tts(int fd, const char *s) { (void)fd; (void)s; return 0; }
static int playback_status = 0;
static char current_track_name[128] = {0};
static char current_artist_name[128] = {0};
static int sys_open(const char *p, int f, int m) { (void)p; (void)f; (void)m; return -1; }
static void sys_close(int fd) { (void)fd; }
static int sys_unlink(const char *p) { (void)p; return 0; }
static long sys_write(int fd, const void *b, unsigned long n) { (void)fd; (void)b; return n; }
static unsigned long str_len(const char *s) { return s ? strlen(s) : 0; }
'''

# Extract helpers and handle_voice_recognition
helpers = (
    text_json_src[text_json_src.index("static void to_lower_ru"):text_json_src.index("static int is_explicit_search")]
    + "\n"
    + text_json_src[text_json_src.index("static void trim_trailing_voice_punct"):text_json_src.index("static void append_artist_name")]
)
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

    // Playlist titles can contain words used by LED and household commands.
    g_searches = 0;
    const char *playlist = "алиса, давай включи плейлист погаси свет";
    handle_voice_recognition(0, playlist, strlen(playlist));
    if (g_searches != 1 || g_led_state != -1) return 19;
    const char *yandex_playlist = "включи плейлист джаз в яндекс музыке";
    handle_voice_recognition(0, yandex_playlist, strlen(yandex_playlist));
    if (g_searches != 1) return 20;

    // Russian tracks sharing prefixes with smart-home nouns must reach Spotify search,
    // including explicit media requests containing exact smart-home nouns.
    const char *music_ok[] = {
        "включи трек светофор",
        "включи светофор",
        "включи шторм",
        "включи песню свет",
        "включи группу лампасы",
    };
    int expected_searches = 1;
    for (unsigned i = 0; i < sizeof(music_ok) / sizeof(music_ok[0]); i++) {
        handle_voice_recognition(0, music_ok[i], strlen(music_ok[i]));
        expected_searches++;
        if (g_searches != expected_searches) return 21 + (int)i;
    }

    // Actual smart-home commands and musical alarms must stay ignored for Alice.
    const char *smart_home[] = {
        "включи свет",
        "включи свет на кухне",
        "включи светильник",
        "включи шторы",
        "включи лампу",
        "поставь будильник с музыкой",
    };
    for (unsigned i = 0; i < sizeof(smart_home) / sizeof(smart_home[0]); i++) {
        handle_voice_recognition(0, smart_home[i], strlen(smart_home[i]));
        if (g_searches != expected_searches) return 30 + (int)i;
    }

    // Spotify LED FX triggers on search, play/resume, next, prev, and pause clear
    g_last_fx = 0;
    handle_voice_recognition(0, "включи песню Кукушка", strlen("включи песню Кукушка"));
    if (g_last_fx != LED_FX_SEARCH) return 40;

    g_last_fx = 0; spotify_is_active = 1; spotify_device_is_active = 1; yandex_music_active = 0;
    handle_voice_recognition(0, "дальше", strlen("дальше"));
    if (g_last_fx != LED_FX_NEXT) return 41;

    g_last_fx = 0;
    handle_voice_recognition(0, "назад", strlen("назад"));
    if (g_last_fx != LED_FX_PREV) return 42;

    g_fx_cleared = 0; playback_status = 1;
    handle_voice_recognition(0, "пауза", strlen("пауза"));
    if (g_fx_cleared == 0) return 43;

    g_last_fx = 0;
    handle_voice_recognition(0, "играй", strlen("играй"));
    if (g_last_fx != LED_FX_PLAY) return 44;

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

# 2. Control script cache cleanup tests (BusyBox find -mmin + size cap)
with tempfile.TemporaryDirectory() as td:
    tpath = pathlib.Path(td)
    cache_dir = tpath / "tmp_cache" / "cache" / "ab"
    cache_dir.mkdir(parents=True)
    old_file = cache_dir / "old_chunk.dat"
    old_file.write_text("old")
    os.utime(old_file, (os.path.getatime(old_file) - 120, os.path.getmtime(old_file) - 120))
    fresh_file = cache_dir / "fresh_chunk.dat"
    fresh_file.write_text("fresh")

    ctrl_sh = ROOT / "scripts/control.sh"
    env = os.environ.copy()
    env["SOLOIST_CACHE_DIR"] = str(tpath / "tmp_cache" / "cache")
    proc = subprocess.run(["sh", str(ctrl_sh), "clean-cache"], env=env, capture_output=True)
    check(proc.returncode == 0, "control.sh clean-cache exits with code 0")
    check(not old_file.exists(), "control.sh clean-cache removes >1m old chunks without -amin/-delete")
    check(fresh_file.exists(), "control.sh clean-cache keeps recent chunk under 16MB")

    # Exceed 16MB with fresh files and verify oldest are trimmed
    for idx in range(5):
        f = cache_dir / f"big_{idx}.file"
        f.write_bytes(b"\0" * (4 * 1024 * 1024))
        os.utime(f, (1000000 + idx * 10, 1000000 + idx * 10))
    # Touch fresh_file as newest? No, use recent mtime so -mmin +1 keeps them for size-cap test
    import time
    base_t = time.time() - 5
    for idx in range(5):
        f = cache_dir / f"big_{idx}.file"
        os.utime(f, (base_t + idx, base_t + idx))
    os.utime(fresh_file, (base_t + 10, base_t + 10))
    proc_cap = subprocess.run(["sh", str(ctrl_sh), "clean-cache"], env=env, capture_output=True)
    check(proc_cap.returncode == 0, "control.sh clean-cache enforces 16MB cap exits 0")
    remaining = sorted(p.name for p in cache_dir.iterdir())
    check(len(remaining) <= 3 and "big_0.file" not in remaining, f"control.sh trims excess >16MB chunks ({remaining})")

    proc_full = subprocess.run(["sh", str(ctrl_sh), "clean-cache-full"], env=env, capture_output=True)
    check(proc_full.returncode == 0, "control.sh clean-cache-full exits with code 0")
    check(not list((tpath / "tmp_cache" / "cache").iterdir()), "control.sh clean-cache-full clears cache directory")

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

    proc_led_green = subprocess.run(["sh", str(cli_sh), "led", "green"], env=env, capture_output=True, text=True)
    check(proc_led_green.returncode == 0, "soloist-cli led green executes successfully")
    check(req_f.exists() and req_f.read_text().strip() == "g", "soloist-cli led green writes IPC request")

    proc_fx_off = subprocess.run(["sh", str(cli_sh), "led", "fx", "off"], env=env, capture_output=True, text=True)
    check(proc_fx_off.returncode == 0, "soloist-cli led fx off executes successfully")
    check(req_f.exists() and req_f.read_text().strip() == "f0", "soloist-cli led fx off writes f0 IPC request")

    proc_fx_on = subprocess.run(["sh", str(cli_sh), "led", "fx", "on"], env=env, capture_output=True, text=True)
    check(proc_fx_on.returncode == 0, "soloist-cli led fx on executes successfully")
    check(req_f.exists() and req_f.read_text().strip() == "f1", "soloist-cli led fx on writes f1 IPC request")

    proc_fx_status_def = subprocess.run(["sh", str(cli_sh), "led", "fx", "status"], env=env, capture_output=True, text=True)
    check(proc_fx_status_def.returncode == 0 and "ВКЛЮЧЕНЫ" in proc_fx_status_def.stdout,
          "soloist-cli led fx status defaults to ON")

    conf_cli = pathlib.Path(td) / "spotify_api.conf"
    conf_cli.write_text('LED_ANIMATIONS="off"\n', encoding="utf-8")
    proc_fx_status_conf = subprocess.run(["sh", str(cli_sh), "led", "fx", "status"], env=env, capture_output=True, text=True)
    check(proc_fx_status_conf.returncode == 0 and "ВЫКЛЮЧЕНЫ" in proc_fx_status_conf.stdout,
          "soloist-cli led fx status reads LED_ANIMATIONS=off from conf")

    fx_state_cli = pathlib.Path(td) / "data/requests/led_fx_state"
    fx_state_cli.write_text("on\n", encoding="utf-8")
    proc_fx_status_state = subprocess.run(["sh", str(cli_sh), "led", "fx", "status"], env=env, capture_output=True, text=True)
    check(proc_fx_status_state.returncode == 0 and "ВКЛЮЧЕНЫ" in proc_fx_status_state.stdout,
          "soloist-cli led fx status prefers led_fx_state over conf")
    fx_state_cli.unlink()
    conf_cli.unlink()

    proc_status_def = subprocess.run(["sh", str(cli_sh), "led", "status"], env=env, capture_output=True, text=True)
    check(proc_status_def.returncode == 0 and "ВКЛЮЧЕНА" in proc_status_def.stdout, "soloist-cli led status defaults to ON white")

    proc_clean = subprocess.run(["sh", str(cli_sh), "clean"], env=env, capture_output=True, text=True)
    check(proc_clean.returncode == 0, "soloist-cli clean executes successfully")

def slice_between(txt: str, start: str, end: str) -> str:
    s = txt.find(start)
    assert s != -1, f"Start marker {start} not found"
    e = txt.find(end, s)
    assert e != -1, f"End marker {end} not found"
    return txt[s:e]

# 4. "Алиса, включи эйрплей" must be left to Quasar Bridge.
# The AirPlay receiver lives there and answers should_listen=false, starting
# nothing. Without this hand-off the "включи ..." branch below treats the phrase
# as a Spotify catalog query, cancels Alice's answer and plays a random track.
# An explicit media word keeps a real search ("включи песню Airplay").
predicate = slice_between(
    text_json_src,
    "static int is_airplay_command(const char *lower) {",
    "static int is_named_playlist_request(const char *text) {",
)
with tempfile.TemporaryDirectory() as td:
    probe = pathlib.Path(td) / "air.c"
    probe.write_text(r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int str_contains(const char *h, const char *n) {
    if (!h || !n) return 0;
    unsigned long hl = strlen(h), nl = strlen(n);
    if (!nl) return 1;
    if (hl < nl) return 0;
    for (unsigned long i = 0; i <= hl - nl; i++) {
        unsigned long j = 0;
        while (j < nl && h[i + j] == n[j]) j++;
        if (j == nl) return 1;
    }
    return 0;
}

__PREDICATE__

struct case_t { const char *text; int want; };

int main(void) {
    static const struct case_t cases[] = {
        {"включи эйрплей", 1},
        {"включи airplay", 1},
        {"вруби эйр плей", 1},
        {"поставь эирплей", 1},
        {"включи песню airplay", 0},
        {"включи трек эйрплей", 0},
        {"включи музыку", 0},
        {"включи кино", 0},
        {"поставь плейлист airplay mix", 0},
        {"включи группу аэрплай", 0},
        {"airplay", 1},
    };
    int bad = 0;
    for (unsigned long i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        int got = is_airplay_command(cases[i].text);
        if (got != cases[i].want) {
            fprintf(stderr, "case %lu [%s]: got %d want %d\n", i, cases[i].text, got, cases[i].want);
            bad++;
        }
    }
    return bad ? 1 : 0;
}
""".replace("__PREDICATE__", predicate), encoding="utf-8")
    bin_p = probe.with_suffix("")
    rc = subprocess.run(["cc", "-O2", "-Wall", "-Wextra", str(probe), "-o", str(bin_p)]).returncode
    if rc != 0:
        check(False, "airplay predicate probe compiles")
    else:
        rc = subprocess.run([str(bin_p)]).returncode
        check(rc == 0, f"is_airplay_command hands AirPlay to the receiver and spares real searches (rc={rc})")

# 5. Bridge read_led_config() default-white migration & config override test
with tempfile.TemporaryDirectory() as td:
    td_p = pathlib.Path(td)
    req_dir = td_p / "requests"
    req_dir.mkdir(parents=True, exist_ok=True)
    conf_p = td_p / "spotify_api.conf"
    state_p = req_dir / "led_state"
    conf_p.write_text('LED_VISUALIZER="off"\n', encoding="utf-8")
    state_p.write_text("off\n", encoding="utf-8")

    read_led_cfg_fn = slice_between(led_hdr_src, "static void parse_led_conf_buf(", "static void check_led_request(")
    join_fn = slice_between(source, "static void join_path(char *dst, unsigned long max, const char *a, const char *b) {", "static void lookup_proc_env(")
    led_test_c = td_p / "led_cfg_test.c"
    led_test_bin = td_p / "led_cfg_test"
    led_test_c.write_text(f'''
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_led_enabled = 1;
static int g_led_color_mode = 0;
static int g_led_fx_enabled = 1;
static const char *g_test_req_dir = "{req_dir}";
static const char *g_test_conf = "{conf_p}";
static const char *g_test_state = "{state_p}";
static const char *get_requests_dir(void) {{ return g_test_req_dir; }}
static const char *get_conf_path(void) {{ return g_test_conf; }}
static const char *get_led_state_path(void) {{ return g_test_state; }}
static int sys_mkdir(const char *p, int m) {{ return mkdir(p, m); }}
static int sys_open(const char *p, int f, int m) {{ return open(p, f, m); }}
static long sys_read(int fd, void *b, unsigned long c) {{ return read(fd, b, c); }}
static long sys_write(int fd, const void *b, unsigned long c) {{ return write(fd, b, c); }}
static int sys_close(int fd) {{ return close(fd); }}
static int sys_unlink(const char *p) {{ return unlink(p); }}
static int mem_equal(const void *a, const void *b, unsigned long n) {{ return memcmp(a, b, n) == 0; }}
static int str_contains(const char *h, const char *n) {{ return strstr(h, n) != NULL; }}

{join_fn}
{read_led_cfg_fn}

int main(void) {{
    // 1. First launch (no marker): forces white ON despite old off state/conf
    g_led_enabled = 0; g_led_color_mode = 2; g_led_fx_enabled = 0;
    read_led_config();
    if (g_led_enabled != 1 || g_led_color_mode != 0 || g_led_fx_enabled != 1) return 1;
    if (access("{state_p}", F_OK) == 0) return 2;

    // 2. Subsequent launch: honors config (quoted/unquoted) and ignores comments
    g_led_enabled = 1; g_led_color_mode = 1;
    read_led_config(); // conf currently has LED_VISUALIZER="off"
    if (g_led_enabled != 0) return 3;

    FILE *f = fopen(g_test_conf, "w");
    fprintf(f, "# LED_VISUALIZER=off\\nLED_VISUALIZER=\\"rainbow\\"\\n");
    fclose(f);
    read_led_config();
    if (g_led_enabled != 1 || g_led_color_mode != 1) return 4;

    f = fopen(g_test_conf, "w");
    fprintf(f, "LED_VISUALIZER=cyan\\n");
    fclose(f);
    read_led_config();
    if (g_led_enabled != 1 || g_led_color_mode != 2) return 5;

    f = fopen(g_test_conf, "w");
    fprintf(f, "LED_VISUALIZER=green\\nLED_ANIMATIONS=\\"off\\"\\n");
    fclose(f);
    read_led_config();
    if (g_led_enabled != 1 || g_led_color_mode != 4 || g_led_fx_enabled != 0) return 7;

    f = fopen(g_test_conf, "w");
    fprintf(f, "LED_ANIMATIONS=on\\n");
    fclose(f);
    read_led_config();
    if (g_led_fx_enabled != 1) return 8;

    f = fopen(g_test_conf, "w");
    fprintf(f, "LED_FX=off\\n");
    fclose(f);
    read_led_config();
    if (g_led_fx_enabled != 0) return 9;

    // 3. Explicit runtime state files override conf on restart
    f = fopen(g_test_state, "w");
    fprintf(f, "off\\n");
    fclose(f);
    {{
        char fxs[512];
        snprintf(fxs, sizeof(fxs), "%s/led_fx_state", g_test_req_dir);
        f = fopen(fxs, "w");
        fprintf(f, "on\\n");
        fclose(f);
    }}
    read_led_config();
    if (g_led_enabled != 0 || g_led_fx_enabled != 1) return 6;
    return 0;
}}
''', encoding="utf-8")
    subprocess.check_call(["cc", "-O2", "-Wall", "-Wextra", str(led_test_c), "-o", str(led_test_bin)])
    rc = subprocess.run([str(led_test_bin)]).returncode
    check(rc == 0, f"read_led_config migration & config override checks passed (rc={rc})")

# 6. led_all_off() must hand the ring back to the trigger the kernel uses.
# On a Midi (cucumber, is_zero_indexed) the kernel trigger is ledfb-ring;
# writing brightness detaches it and maind then animates into a framebuffer
# nobody reads, so Alice's ring stays dark until a reboot. Upstream restored
# only plum's led_ring_fb, which left every Midi without an indication.
# write_sysfs_str is called for the trigger only, so it is the observation point.
with tempfile.TemporaryDirectory() as td:
    td_p = pathlib.Path(td)
    probe = td_p / "probe.c"
    bin_p = td_p / "probe"
    off_fn = slice_between(led_hdr_src, "static void led_all_off(void) {", "static void join_path(")
    reset_fn = slice_between(led_hdr_src, "static void led_reset_internal_state(void) {", "static void led_all_off(void) {")
    probe.write_text(f'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct led_hardware_info {{
    int probed; int detected; int num_zones; int is_zero_indexed;
    char prefix[32]; int is_plum;
}};
static struct led_hardware_info g_led_hw = {{0}};
static int g_led_ring_dirty = 1;
static int g_led_last_active = 1;
static uint64_t g_led_frame = 0;
static int g_led_last_rgb[24][3];
static int g_prev_zone_val[24];
static int g_band_floor[3], g_band_peak[3], g_band_init[3];
static int g_shm_is_32bit = 0;
static int g_led_rgb_cache_valid = 0;
static int64_t g_last_led_tick_ms = 0;
static void audio_shm_detach(void) {{}}
static void audio_shm_ensure_attached(void) {{}}
static int sys_close(int fd) {{ return fd; }}
static int64_t now_ms(void) {{ return 0; }}
static int alice_owns_leds(int64_t t) {{ (void)t; return 0; }}
static void write_sysfs_int(const char *path, int val) {{ (void)path; (void)val; }}
static void str_copy(char *dst, const char *src, unsigned long max) {{
    unsigned long i = 0; for (; src[i] && i + 1 < max; i++) dst[i] = src[i]; dst[i] = 0;
}}
static void build_led_path(char *dst, unsigned long max, const char *color, int zone_idx) {{
    (void)color; (void)zone_idx;
    snprintf(dst, max, "/sys/class/leds/cucumber:red:ring0/brightness");
}}
static char g_trig[32];
static int g_trig_writes = 0;
static void write_sysfs_str(const char *path, const char *str) {{
    (void)path;
    snprintf(g_trig, sizeof(g_trig), "%s", str);
    g_trig_writes++;
}}

{reset_fn}
{off_fn}

int main(void) {{
    // Midi: zero-indexed ring, kernel trigger ledfb-ring.
    g_led_hw.detected = 1; g_led_hw.is_zero_indexed = 1; g_led_hw.is_plum = 0;
    g_led_hw.num_zones = 24; snprintf(g_led_hw.prefix, sizeof(g_led_hw.prefix), "cucumber:");
    led_all_off();
    if (g_trig_writes == 0) {{ fprintf(stderr, "MIDI: no trigger reattached"); return 21; }}
    if (!strstr(g_trig, "ledfb-ring")) {{ fprintf(stderr, "MIDI: wrong trigger [%s]", g_trig); return 22; }}

    // Plum keeps its own trigger name, as upstream has it.
    g_trig_writes = 0; g_trig[0] = 0;
    g_led_hw.is_zero_indexed = 0; g_led_hw.is_plum = 1;
    led_all_off();
    if (!strstr(g_trig, "led_ring_fb")) {{ fprintf(stderr, "PLUM: wrong trigger [%s]", g_trig); return 23; }}

    // A 1-indexed device that is not plum must stay untouched, as upstream has it.
    g_trig_writes = 0; g_trig[0] = 0;
    g_led_hw.is_zero_indexed = 0; g_led_hw.is_plum = 0;
    led_all_off();
    if (g_trig_writes != 0) {{ fprintf(stderr, "OTHER: wrote trigger [%s]", g_trig); return 24; }}
    return 0;
}}
''', encoding="utf-8")
    subprocess.check_call(["cc", "-O2", "-Wall", "-Wextra", str(probe), "-o", str(bin_p)])
    rc = subprocess.run([str(bin_p)]).returncode
    check(rc == 0, f"led_all_off reattaches the right trigger per hardware (rc={rc})")

if failures == 0:
    print("ALL LED & CACHE TESTS PASSED!", flush=True)
    sys.exit(0)
else:
    print(f"FAILED {failures} checks", flush=True)
    sys.exit(1)
