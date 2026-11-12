#!/usr/bin/env python3
"""Host unit & LD_PRELOAD integration tests for 'What is playing' TTS and PCM ducking."""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BRIDGE_SRC = (ROOT / "src" / "soloist-bridge.c").read_text(encoding="utf-8")
DUCK_SRC = ROOT / "src" / "soloist_duck.c"


def slice_between(src: str, start_marker: str, end_marker: str) -> str:
    s = src.find(start_marker)
    assert s != -1, f"Start marker not found: {start_marker}"
    e = src.find(end_marker, s)
    assert e != -1, f"End marker not found: {end_marker}"
    return src[s:e]


def run_bridge_tts_and_parser_tests() -> None:
    text_and_json_helpers = slice_between(
        BRIDGE_SRC,
        "static void to_lower_ru(",
        "static int spotify_is_active = 0;",
    )
    track_parser = slice_between(
        BRIDGE_SRC,
        "static void append_artist_name(",
        "static void handle_soloist_ws_json(",
    )
    intent_and_voice = slice_between(
        BRIDGE_SRC,
        "static int is_web_or_culinary_intent(",
        "/* =========================================================================",
    )
    tts_func = slice_between(
        BRIDGE_SRC,
        "static int send_alice_tts(",
        "static void trigger_alice_suppression(",
    )

    test_c = f'''
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "duck_shm.h"

static void log_msg(const char *s) {{ (void)s; }}
static unsigned long str_len(const char *s) {{ return s ? strlen(s) : 0; }}
static int64_t mock_now = 50000;
static int64_t now_ms(void) {{ return mock_now; }}

static uint8_t written_frames[8192];
static size_t written_len = 0;
static long sys_write(int fd, const void *buf, unsigned long count) {{
    (void)fd;
    if (written_len + count <= sizeof(written_frames)) {{
        memcpy(written_frames + written_len, buf, count);
        written_len += count;
    }}
    return (long)count;
}}
static int sys_open(const char *p, int f, int m) {{ (void)p;(void)f;(void)m; return -1; }}
static void sys_close(int fd) {{ (void)fd; }}
static int sys_unlink(const char *p) {{ (void)p; return 0; }}

static int cancel_calls = 0;
static int suppressed_calls = 0;
static int duck_active_state = 0;
static int yield_active_state = 0;
static int64_t g_tts_hold_until_ms = 0;
static int g_tts_speaking_seen = 0;
static int quasar_uses_f252 = -1;
static int64_t cancel_alice_until_ms = 0;
static int spotify_is_active = 0;
static int playback_status = 0;
static char current_track_name[128] = {{0}};
static char current_artist_name[128] = {{0}};

static void sys_msleep(long ms) {{ mock_now += ms; }}
static void send_cancel_alice(int fd) {{ (void)fd; cancel_calls++; }}
static void set_ducking(int active, int64_t now) {{ (void)now; duck_active_state = active; }}
static void set_alsa_yield(int enable, int64_t now) {{ (void)now; yield_active_state = enable; }}
static void trigger_alice_suppression(int fd, int ms) {{
    (void)fd; suppressed_calls++; g_tts_hold_until_ms = 0; cancel_alice_until_ms = mock_now + ms; set_ducking(0, mock_now); set_alsa_yield(0, mock_now);
}}
static void set_led_visualizer(int enable) {{ (void)enable; }}
static void run_soloist_cmd(const char *a) {{ (void)a; }}
static void run_soloist_cmd2(const char *a, const char *b) {{ (void)a; (void)b; }}
static void run_toggle(void) {{}}
static void run_search_and_play(const char *q) {{ (void)q; }}
static const char *get_censor_path(void) {{ return "/tmp/none"; }}

{text_and_json_helpers}
{track_parser}
{tts_func}
{intent_and_voice}

#define CHECK(cond, msg) do {{ if (!(cond)) {{ fprintf(stderr, "FAIL: %s\\n", msg); return 1; }} }} while (0)

int main(void) {{
    // 1. Real Soloist JSON: parent (album) precedes creators (artists)
    const char *ws_json =
        "{{\\"type\\":\\"playback_state\\",\\"status\\":\\"playing\\","
        "\\"item\\":{{\\"uri\\":\\"spotify:track:123\\","
        "\\"decorations\\":{{\\"identity\\":{{\\"name\\":\\"Song \\\\\\"Live\\\\\\"\\"}},"
        "\\"parent\\":{{\\"entity\\":{{\\"decorations\\":{{\\"identity\\":{{\\"name\\":\\"AlbumOnly\\"}}}}}}}},"
        "\\"creators\\":[{{\\"entity\\":{{\\"decorations\\":{{\\"identity\\":{{\\"name\\":\\"ArtistOne\\"}}}}}}}},"
        "{{\\"entity\\":{{\\"decorations\\":{{\\"identity\\":{{\\"name\\":\\"ArtistTwo\\"}}}}}}}}]}}}},"
        "\\"context\\":{{\\"decorations\\":{{\\"identity\\":{{\\"name\\":\\"MyPlaylist\\"}}}}}}}}";

    extract_track_info(ws_json);
    CHECK(strcmp(current_track_name, "Song \\"Live\\"") == 0, "extract track unescapes quotes and ignores parent/context");
    CHECK(strcmp(current_artist_name, "ArtistOne, ArtistTwo") == 0, "extract artist reads creators array, not parent album");

    // 2. track_changed without creators clears stale artist
    extract_track_info("{{\\"type\\":\\"track_changed\\",\\"item\\":{{\\"decorations\\":{{\\"identity\\":{{\\"name\\":\\"Instrumentals\\"}},\\"creators\\":[]}}}}}}");
    CHECK(strcmp(current_track_name, "Instrumentals") == 0, "track_changed updates track");
    CHECK(current_artist_name[0] == 0, "empty creators clears stale artist");

    // 3. playback_changed preserves track and artist
    extract_track_info(ws_json);
    extract_track_info("{{\\"type\\":\\"playback_changed\\",\\"status\\":\\"paused\\"}}");
    CHECK(strcmp(current_track_name, "Song \\"Live\\"") == 0, "playback_changed preserves track");
    CHECK(strcmp(current_artist_name, "ArtistOne, ArtistTwo") == 0, "playback_changed preserves artist");

    // 4. item:null clears metadata
    extract_track_info("{{\\"type\\":\\"playback_state\\",\\"item\\":null}}");
    CHECK(current_track_name[0] == 0 && current_artist_name[0] == 0, "item:null clears track metadata");

    // 5. Voice TTS when playing ("что сейчас играет")
    extract_track_info(ws_json);
    playback_status = 1;
    cancel_calls = 0; suppressed_calls = 0; duck_active_state = 0; cancel_alice_until_ms = 999999;
    written_len = 0;
    const char *q1 = "что сейчас играет";
    handle_voice_recognition(1, q1, strlen(q1));
    CHECK(cancel_calls == 1, "what-is-playing sends single cloud cancel");
    CHECK(suppressed_calls == 0, "what-is-playing does not arm suppression window");
    CHECK(cancel_alice_until_ms == 0, "send_alice_tts clears suppression timer so own TTS is not killed");
    CHECK(duck_active_state == 1, "send_alice_tts engages ducking");
    CHECK(yield_active_state == 1, "send_alice_tts engages ALSA yield for 32-bit Alice TTS");
    CHECK(g_tts_hold_until_ms > mock_now, "send_alice_tts holds yield across transient cancelled IDLE");
    CHECK(written_len > 20, "START_VOCALIZE frame written");
    CHECK(memmem(written_frames, written_len, "AliceCapability", 15) != NULL, "frame targets AliceCapability");
    const char *exp_play = "Сейчас играет Song \\"Live\\", исполнитель ArtistOne, ArtistTwo";
    CHECK(memmem(written_frames, written_len, exp_play, strlen(exp_play)) != NULL,
          "TTS announces playing track and artists");
    // Ensure obsolete field 3 (tag 0x18 0x01) is not appended after Interrupt (0x10 0x02)
    CHECK(written_frames[4] == 0x92 && written_frames[5] == 0x0d,
          "direct Quasar daemon receives field 210 remoting frame");
    CHECK(written_frames[written_len - 2] == 0x10 && written_frames[written_len - 1] == 0x02,
          "TVocalizeDirective ends cleanly with SynthesisMode=Interrupt (2)");

    // 5b. SharedMessage 252/253=210 framing for newer/alternate Quasar daemons (e.g. YNDX-0004)
    quasar_uses_f252 = 1;
    written_len = 0;
    handle_voice_recognition(1, "какая музыка играет", strlen("какая музыка играет"));
    CHECK(written_len > 24, "SharedMessage START_VOCALIZE frame written");
    CHECK(written_frames[4] == 0xe2 && written_frames[5] == 0x0f,
          "SharedMessage uses tag 252 envelope");
    CHECK(written_frames[written_len - 4] == 0xe8 && written_frames[written_len - 3] == 0x0f &&
          written_frames[written_len - 2] == 0xd2 && written_frames[written_len - 1] == 0x01,
          "SharedMessage ends with tag 253 = 210");
    quasar_uses_f252 = -1;

    // 6. Voice TTS when paused ("кто поёт")
    playback_status = 2;
    written_len = 0;
    const char *q2 = "кто поёт";
    handle_voice_recognition(1, q2, strlen(q2));
    const char *exp_pause = "На паузе Song \\"Live\\", исполнитель ArtistOne, ArtistTwo";
    CHECK(memmem(written_frames, written_len, exp_pause, strlen(exp_pause)) != NULL,
          "paused track query with yo announces paused track");

    // 7. Voice TTS when stopped
    playback_status = 0;
    written_len = 0;
    handle_voice_recognition(1, q1, strlen(q1));
    const char *exp_idle = "Сейчас ничего не играет";
    CHECK(memmem(written_frames, written_len, exp_idle, strlen(exp_idle)) != NULL,
          "stopped playback announces nothing playing");

    // 8. Explicit catalog search stays search even with "что за песня"
    written_len = 0;
    playback_status = 1;
    const char *q3 = "включи песню что сейчас играет";
    handle_voice_recognition(1, q3, strlen(q3));
    CHECK(written_len == 0 && suppressed_calls == 1, "explicit search is not hijacked by TTS info handler");

    printf("PASS: track parser & TTS voice checks all passed\\n");
    return 0;
}}
'''
    with tempfile.TemporaryDirectory() as td:
        src_path = pathlib.Path(td) / "test_tts.c"
        bin_path = pathlib.Path(td) / "test_tts"
        src_path.write_text(test_c, encoding="utf-8")
        comp = subprocess.run(
            ["cc", "-O2", "-Wall", "-Wextra", f"-I{ROOT / 'src'}", str(src_path), "-o", str(bin_path)],
            capture_output=True,
            text=True,
        )
        assert comp.returncode == 0, f"test_tts compile failed:\n{comp.stderr}"
        run = subprocess.run([str(bin_path)], capture_output=True, text=True)
        print(run.stdout, end="")
        if run.returncode != 0:
            print(run.stderr, file=sys.stderr)
            raise SystemExit(run.returncode)


def run_duck_preload_tests() -> None:
    mock_alsa_c = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>

uint8_t g_captured[262144];
size_t g_captured_len = 0;

void reset_capture(void) { g_captured_len = 0; }

int snd_pcm_hw_params_set_format(void *pcm, void *params, int val) {
    (void)pcm; (void)params; (void)val; return 0;
}
int snd_pcm_hw_params_set_channels(void *pcm, void *params, unsigned int val) {
    (void)pcm; (void)params; (void)val; return 0;
}
int snd_pcm_hw_params_set_rate(void *pcm, void *params, unsigned int val, int dir) {
    (void)pcm; (void)params; (void)val; (void)dir; return 0;
}
int snd_pcm_hw_params_set_rate_near(void *pcm, void *params, unsigned int *val, int *dir) {
    (void)pcm; (void)params; (void)val; (void)dir; return 0;
}
long snd_pcm_writei(void *pcm, const void *buffer, unsigned long size) {
    (void)pcm;
    size_t bytes = size * 8; // Upper bound cap for test capture; driver compares exact written slice
    if (buffer && size > 0 && g_captured_len + bytes <= sizeof(g_captured)) {
        memcpy(g_captured + g_captured_len, buffer, bytes);
        g_captured_len += bytes;
    }
    return (long)size;
}
'''

    host_driver_c = r'''
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "duck_shm.h"

extern int snd_pcm_hw_params_set_format(void *pcm, void *params, int val);
extern int snd_pcm_hw_params_set_channels(void *pcm, void *params, unsigned int val);
extern int snd_pcm_hw_params_set_rate_near(void *pcm, void *params, unsigned int *val, int *dir);
extern long snd_pcm_writei(void *pcm, const void *buffer, unsigned long size);
extern uint8_t g_captured[];
extern void reset_capture(void);

static int64_t now_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

int main(void) {
    const char *shm_path = getenv("SOLOIST_DUCK_PATH");
    CHECK(shm_path != NULL, "SOLOIST_DUCK_PATH set");
    int fd = open(shm_path, O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0 && ftruncate(fd, SOLOIST_DUCK_SHM_SIZE) == 0, "create duck shm");
    struct soloist_duck_shm *shm = mmap(NULL, SOLOIST_DUCK_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    CHECK(shm != MAP_FAILED, "mmap shm");
    shm->magic = SOLOIST_DUCK_MAGIC;
    shm->active = 0;
    shm->until_ms = 0;

    // A. ALSA FLOAT_LE (14) 48kHz stereo test
    void *pcm_f = (void *)0x1111;
    unsigned int rate = 48000;
    int dir = 0;
    CHECK(snd_pcm_hw_params_set_format(pcm_f, NULL, 14) == 0, "set FLOAT_LE format");
    CHECK(snd_pcm_hw_params_set_channels(pcm_f, NULL, 2) == 0, "set stereo channels");
    CHECK(snd_pcm_hw_params_set_rate_near(pcm_f, NULL, &rate, &dir) == 0, "set 48kHz rate");

    float src_f[9600]; // 4800 frames = 100ms
    for (int i = 0; i < 9600; i++) src_f[i] = 1.0f;

    // 1. Idle passthrough
    reset_capture();
    CHECK(snd_pcm_writei(pcm_f, src_f, 4800) == 4800, "idle writei");
    const float *cap_f = (const float *)g_captured;
    CHECK(fabsf(cap_f[0] - 1.0f) < 1e-5f && fabsf(cap_f[9599] - 1.0f) < 1e-5f, "idle passthrough unity gain");

    // 2. Duck active: 80ms attack (3840 frames) down to 0.20, stereo symmetry
    shm->until_ms = now_mono_ms() + 60000;
    __atomic_store_n(&shm->active, 1, __ATOMIC_RELEASE);
    reset_capture();
    CHECK(snd_pcm_writei(pcm_f, src_f, 4800) == 4800, "duck writei");
    CHECK(fabsf(src_f[0] - 1.0f) < 1e-6f, "input caller buffer stays untouched");
    CHECK(cap_f[0] < 1.0f && cap_f[0] > 0.99f, "attack begins ramping down smoothly");
    CHECK(fabsf(cap_f[0] - cap_f[1]) < 1e-6f, "L and R channels maintain stereo gain equality");
    for (int f = 1; f < 4000; f++) {
        CHECK(cap_f[f * 2] <= cap_f[(f - 1) * 2] + 1e-6f, "monotonic attack descent");
    }
    CHECK(fabsf(cap_f[8000] - 0.20f) < 1e-4f && fabsf(cap_f[9599] - 0.20f) < 1e-4f, "settles smoothly at 20% duck level");

    // 3. Release back to 1.0 over 300ms (4 x 100ms chunks = 400ms total)
    __atomic_store_n(&shm->active, 0, __ATOMIC_RELEASE);
    for (int c = 0; c < 4; c++) {
        reset_capture();
        snd_pcm_writei(pcm_f, src_f, 4800);
    }
    CHECK(fabsf(cap_f[9598] - 1.0f) < 1e-4f, "recovers back to 100% after release");

    // 4. S16_LE (2) test + expired deadline fail-safe
    void *pcm16 = (void *)0x2222;
    CHECK(snd_pcm_hw_params_set_format(pcm16, NULL, 2) == 0, "set S16_LE format");
    CHECK(snd_pcm_hw_params_set_channels(pcm16, NULL, 2) == 0, "set S16_LE stereo");
    int16_t src16[9600]; // 4800 frames = 100ms
    for (int i = 0; i < 9600; i++) src16[i] = 10000;
    shm->until_ms = now_mono_ms() + 60000;
    __atomic_store_n(&shm->active, 1, __ATOMIC_RELEASE);
    reset_capture();
    CHECK(snd_pcm_writei(pcm16, src16, 4800) == 4800, "S16_LE duck writei");
    const int16_t *cap16 = (const int16_t *)g_captured;
    CHECK(cap16[9598] >= 1995 && cap16[9598] <= 2005, "S16_LE settles accurately at 20% (2000 of 10000)");

    // Safety expiry with active=1 recovers to 100%
    shm->until_ms = now_mono_ms() - 1;
    for (int c = 0; c < 4; c++) {
        reset_capture();
        snd_pcm_writei(pcm16, src16, 4800);
    }
    CHECK(cap16[9598] == 10000, "expired deadline restores 100% gain even if active flag stuck");

    printf("PASS: LD_PRELOAD ALSA PCM ducking tests (FLOAT_LE + S16_LE + expiry) passed\n");
    return 0;
}
'''

    with tempfile.TemporaryDirectory() as td:
        td_path = pathlib.Path(td)
        mock_src = td_path / "mock_alsa.c"
        mock_so = td_path / "libmock_alsa.so"
        duck_so = td_path / "libsoloist_duck.so"
        drv_src = td_path / "driver.c"
        drv_bin = td_path / "driver"
        shm_file = td_path / "duck_shm"

        mock_src.write_text(mock_alsa_c, encoding="utf-8")
        drv_src.write_text(host_driver_c, encoding="utf-8")

        subprocess.check_call(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", str(mock_src), "-o", str(mock_so)])
        subprocess.check_call(
            ["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", f"-I{ROOT / 'src'}", str(DUCK_SRC), "-ldl", "-lpthread", "-o", str(duck_so)]
        )
        subprocess.check_call(
            ["cc", "-O2", "-Wall", "-Wextra", f"-I{ROOT / 'src'}", str(drv_src), str(mock_so), f"-Wl,-rpath,{td}", "-lm", "-o", str(drv_bin)]
        )

        env = os.environ.copy()
        env["LD_PRELOAD"] = str(duck_so)
        env["SOLOIST_DUCK_PATH"] = str(shm_file)
        res = subprocess.run([str(drv_bin)], env=env, capture_output=True, text=True)
        print(res.stdout, end="")
        if res.returncode != 0:
            print(res.stderr, file=sys.stderr)
            raise SystemExit(res.returncode)


if __name__ == "__main__":
    run_bridge_tts_and_parser_tests()
    run_duck_preload_tests()
