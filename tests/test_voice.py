"""Compile actual production parsing/state code with a tiny host fixture driver."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/soloist-bridge.c").read_text(encoding="utf-8")
text_hdr = (ROOT / "src/text-json.h").read_text(encoding="utf-8")
qipc_hdr = (ROOT / "src/quasar-ipc.h").read_text(encoding="utf-8")
qvol_hdr = (ROOT / "src/quasar-volume.h").read_text(encoding="utf-8")

def section(start, end):
    return source[source.index(start):source.index(end)]

prefix = '''#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int count, failures;
static char received[512];
static void handle_voice_recognition(int fd, const char *s, unsigned long n) {
    (void)fd; count++; if(n >= sizeof(received)) n = sizeof(received)-1;
    memcpy(received,s,n); received[n]=0;
}
'''
header = ROOT / "src/voice-request.h"
if header.exists():
    production = '#include "voice-request.h"\nstatic struct voice_request_state cache;\n'
    production += '''static void reset(void) { memset(&cache,0,sizeof(cache)); count=0; }
static void process(const uint8_t *frame_body, unsigned long frame_len) {
    const uint8_t *text=0; unsigned long len=0;
    if (voice_request_read(&cache, frame_body, frame_len, &text, &len))
        handle_voice_recognition(0,(const char *)text,len);
}
'''
else:
    helpers = section("static uint64_t parse_varint", "static void str_copy")
    helpers += section("static int is_word_byte", "static int spotify_is_active")
    # RED adapter: no model of the bug; execute the existing production block.
    block = section("                        const uint8_t *frame_body =", "                        unsigned long total_consumed")
    block = block.replace("const uint8_t *frame_body = quasar_buf + 4;", "")
    block = block.replace("static uint64_t prev_alice_state = 0;", "")
    production = 'static unsigned long str_len(const char *s) { return s ? strlen(s) : 0; }\n' + helpers + '''static char last_uuid[64],last_processed_text[256];
static uint64_t prev_alice_state;
static int64_t now=0,cancel_alice_until_ms=0;
static int quasar_fd=0;
static void send_cancel_alice(int fd) {(void)fd;}
static void reset(void) { memset(last_uuid,0,64); memset(last_processed_text,0,256); prev_alice_state=0; count=0; }
static void process(const uint8_t *frame_body, unsigned long frame_len) {
''' + block + "}\n"

driver = r'''
static void check(int good, const char *name) {
    printf("%s %s\n",good?"PASS":"FAIL",name); if(!good) failures++;
}
static unsigned long vi(uint8_t *b, uint64_t n) {
    unsigned long p=0; do { b[p]=(uint8_t)(n&127); n>>=7; if(n)b[p]|=128; p++; } while(n); return p;
}
static unsigned long bytes(uint8_t *b,unsigned f,const void *s,unsigned long n) {
    unsigned long p=vi(b,((uint64_t)f<<3)|2); p+=vi(b+p,n); memcpy(b+p,s,n); return p+n;
}
static void frame(int state, const char *id, const char *text, int final, int started, int wrapper) {
    uint8_t req[1024],st[2048],out[4096]; unsigned long r=0,s=0,o=0;
    if(state>=0) { st[s++]=8; st[s++]=(uint8_t)state; }
    if(id) s+=bytes(st+s,7,id,strlen(id));
    if(text) {
        r+=bytes(req+r,1,text,strlen(text)); req[r++]=16; req[r++]=(uint8_t)final;
        r+=bytes(req+r,3,"shared-dialog",13); req[r++]=32; req[r++]=(uint8_t)started;
        s+=bytes(st+s,15,req,r);
    }
    o+=bytes(out+o,wrapper?252:3,st,s);
    if(wrapper) {o+=vi(out+o,253*8);o+=vi(out+o,wrapper);}
    process(out,o);
}
int main(void) {
    reset(); frame(3,"partial","включи песню",0,1,0);
    check(count==0,"PROCESSING partial must wait");
    frame(3,"partial","включи песню Don't Stop Me Now",1,1,0);
    check(count==1 && !strcmp(received,"включи песню Don't Stop Me Now"),"complete final phrase arrives once");
    reset(); frame(1,"ctrl","стоп",0,1,0); check(count==0,"partial control must wait");
    frame(3,"ctrl","стоп",1,1,0); check(count==1,"final control accepted");
    reset(); frame(3,"retained","включи Кино",1,1,0);
    frame(0,"retained","включи Кино",1,1,0); frame(-1,"retained","включи Кино",1,1,0);
    frame(3,"retained","включи Кино",1,1,0); check(count==1,"IDLE and missing state cannot replay retained request");
    reset(); frame(-1,"new","включи Кино",1,1,0); check(count==0,"missing state cannot start a request");
    reset(); frame(3,"abcdef","включи Кино",1,1,0); frame(3,"abc","включи Кино",1,1,0);
    check(count==2,"IDs with common prefix have exact lengths");
    frame(3,"different","включи Кино",1,1,0); check(count==3,"same text with a new ID is a new request");
    frame(3,"abcdef","включи Кино",1,1,0); check(count==3,"recent older ID is not replayed after another request");
    reset(); frame(3,"wrapped","включи Кино",1,1,210); check(count==0,"unrelated wrapped message ignored");
    frame(3,"wrapped","включи Кино",1,1,3); check(count==1,"wrapped type 3 accepted");
    reset(); frame(1,0,"включи",0,1,0); frame(3,0,"включи Кино",1,1,0);
    frame(0,0,"включи Кино",1,1,0); frame(-1,0,"включи Кино",1,1,0);
    check(count==1,"ID-less final deduplicated across idle");
    frame(1,0,"включи",0,1,0); frame(3,0,"включи Кино",1,1,0);
    check(count==2,"new recognition cycle works with reused dialog_id");
    frame(0,0,"включи Кино",1,1,0); frame(1,0,"включи Кино",1,1,0);
    frame(1,0,"включи",0,1,0); frame(3,0,"включи Кино",1,1,0);
    check(count==3,"retained final on entering LISTENING cannot hide the next partial");
    reset(); frame(3,0,"включи Кино",1,1,0); check(count==0,"ID-less retained final without observed cycle is ignored");
    reset(); frame(0,"idle-new","включи Кино",1,1,0); check(count==0,"IDLE never starts playback");
    { char large[600]; memset(large,'a',sizeof(large)); large[599]=0;
      reset(); frame(3,"long-text",large,1,1,0); check(count==0,"oversized phrase rejected instead of truncated");
      large[129]=0; frame(3,large,"включи Кино",1,1,0); check(count==0,"oversized ID rejected instead of text fallback"); }
    reset(); frame(3,"weather","какая погода",1,1,0);
    check(count==1 && !strcmp(received,"какая погода"),"ordinary Alice phrase preserved for existing classifier");
    return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix="soloist-voice-") as temp:
    path = pathlib.Path(temp)
    (path / "test.c").write_text(prefix + production + driver, encoding="utf-8")
    executable = path / ("test.exe" if sys.platform == "win32" else "test")
    compiler = [sys.argv[1], "cc"] if len(sys.argv)>1 else ["cc"]
    subprocess.run(compiler + ["-std=c99", "-Wall", "-Wextra", "-I", str(ROOT / "src"), str(path / "test.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

# Exercise media ownership with the real WS and voice handlers, plus the input
# event body from _start. Keep controller and hardware effects observable.
media_prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "voice-request.h"
static unsigned long str_len(const char *s) { return s ? strlen(s) : 0; }
static int failures, command_calls, search_calls, resume_calls, suppressed_calls, cache_calls, tts_calls;
static char command[32];
static char command_arg[512];
static char last_tts[256];
static int64_t mock_now = 10000;
static int64_t now_ms(void) { return mock_now; }
static void log_msg(const char *s) { (void)s; }
static void trigger_alice_suppression(int fd, int64_t ms) { (void)fd; (void)ms; suppressed_calls++; }
static void run_soloist_cmd(const char *s) { command_calls++; snprintf(command, sizeof(command), "%s", s); command_arg[0] = 0; }
static void run_soloist_cmd2(const char *s, const char *a) {
    command_calls++; snprintf(command, sizeof(command), "%s", s);
    snprintf(command_arg, sizeof(command_arg), "%s", a ? a : "");
}
static void run_toggle(void) { run_soloist_cmd("toggle"); }
static void run_search_and_play(const char *s) {
    if (!strcmp(s, "включи музыку")) resume_calls++;
    else search_calls++;
}
static void run_controller(const char *s, const char *a) {
    if (!strcmp(s, "clean-cache-full")) { cache_calls++; return; }
    command_calls++; snprintf(command, sizeof(command), "%s", s);
    snprintf(command_arg, sizeof(command_arg), "%s", a ? a : "");
}
static void publish_track(const char *s) { (void)s; }
static int selection_needs_observation(void) { return 0; }
static void set_led_visualizer(int enable) { (void)enable; }
static void persist_voice_mode(int mode) { (void)mode; }
static int send_alice_tts(int fd, const char *s) {
    (void)fd;
    tts_calls++;
    snprintf(last_tts, sizeof(last_tts), "%s", s ? s : "");
    return 0;
}
static const char *get_censor_path(void) { return "/tmp/unused"; }
static int censor_on_calls, censor_off_calls;
static int sys_open(const char *p, int f, int m) { (void)p; (void)f; (void)m; censor_on_calls++; return -1; }
static void sys_close(int fd) { (void)fd; }
static int sys_unlink(const char *p) { (void)p; censor_off_calls++; return 0; }
static long sys_write(int fd, const void *b, unsigned long n) { (void)fd; (void)b; return n; }
static int touch_down_start, touch_is_down;
static int64_t touch_down_time, last_touch_release_time, cancel_alice_until_ms;
#define LED_FX_NONE   0
#define LED_FX_PLAY   1
#define LED_FX_NEXT   2
#define LED_FX_PREV   3
#define LED_FX_SEARCH 4
#define LED_FX_ERROR  5
static int g_ws_fx_active = 0;
static int g_ws_play_fx_calls = 0;
static inline void led_fx_trigger(int fx_type) {
    g_ws_fx_active = fx_type;
    if (fx_type == LED_FX_PLAY) g_ws_play_fx_calls++;
}
static inline void led_fx_clear(void) { g_ws_fx_active = 0; }
static inline int led_fx_is_skip_recent(int64_t now) { (void)now; return 0; }
static inline int led_fx_is_play_recent(int64_t now) { (void)now; return 0; }
static inline int led_fx_active_type(void) { return g_ws_fx_active; }
'''
media_production = (
    section("static int quasar_uses_f252 = -1;", '#include "text-json.h"')
    + text_hdr[text_hdr.index("static void to_lower_ru"):text_hdr.index("#endif")]
    + "\n"
    + qvol_hdr[qvol_hdr.index("static int quasar_vol = -1;"):qvol_hdr.index("#endif")]
    + "\n"
    + qipc_hdr[qipc_hdr.index("static int parse_quasar_io_media_state"):qipc_hdr.index("#endif")]
    + "\n"
    + section("static void handle_soloist_ws_json", "#ifdef __aarch64__\nstatic void set_led_visualizer_impl")
)
media_production += "\n" + section("static int is_web_or_culinary_intent", "/* =========================================================================")
input_start = source.index("                            int is_play_pause_key =")
input_end = source.index("\n                        }\n                    } else if (pfd[p_idx].revents", input_start)
media_production += '''
static void input(int code, int value) {
    struct { int type, code, value; } ev = {1, code, value};
    int quasar_fd = 0;
    do {
''' + source[input_start:input_end] + '''
    } while (0);
}
'''
media_driver = r'''
static void check(int good, const char *name) {
    printf("%s %s\n", good ? "PASS" : "FAIL", name); if (!good) failures++;
}
static void clear_calls(void) {
    command_calls = search_calls = resume_calls = suppressed_calls = tts_calls = 0;
    command[0] = 0; command_arg[0] = 0; last_tts[0] = 0;
}
static void reset_media(void) {
    spotify_is_active = spotify_device_is_active = yandex_music_active = playback_status = 0;
    yandex_music_handoff_ms = 0;
    voice_mode = VOICE_MODE_SPOTIFY;
    touch_down_start = touch_is_down = 0; last_touch_release_time = 0;
    censor_on_calls = censor_off_calls = 0;
    clear_calls();
}
static unsigned long pb_vi(uint8_t *b, uint64_t n) {
    unsigned long p = 0; do { b[p] = (uint8_t)(n & 127); n >>= 7; if (n) b[p] |= 128; p++; } while (n); return p;
}
static unsigned long pb_bytes(uint8_t *b, unsigned f, const void *s, unsigned long n) {
    unsigned long p = pb_vi(b, ((uint64_t)f << 3) | 2); p += pb_vi(b + p, n); memcpy(b + p, s, n); return p + n;
}
static unsigned long pb_varint(uint8_t *b, unsigned f, uint64_t v) {
    unsigned long p = pb_vi(b, ((uint64_t)f << 3) | 0); p += pb_vi(b + p, v); return p;
}
static void voice(const char *s) { handle_voice_recognition(0, s, strlen(s)); }
static int passed_to_alice(void) {
    return command_calls == 0 && search_calls == 0 && resume_calls == 0 && suppressed_calls == 0;
}
static int paused_once(void) {
    return command_calls == 1 && !strcmp(command, "pause") && suppressed_calls == 1 && search_calls == 0 && resume_calls == 0;
}
int main(void) {
    for (int initial = 0; initial <= 1; initial++) {
        reset_media(); spotify_is_active = spotify_device_is_active = initial;
        handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":false}");
        voice("стоп");
        check(spotify_is_active == 0 && spotify_device_is_active == 0 && passed_to_alice(),
              initial ? "phone playing clears stale local flags and preserves Alice" : "phone playing cannot claim idle speaker or suppress Alice");
    }
    reset_media(); spotify_is_active = spotify_device_is_active = 1;
    handle_soloist_ws_json("{\"is_active\":false}"); voice("пауза");
    check(spotify_is_active == 0 && spotify_device_is_active == 0 && passed_to_alice(),
          "inactive update without status clears optimism and preserves Alice");

    for (int voice_pause = 0; voice_pause <= 1; voice_pause++) {
        reset_media();
        handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
        if (voice_pause) { voice("пауза"); check(paused_once(), "voice pause invokes Spotify and suppression once"); }
        handle_soloist_ws_json("{\"status\":\"paused\"}");
        clear_calls(); voice("стоп"); voice("пауза");
        check(spotify_is_active == 0 && spotify_device_is_active == 1 && passed_to_alice(),
              voice_pause ? "WS after voice pause leaves STOP and PAUSE to Alice" : "app pause clears optimism and leaves STOP and PAUSE to Alice");
        voice("дальше");
        check(command_calls == 1 && !strcmp(command, "next") && suppressed_calls == 1 && search_calls == 0 && resume_calls == 0,
              "NEXT remains allowed on active paused speaker");
    }
    reset_media();
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    voice("стоп"); check(paused_once(), "local playing allows STOP");
    reset_media();
    handle_soloist_ws_json("{\"status\":\"buffering\",\"is_active\":true}");
    voice("стоп"); check(paused_once(), "local buffering allows STOP without optimism");
    reset_media(); spotify_device_is_active = 1;
    voice("стоп"); check(passed_to_alice(), "active stopped speaker leaves STOP to Alice");

    for (int inactive = 0; inactive <= 1; inactive++) {
        reset_media(); voice("включи песню Кукушка");
        check(search_calls == 1 && resume_calls == 0 && suppressed_calls == 1 && spotify_is_active == 1,
              "catalog search sets optimism before WS");
        clear_calls(); voice("стоп"); check(paused_once(), "optimistic search allows STOP before WS");
        voice("включи песню Кукушка");
        handle_soloist_ws_json(inactive ? "{\"is_active\":false}" : "{\"status\":\"paused\",\"is_active\":true}");
        clear_calls(); voice("стоп");
        check(passed_to_alice(), inactive ? "inactive WS cancels optimistic STOP permission" : "paused WS cancels optimistic STOP permission");
    }
    const char *resume[] = {"продолжи", "включи пожалуйста музыку", "играй", "возобнови", "заиграй", "вруби музыку", "включи музыку"};
    for (unsigned i = 0; i < sizeof(resume) / sizeof(resume[0]); i++) {
        reset_media(); voice(resume[i]);
        check(resume_calls == 1 && search_calls == 0 && command_calls == 0 && suppressed_calls == 1,
              resume[i]);
    }
    const char *censorship[] = {"включи цензуру", "включи детский режим"};
    for (unsigned i = 0; i < sizeof(censorship) / sizeof(censorship[0]); i++) {
        for (int active = 0; active <= 1; active++) {
            reset_media(); spotify_is_active = spotify_device_is_active = active;
            voice(censorship[i]);
            check(censor_on_calls == 1 && censor_off_calls == 0 && search_calls == 0 &&
                  resume_calls == 0 && command_calls == 0,
                  "censorship toggle is not swallowed by explicit-search guard");
        }
    }
    reset_media(); voice("выключи цензуру");
    check(censor_off_calls == 1 && censor_on_calls == 0 && search_calls == 0 && resume_calls == 0,
          "censorship off is not swallowed by explicit-search guard");
    const char *qualified[] = {"включи грустную музыку", "включи музыку Linkin Park", "включи песню Играй",
                               "включи музыку Алиса", "включи музыку Пожалуйста"};
    for (unsigned i = 0; i < sizeof(qualified) / sizeof(qualified[0]); i++) {
        reset_media(); voice(qualified[i]);
        check(search_calls == 1 && resume_calls == 0, "qualified search is not treated as resume");
    }
    reset_media(); voice("включи пожалуйстапесня музыку");
    check(search_calls == 1 && resume_calls == 0, "filler must match a whole word");
    reset_media(); voice("алиса, давай включи пожалуйста музыку");
    check(resume_calls == 1 && search_calls == 0, "leading address and fillers still resume");
    const int keys[] = {163, 159, 208, 165, 158, 168};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        reset_media(); input(keys[i], 1);
        check(command_calls == 1 && !strcmp(command, i < 3 ? "next" : "prev") && suppressed_calls == 1 &&
              search_calls == 0 && resume_calls == 0 && spotify_is_active == 0 && spotify_device_is_active == 0,
              i < 3 ? "hardware NEXT sends command without claiming local playback" : "hardware PREV sends command without claiming local playback");
    }
    const int taps[] = {164, 0x246};
    for (unsigned i = 0; i < sizeof(taps) / sizeof(taps[0]); i++) {
        reset_media(); playback_status = 2; input(taps[i], 1); input(taps[i], 0);
        check(command_calls == 1 && !strcmp(command, "play") && suppressed_calls == 2 &&
              search_calls == 0 && resume_calls == 0 && spotify_is_active == 0 && spotify_device_is_active == 0,
              "button or touch resume sends play without claiming local playback");
    }
    reset_media(); playback_status = 2; last_touch_release_time = mock_now - 100;
    input(0x246, 1); input(0x246, 0);
    check(command_calls == 1 && !strcmp(command, "next") && suppressed_calls == 2 && spotify_is_active == 0,
          "double tap sends NEXT without claiming local playback");
    reset_media(); playback_status = 1; spotify_is_active = 1;
    input(164, 1); input(164, 0);
    check(command_calls == 1 && !strcmp(command, "pause") && suppressed_calls == 2 && spotify_is_active == 0,
          "hardware pause retains optimistic reset");
    cache_calls = 0; handle_soloist_ws_json("{\"status\":\"stopped\"}");
    check(playback_status == 0 && spotify_is_active == 0 && cache_calls == 1 && last_cache_clean_ms == mock_now,
          "stopped WS retains full cache cleanup");

    // Issue #33: handing playback to Yandex Music yields NEXT/PREV/SHUFFLE/REPEAT/"что играет" even if Spotify stayed active/paused.
    reset_media();
    handle_soloist_ws_json("{\"status\":\"paused\",\"is_active\":true}");
    voice("включи яндекс музыку");
    check(yandex_music_active == 1 && spotify_is_active == 0 && command_calls == 1 &&
          !strcmp(command, "deactivate") && suppressed_calls == 0,
          "active paused Spotify deactivates cleanly when switching to Yandex Music");
    const char *ym_pass[] = {"дальше", "назад", "перемешай", "повторяй", "что играет"};
    for (unsigned i = 0; i < sizeof(ym_pass) / sizeof(ym_pass[0]); i++) {
        clear_calls(); voice(ym_pass[i]);
        check(passed_to_alice(), "active Yandex Music passes media/info command to Alice");
    }
    // Within 5000ms handoff grace, stale WS playing frame keeps Yandex Music ownership
    clear_calls(); mock_now += 500;
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    check(yandex_music_active == 1, "stale WS playing within 5s grace preserves Yandex Music handoff");
    mock_now += 6000;
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    check(yandex_music_active == 0, "WS playing after 5s handoff grace reclaims Spotify ownership");

    reset_media();
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    voice("включи яндекс музыку");
    check(yandex_music_active == 1 && spotify_is_active == 0 && command_calls == 1 &&
          !strcmp(command, "deactivate") && suppressed_calls == 0,
          "playing Spotify deactivates cleanly without suppressing Alice when switching to Yandex Music");
    clear_calls(); voice("дальше");
    check(passed_to_alice(), "дальше after Yandex Music handoff reaches Alice");

    reset_media();
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    voice("включи плейлист утренний в яндекс музыке");
    check(yandex_music_active == 1 && spotify_is_active == 0 && command_calls == 1 &&
          !strcmp(command, "deactivate") && suppressed_calls == 0,
          "Yandex playlist handoff deactivates playing Spotify without suppressing Alice");

    clear_calls(); voice("включи музыку");
    check(yandex_music_active == 0 && resume_calls == 1 && suppressed_calls == 1,
          "explicit Spotify resume clears yandex_music_active");
    clear_calls();
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    yandex_music_active = 1;
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    check(yandex_music_active == 0, "active Spotify WS playback clears yandex_music_active");
    clear_calls(); voice("дальше");
    check(command_calls == 1 && !strcmp(command, "next") && suppressed_calls == 1,
          "дальше controls Spotify again once Spotify resumes");

    reset_media();
    g_ws_fx_active = LED_FX_NONE; g_ws_play_fx_calls = 0;
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true,\"uri\":\"spotify:track:FIRST\"}");
    handle_soloist_ws_json("{\"status\":\"buffering\",\"is_active\":true,\"uri\":\"spotify:track:FIRST\"}");
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true,\"uri\":\"spotify:track:SECOND\"}");
    check(g_ws_play_fx_calls == 0,
          "natural track switch without command does not trigger LED_FX_PLAY");

    g_ws_fx_active = LED_FX_SEARCH; g_ws_play_fx_calls = 0;
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true,\"uri\":\"spotify:track:FOUND\"}");
    check(g_ws_play_fx_calls == 1 && g_ws_fx_active == LED_FX_PLAY,
          "search completion on playing WS transitions LED_FX_SEARCH into LED_FX_PLAY");

    // Voice queue & restart commands
    reset_media();
    voice("поставь следующим Кино Кукушка");
    check(command_calls == 1 && !strcmp(command, "enqueue") && !strcmp(command_arg, "кино кукушка") &&
          suppressed_calls == 1 && search_calls == 0 && g_ws_fx_active == LED_FX_SEARCH,
          "поставь следующим extracts clean query and triggers enqueue + search FX");

    reset_media();
    voice("алиса, добавь в очередь Раммштайн");
    check(command_calls == 1 && !strcmp(command, "enqueue") && !strcmp(command_arg, "раммштайн") &&
          suppressed_calls == 1,
          "добавь в очередь extracts clean query and triggers enqueue");

    reset_media();
    voice("поставь ДДТ в очередь");
    check(command_calls == 1 && !strcmp(command, "enqueue") && !strcmp(command_arg, "ддт") &&
          suppressed_calls == 1,
          "поставь <X> в очередь extracts clean query and triggers enqueue");

    reset_media(); spotify_device_is_active = 1; playback_status = 1;
    voice("следующий трек");
    check(command_calls == 1 && !strcmp(command, "next"), "следующий трек triggers NEXT, not enqueue");

    reset_media(); spotify_device_is_active = 1; playback_status = 1;
    voice("включи следующий трек");
    check(strcmp(command, "enqueue") != 0, "включи следующий трек does not trigger enqueue");

    reset_media();
    voice("добавь в очередь");
    check(passed_to_alice(), "empty добавь в очередь is passed to Alice");

    const char *restarts[] = {"начни сначала", "сначала", "включи сначала", "поставь заново"};
    for (unsigned i = 0; i < sizeof(restarts) / sizeof(restarts[0]); i++) {
        reset_media(); spotify_is_active = spotify_device_is_active = 1; playback_status = 1;
        voice(restarts[i]);
        check(command_calls == 1 && !strcmp(command, "seek") && !strcmp(command_arg, "0") && suppressed_calls == 1,
              restarts[i]);
    }
    reset_media(); spotify_is_active = spotify_device_is_active = 1; playback_status = 1;
    voice("включи песню Сначала");
    check(search_calls == 1 && command_calls == 0, "включи песню Сначала remains catalog search");

    reset_media(); spotify_is_active = spotify_device_is_active = 1; playback_status = 1;
    voice("сначала включи свет");
    check(passed_to_alice(), "сначала включи свет is left to Alice");

    reset_media(); spotify_is_active = spotify_device_is_active = 1; yandex_music_active = 1;
    voice("начни сначала");
    check(passed_to_alice(), "начни сначала while yandex_music_active is left to Alice");

    // Port 9910 IOEvent / AppState parser checks
    {
        uint8_t empty_io[8] = { 0xaa, 0x05, 0x02, 0x2a, 0x00 }; // field 85 -> field 5 empty
        int st = -1;
        check(parse_quasar_io_media_state(empty_io, 5, &st) == 1 && st == 0,
              "IOEvent empty AppState parses as idle (0)");

        uint8_t m_sub[32]; unsigned long mp = pb_bytes(m_sub, 1, "t123", 4);
        uint8_t app[64]; unsigned long ap = pb_bytes(app, 2, m_sub, mp);
        uint8_t io[96]; unsigned long ip = pb_bytes(io, 5, app, ap);
        uint8_t msg[128]; unsigned long fp = pb_bytes(msg, 85, io, ip);
        check(parse_quasar_io_media_state(msg, fp, &st) == 1 && st == 2,
              "IOEvent MusicState active parses as 2");

        mp += pb_varint(m_sub + mp, 3, 1); // is_paused = 1
        ap = pb_bytes(app, 2, m_sub, mp);
        ip = pb_bytes(io, 5, app, ap);
        fp = pb_bytes(msg, 85, io, ip);
        check(parse_quasar_io_media_state(msg, fp, &st) == 1 && st == 1,
              "IOEvent MusicState paused parses as 1");

        uint8_t r_sub[32]; unsigned long rp = pb_bytes(r_sub, 1, "rock", 4);
        ap = pb_bytes(app, 4, r_sub, rp);
        ip = pb_bytes(io, 5, app, ap);
        fp = pb_bytes(msg, 252, io, ip);
        fp += pb_varint(msg + fp, 253, 85);
        check(parse_quasar_io_media_state(msg, fp, &st) == 1 && st == 2,
              "SharedMessage 252/253=85 RadioState active parses as 2");

        uint8_t bt_sub[32]; unsigned long bp = pb_bytes(bt_sub, 2, "bt", 2);
        ap = pb_bytes(app, 5, bt_sub, bp);
        ip = pb_bytes(io, 5, app, ap);
        fp = pb_bytes(msg, 85, io, ip);
        check(parse_quasar_io_media_state(msg, fp, &st) == 1 && st == 2,
              "IOEvent BluetoothPlayerState active parses as 2");

        uint8_t ac_sub[16]; unsigned long acp = pb_varint(ac_sub, 2, 3); // PLAYING
        ap = pb_bytes(app, 6, ac_sub, acp);
        ip = pb_bytes(io, 5, app, ap);
        fp = pb_bytes(msg, 85, io, ip);
        check(parse_quasar_io_media_state(msg, fp, &st) == 1 && st == 2,
              "IOEvent AudioClientEvent PLAYING parses as 2");
        acp = pb_varint(ac_sub, 2, 4); // PAUSED
        ap = pb_bytes(app, 6, ac_sub, acp);
        ip = pb_bytes(io, 5, app, ap);
        fp = pb_bytes(msg, 85, io, ip);
        check(parse_quasar_io_media_state(msg, fp, &st) == 1 && st == 1,
              "IOEvent AudioClientEvent PAUSED parses as 1");
        acp = pb_varint(ac_sub, 2, 5); // STOPPED
        ap = pb_bytes(app, 6, ac_sub, acp);
        ip = pb_bytes(io, 5, app, ap);
        fp = pb_bytes(msg, 85, io, ip);
        check(parse_quasar_io_media_state(msg, fp, &st) == 1 && st == 0,
              "IOEvent AudioClientEvent STOPPED parses as 0");
    }

    // Issue #34: smart-home color/LED/brightness commands stay with Alice while tracks still search Spotify.
    const char *smart_color_alice[] = {
        "включи красный цвет",
        "поставь синий цвет на кухне",
        "включи другой цвет",
        "включи красный",
        "поставь фиолетовый в спальне",
        "включи в спальне зелёный",
        "поставь на кухне тёплый белый",
        "включи красный, пожалуйста",
        "включи светодиодную ленту",
        "поставь яркость на максимум",
        "включи ночной режим",
    };
    for (unsigned i = 0; i < sizeof(smart_color_alice) / sizeof(smart_color_alice[0]); i++) {
        reset_media();
        voice(smart_color_alice[i]);
        check(passed_to_alice(), smart_color_alice[i]);
    }

    const char *color_spotify_searches[] = {
        "включи синий трактор",
        "включи белый снег",
        "включи розовый фламинго",
        "включи цветы",
        "включи песню Красный цвет",
        "включи трек Цвет настроения синий",
        "включи трек Синий",
    };
    for (unsigned i = 0; i < sizeof(color_spotify_searches) / sizeof(color_spotify_searches[0]); i++) {
        reset_media();
        voice(color_spotify_searches[i]);
        check(search_calls == 1 && command_calls == 0, color_spotify_searches[i]);
    }

    // 3-mode voice switching (spotify / yandex / remote) & query TTS
    reset_media();
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    voice("алиса, включи режим яндекс");
    check(voice_mode == VOICE_MODE_YANDEX && yandex_music_active == 1 && spotify_is_active == 0 &&
          command_calls == 1 && !strcmp(command, "deactivate") &&
          tts_calls == 1 && !strcmp(last_tts, "Режим Яндекс"),
          "switching active local speaker to Yandex mode deactivates and announces mode");

    clear_calls();
    voice("какой сейчас режим?");
    check(tts_calls == 1 && !strcmp(last_tts, "Сейчас режим Яндекс") && command_calls == 0,
          "query mode speaks Yandex mode");

    const char *yandex_ignored_voice[] = {
        "включи Кино", "включи музыку", "дальше", "назад", "пауза", "что играет", "добавь в очередь Кино"
    };
    for (unsigned i = 0; i < sizeof(yandex_ignored_voice) / sizeof(yandex_ignored_voice[0]); i++) {
        clear_calls();
        voice(yandex_ignored_voice[i]);
        check(passed_to_alice() && tts_calls == 0, yandex_ignored_voice[i]);
    }
    handle_soloist_ws_json("{\"status\":\"stopped\",\"is_active\":false}");
    clear_calls();
    input(163, 1); input(164, 1); input(164, 0);
    check(passed_to_alice(), "hardware keys pass to Alice in Yandex mode when speaker not locally active");
    mock_now += 1000;
    spotify_device_is_active = 1; playback_status = 1;
    clear_calls();
    input(164, 1); input(164, 0);
    check(command_calls == 1 && !strcmp(command, "pause"),
          "manual Spotify Connect cast on speaker keeps hardware pause working in Yandex mode");

    // Switching to remote mode while inactive does not call deactivate
    reset_media();
    voice("переключись на пульт");
    check(voice_mode == VOICE_MODE_REMOTE && yandex_music_active == 0 && spotify_is_active == 0 &&
          command_calls == 0 && tts_calls == 1 && !strcmp(last_tts, "Режим пульта"),
          "switching to remote mode preserves active Connect session without deactivate");

    clear_calls();
    voice("какой режим");
    check(tts_calls == 1 && !strcmp(last_tts, "Сейчас режим пульта"),
          "query mode speaks remote mode");

    // In remote mode with another device (is_active:false) playing, commands steer remote player without claiming local speaker
    reset_media();
    voice_mode = VOICE_MODE_REMOTE;
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":false}");
    const char *remote_ctrl[] = {"дальше", "назад", "пауза", "начни сначала", "перемешай", "повторяй"};
    const char *remote_exp[]  = {"next",   "prev",  "pause", "seek",          "shuffle",   "repeat"};
    for (unsigned i = 0; i < sizeof(remote_ctrl) / sizeof(remote_ctrl[0]); i++) {
        clear_calls();
        voice(remote_ctrl[i]);
        check(command_calls == 1 && !strcmp(command, remote_exp[i]) && suppressed_calls == 1 && spotify_is_active == 0,
              remote_ctrl[i]);
    }

    handle_soloist_ws_json("{\"status\":\"paused\",\"is_active\":false}");
    clear_calls(); voice("стоп");
    check(passed_to_alice(), "paused remote device leaves STOP to Alice");

    handle_soloist_ws_json("{\"status\":\"stopped\",\"is_active\":false}");
    clear_calls(); voice("пауза");
    check(passed_to_alice(), "stopped remote device leaves PAUSE to Alice");

    const char *remote_searches[] = {"включи Queen", "включи музыку", "включи плейлист Рок", "включи песню Играй здесь"};
    for (unsigned i = 0; i < sizeof(remote_searches) / sizeof(remote_searches[0]); i++) {
        clear_calls();
        voice(remote_searches[i]);
        check(command_calls == 1 && !strcmp(command, "search-remote") && suppressed_calls == 1 &&
              search_calls == 0 && resume_calls == 0 && spotify_is_active == 0,
              remote_searches[i]);
    }

    clear_calls(); playback_status = 2; mock_now += 1000;
    input(164, 1); input(164, 0);
    check(command_calls == 1 && !strcmp(command, "play-remote") && spotify_is_active == 0,
          "hardware tap when paused in remote mode dispatches play-remote");

    clear_calls(); playback_status = 0; mock_now += 1000;
    input(164, 1); input(164, 0);
    check(command_calls == 1 && !strcmp(command, "toggle-remote") && spotify_is_active == 0,
          "hardware tap when stopped in remote mode dispatches toggle-remote");

    const char *claim_phrases[] = {"играй здесь", "переключи на колонку", "перенеси музыку сюда"};
    for (unsigned i = 0; i < sizeof(claim_phrases) / sizeof(claim_phrases[0]); i++) {
        clear_calls(); spotify_is_active = 0; spotify_device_is_active = 0;
        voice(claim_phrases[i]);
        check(command_calls == 1 && !strcmp(command, "activate") && suppressed_calls == 1 && spotify_is_active == 1,
              claim_phrases[i]);
    }

    // Yandex handoff while remote PC is playing must not pause/deactivate the PC
    reset_media();
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":false}");
    voice("включи яндекс музыку");
    check(yandex_music_active == 1 && command_calls == 0 && voice_mode == VOICE_MODE_SPOTIFY,
          "temporary Yandex handoff never deactivates remote Connect device or flips persistent mode");

    // Switching back to spotify mode & non-mode phrases staying untouched
    reset_media(); voice_mode = VOICE_MODE_REMOTE;
    voice("алиса, включи режим спотифай");
    check(voice_mode == VOICE_MODE_SPOTIFY && tts_calls == 1 && !strcmp(last_tts, "Режим Спотифай"),
          "switching back to Spotify mode announces mode");

    const char *non_mode_phrases[] = {"детский режим", "алиса, включи ночной режим", "включи режим дискотеки"};
    for (unsigned i = 0; i < sizeof(non_mode_phrases) / sizeof(non_mode_phrases[0]); i++) {
        reset_media(); voice_mode = VOICE_MODE_REMOTE;
        voice(non_mode_phrases[i]);
        check(voice_mode == VOICE_MODE_REMOTE, non_mode_phrases[i]);
    }

    reset_media();
    voice("включи песню Режим");
    check(voice_mode == VOICE_MODE_SPOTIFY && search_calls == 1 && tts_calls == 0,
          "explicit song Режим triggers catalog search rather than mode switch");

    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="soloist-media-") as temp:
    path = pathlib.Path(temp)
    (path / "test.c").write_text(media_prefix + media_production + media_driver, encoding="utf-8")
    executable = path / ("test.exe" if sys.platform == "win32" else "test")
    subprocess.run(compiler + ["-std=c99", "-Wall", "-Wextra", "-I", str(ROOT / "src"), str(path / "test.c"), "-o", str(executable)], check=True)
    sys.exit(subprocess.run([str(executable)]).returncode)
