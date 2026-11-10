#!/usr/bin/env python3
"""Host tests for bidirectional Quasar <-> Soloist volume synchronization in src/soloist-bridge.c."""
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
bridge_src = (ROOT / "src/soloist-bridge.c").read_text(encoding="utf-8")
voice_hdr = (ROOT / "src/voice-request.h").read_text(encoding="utf-8")

helpers_start = bridge_src.index("static unsigned long format_uint_ascii")
helpers_end = bridge_src.index("static int is_yam_mode = 0;")
helpers_code = bridge_src[helpers_start:helpers_end]

state_start = bridge_src.index("static int quasar_vol = -1;")
state_end = bridge_src.index("static char last_auto_next_uri")
state_code = bridge_src[state_start:state_end]

ws_handler_start = bridge_src.index("static void handle_soloist_ws_json")
ws_handler_end = bridge_src.index("    char status_str[32];")
# Close handle_soloist_ws_json early for focused volume state-machine testing
ws_handler_code = (
    bridge_src[ws_handler_start:ws_handler_end]
    .replace("extract_track_info(json);\n", "")
    + "}\n"
)

prefix = r"""
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

static int64_t mock_now = 10000;
static int64_t now_ms(void) { return mock_now; }
static long sys_write(int fd, const void *buf, unsigned long count) { return write(fd, buf, count); }
static unsigned long str_len(const char *s) { (void)s; return s ? strlen(s) : 0; }
static int str_equal(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

static int json_find_str(const char *json, const char *key, char *out, unsigned long max_out) {
    if (!json || !key || !out || max_out == 0) return 0;
    unsigned long klen = strlen(key);
    for (unsigned long i = 0; json[i]; i++) {
        if (json[i] == '"' && memcmp(json + i + 1, key, klen) == 0 && json[i + 1 + klen] == '"') {
            unsigned long idx = i + 1 + klen + 1;
            while (json[idx] == ' ' || json[idx] == ':') idx++;
            if (json[idx] == '"') {
                idx++;
                unsigned long o = 0;
                while (json[idx] && json[idx] != '"' && o + 1 < max_out) out[o++] = json[idx++];
                out[o] = 0;
                return 1;
            }
        }
    }
    return 0;
}

static int json_find_int(const char *json, const char *key, int *val) {
    if (!json || !key || !val) return 0;
    unsigned long klen = strlen(key);
    for (unsigned long i = 0; json[i]; i++) {
        if (json[i] == '"' && memcmp(json + i + 1, key, klen) == 0 && json[i + 1 + klen] == '"') {
            unsigned long idx = i + 1 + klen + 1;
            while (json[idx] == ' ' || json[idx] == ':') idx++;
            int sign = 1;
            if (json[idx] == '-') { sign = -1; idx++; }
            if (json[idx] >= '0' && json[idx] <= '9') {
                int v = 0;
                while (json[idx] >= '0' && json[idx] <= '9') { v = v * 10 + (json[idx] - '0'); idx++; }
                *val = sign * v;
                return 1;
            }
        }
    }
    return 0;
}
"""

# Pull only parse_varint & find_field from voice-request.h
vh_lines = voice_hdr.splitlines()
vh_subset = "\n".join(vh_lines[3 : vh_lines.index("/* Firmware supports direct AliceState and a typed field-252 envelope. */")])

test_driver = r"""
static uint32_t f_to_u32(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void reset_vol_state(void) {
    quasar_vol = -1;
    quasar_vol_known = 0;
    last_quasar_reported_vol = -1;
    current_soloist_vol = -1;
    ws_initial_vol_synced = 0;
    need_soloist_vol_sync = 0;
    pending_quasar_vol = -1;
    pending_quasar_vol_due_ms = 0;
    last_sent_to_quasar_vol = -1;
    last_sent_to_quasar_ms = 0;
    last_sent_to_soloist_vol = -1;
    last_sent_to_soloist_ms = 0;
    last_user_ws_vol_ms = 0;
}

int main(void) {
    int fails = 0;
    #define CHECK(cond, msg) do { \
        if (!(cond)) { printf("FAIL %s\n", msg); fails++; } \
        else { printf("PASS %s\n", msg); } \
    } while (0)

    // 1. IEEE-754 float<->int round-trip 0..100
    int roundtrip_ok = 1;
    for (int v = 0; v <= 100; v++) {
        uint32_t bits = int_to_f32_bits(v);
        uint32_t expected = (v == 0) ? 0u : f_to_u32((float)v);
        if (bits != expected || f32_bits_to_int(bits) != v) {
            roundtrip_ok = 0;
            break;
        }
    }
    CHECK(roundtrip_ok, "int_to_f32_bits and f32_bits_to_int exact for 0..100");
    CHECK(f32_bits_to_int(f_to_u32(0.49f)) == 0 && f32_bits_to_int(f_to_u32(0.50f)) == 1 &&
          f32_bits_to_int(f_to_u32(19.6f)) == 20 && f32_bits_to_int(f_to_u32(-5.0f)) == 0 &&
          f32_bits_to_int(f_to_u32(150.0f)) == 100,
          "f32_bits_to_int rounding and clamping");

    // 2. parse_quasar_vol_frame (direct 198 + shared 252/253 + muted + unrelated)
    // Frame: 198 { 1 { 11 { 3:0, 6:fixed32(25.0f), 7:1 } } }
    uint32_t b25 = int_to_f32_bits(25);
    uint8_t sp_frame[] = {
        0xb2, 0x0c, 13, // tag 198 wire 2, len 13
        0x0a, 11,       // field 1 (state), len 11
        0x5a, 9,        // field 11 (VolumeManager.State), len 9
        0x18, 0x00,     // field 3 (is_muted = 0)
        0x35, (uint8_t)(b25 & 0xff), (uint8_t)(b25 >> 8), (uint8_t)(b25 >> 16), (uint8_t)(b25 >> 24), // field 6 (percent_volume)
        0x38, 0x01      // field 7 (source = 1 Button)
    };
    int qvol = -1; uint64_t qsrc = 999;
    CHECK(parse_quasar_vol_frame(sp_frame, sizeof(sp_frame), &qvol, &qsrc) == 1 && qvol == 25 && qsrc == 1,
          "parse_quasar_vol_frame direct field 198");

    sp_frame[8] = 0x01; // is_muted = 1
    CHECK(parse_quasar_vol_frame(sp_frame, sizeof(sp_frame), &qvol, &qsrc) == 1 && qvol == 0,
          "parse_quasar_vol_frame respects is_muted=1");

    // SharedMessage wrapper: 252 { 1 { 11 { 3:0, 6:25.0f, 7:0 } } }, 253 = 198
    uint8_t sp_wrapped_ok[] = {
        0xe2, 0x0f, 13,
        0x0a, 11, 0x5a, 9, 0x18, 0x00,
        0x35, (uint8_t)(b25 & 0xff), (uint8_t)(b25 >> 8), (uint8_t)(b25 >> 16), (uint8_t)(b25 >> 24),
        0x38, 0x00,
        0xe8, 0x0f, 0xc6, 0x01 // field 253 = 198
    };
    CHECK(parse_quasar_vol_frame(sp_wrapped_ok, sizeof(sp_wrapped_ok), &qvol, &qsrc) == 1 && qvol == 25 && qsrc == 0,
          "parse_quasar_vol_frame SharedMessage 252/253");

    uint8_t sp_unrelated[] = { 0xb2, 0x0c, 4, 0x0a, 2, 0x08, 0x01 };
    CHECK(parse_quasar_vol_frame(sp_unrelated, sizeof(sp_unrelated), &qvol, &qsrc) == 0,
          "parse_quasar_vol_frame ignores StereoPair frame without VolumeManager.State");

    // 3. send_quasar_volume binary wire packet check
    int pfd[2];
    pipe(pfd);
    CHECK(send_quasar_volume(pfd[1], 35) == 0, "send_quasar_volume writes packet");
    uint8_t qpkt[64] = {0};
    ssize_t qn = read(pfd[0], qpkt, sizeof(qpkt));
    uint32_t b35 = int_to_f32_bits(35);
    CHECK(qn == 57 && qpkt[0] == 53 && qpkt[4] == 0xc2 && qpkt[5] == 0x05 &&
          qpkt[47] == 0x0d && memcmp(qpkt + 48, &b35, 4) == 0 &&
          qpkt[54] == 0x18 && qpkt[55] == 0xe8 && qpkt[56] == 0x07,
          "send_quasar_volume wire format matches IOControlCommand ExternalDirectives volume_set 35% ESourceOther");
    close(pfd[0]); close(pfd[1]);

    // 4. send_soloist_ws_volume masked WebSocket text frame
    pipe(pfd);
    CHECK(send_soloist_ws_volume(pfd[1], 45) == 0, "send_soloist_ws_volume writes frame");
    uint8_t wpkt[128] = {0};
    ssize_t wn = read(pfd[0], wpkt, sizeof(wpkt));
    int wlen = wpkt[1] & 0x7f;
    char unmasked[128] = {0};
    for (int i = 0; i < wlen; i++) unmasked[i] = (char)(wpkt[6 + i] ^ wpkt[2 + (i & 3)]);
    CHECK(wn == 6 + wlen && wpkt[0] == 0x81 && (wpkt[1] & 0x80) != 0 &&
          strcmp(unmasked, "{\"type\":\"command\",\"command\":\"set_volume\",\"volume\":45}") == 0,
          "send_soloist_ws_volume produces valid masked WS text command");
    close(pfd[0]); close(pfd[1]);

    // 5. Boot order A: Quasar reports 10%, then Soloist WS playback_state reports 100%
    reset_vol_state();
    mock_now = 10000;
    on_quasar_volume_state(10, 1000, mock_now);
    need_soloist_vol_sync = 0; // suppose WS wasn't connected yet
    handle_soloist_ws_json("{\"type\":\"playback_state\",\"status\":\"paused\",\"volume\":100}");
    CHECK(pending_quasar_vol == -1 && need_soloist_vol_sync == 1 && quasar_vol == 10,
          "Boot A: Soloist playback_state=100 never overwrites Quasar=10, schedules WS sync to 10");

    // 6. Boot order B: Soloist playback_state=100 arrives before Quasar, then Quasar connects with 5%
    reset_vol_state();
    mock_now = 10000;
    handle_soloist_ws_json("{\"type\":\"playback_state\",\"status\":\"paused\",\"volume\":100}");
    on_quasar_volume_state(5, 0, mock_now);
    CHECK(pending_quasar_vol == -1 && need_soloist_vol_sync == 1 && quasar_vol == 5,
          "Boot B: Quasar init=5 after playback_state=100 syncs WS to 5");

    // 7. Spotify slider drag + trailing debounce + Quasar rounded echo suppression
    reset_vol_state();
    mock_now = 20000;
    on_quasar_volume_state(20, 0, mock_now);
    current_soloist_vol = 20;
    ws_initial_vol_synced = 1;
    need_soloist_vol_sync = 0;

    // Simulate main loop after first change (sent at t=20000)
    last_sent_to_quasar_vol = 25;
    last_sent_to_quasar_ms = 20000;
    // Rapid drag at t=20010 (30%) then t=20030 (37%)
    mock_now = 20010;
    handle_soloist_ws_json("{\"type\":\"volume_changed\",\"volume\":30}");
    mock_now = 20030;
    handle_soloist_ws_json("{\"type\":\"volume_changed\",\"volume\":37}");
    CHECK(pending_quasar_vol == 37 && pending_quasar_vol_due_ms == 20060,
          "Spotify drag updates pending_quasar_vol trailing target and rate-limits to +60ms");

    // Simulate Quasar flush & quantized echo (37% -> Quasar rounds to 35% ESourceOther=1000)
    pending_quasar_vol = -1;
    last_sent_to_quasar_vol = 37;
    last_sent_to_quasar_ms = 20060;
    mock_now = 20100;
    on_quasar_volume_state(35, 1000, mock_now);
    CHECK(need_soloist_vol_sync == 0 && quasar_vol == 35,
          "Quantized Quasar echo (37% -> 35% source=1000) does not yank Spotify slider back");

    // 8. Hardware Button / Alice Voice change syncs Spotify + Soloist echo suppression
    mock_now = 20500;
    on_quasar_volume_state(40, 1 /* Button */, mock_now);
    CHECK(need_soloist_vol_sync == 1 && quasar_vol == 40,
          "Speaker button change (35 -> 40, source=Button) triggers Soloist WS sync");
    need_soloist_vol_sync = 0;
    last_sent_to_soloist_vol = 40;
    last_sent_to_soloist_ms = 20500;
    mock_now = 20550;
    handle_soloist_ws_json("{\"type\":\"volume_changed\",\"volume\":40}");
    CHECK(pending_quasar_vol == -1,
          "Soloist confirmation echo (40) does not ping-pong back to Quasar");

    // 9. Delayed Yandex App volume change (source=1000 after >1200ms) syncs Spotify
    mock_now = 23000;
    on_quasar_volume_state(60, 1000, mock_now);
    CHECK(need_soloist_vol_sync == 1 && quasar_vol == 60,
          "External Yandex App change (source=1000 after echo window) syncs to Soloist");

    return fails ? 1 : 0;
}
"""

code = prefix + vh_subset + helpers_code + state_code + ws_handler_code + test_driver
with tempfile.TemporaryDirectory(prefix="soloist-vol-test-") as tmp:
    src = pathlib.Path(tmp) / "test.c"
    exe = pathlib.Path(tmp) / "test"
    src.write_text(code, encoding="utf-8")
    subprocess.check_call(["gcc", "-Wall", "-Wextra", "-O2", str(src), "-o", str(exe)])
    res = subprocess.run([str(exe)])
    sys.exit(res.returncode)
