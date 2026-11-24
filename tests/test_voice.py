"""Compile actual production parsing/state code with a tiny host fixture driver."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/soloist-bridge.c").read_text(encoding="utf-8")

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
static unsigned long str_len(const char *s) { return s ? strlen(s) : 0; }
static int failures, command_calls, search_calls, resume_calls, suppressed_calls, cache_calls;
static char command[32];
static int64_t mock_now = 10000;
static int64_t now_ms(void) { return mock_now; }
static void log_msg(const char *s) { (void)s; }
static void trigger_alice_suppression(int fd, int64_t ms) { (void)fd; (void)ms; suppressed_calls++; }
static void run_soloist_cmd(const char *s) { command_calls++; snprintf(command, sizeof(command), "%s", s); }
static void run_soloist_cmd2(const char *s, const char *a) { (void)a; run_soloist_cmd(s); }
static void run_toggle(void) { run_soloist_cmd("toggle"); }
static void run_search_and_play(const char *s) {
    if (!strcmp(s, "включи музыку")) resume_calls++;
    else search_calls++;
}
static void run_controller(const char *s, const char *a) { (void)a; if (!strcmp(s, "clean-cache-full")) cache_calls++; }
static void publish_track(const char *s) { (void)s; }
static int selection_needs_observation(void) { return 0; }
static void set_led_visualizer(int enable) { (void)enable; }
static int send_alice_tts(int fd, const char *s) { (void)fd; (void)s; return 0; }
static const char *get_censor_path(void) { return "/tmp/unused"; }
static int censor_on_calls, censor_off_calls;
static int sys_open(const char *p, int f, int m) { (void)p; (void)f; (void)m; censor_on_calls++; return -1; }
static void sys_close(int fd) { (void)fd; }
static int sys_unlink(const char *p) { (void)p; censor_off_calls++; return 0; }
static long sys_write(int fd, const void *b, unsigned long n) { (void)fd; (void)b; return n; }
static int touch_down_start, touch_is_down;
static int64_t touch_down_time, last_touch_release_time, cancel_alice_until_ms;
'''
media_production = section("static void to_lower_ru", "#ifdef __aarch64__\nstatic void set_led_visualizer_impl")
media_production += section("static int is_web_or_culinary_intent", "/* =========================================================================")
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
    command_calls = search_calls = resume_calls = suppressed_calls = 0; command[0] = 0;
}
static void reset_media(void) {
    spotify_is_active = spotify_device_is_active = yandex_music_active = playback_status = 0;
    touch_down_start = touch_is_down = 0; last_touch_release_time = 0;
    censor_on_calls = censor_off_calls = 0;
    clear_calls();
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
    check(yandex_music_active == 1 && spotify_is_active == 0 && passed_to_alice(),
          "paused Spotify leaves Yandex Music request to Alice and marks Yandex Music active");
    const char *ym_pass[] = {"дальше", "назад", "перемешай", "повторяй", "что играет"};
    for (unsigned i = 0; i < sizeof(ym_pass) / sizeof(ym_pass[0]); i++) {
        clear_calls(); voice(ym_pass[i]);
        check(passed_to_alice(), "active Yandex Music passes media/info command to Alice");
    }

    reset_media();
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    voice("включи яндекс музыку");
    check(yandex_music_active == 1 && spotify_is_active == 0 && command_calls == 1 &&
          !strcmp(command, "pause") && suppressed_calls == 0,
          "playing Spotify pauses cleanly without suppressing Alice when switching to Yandex Music");
    clear_calls(); voice("дальше");
    check(passed_to_alice(), "дальше after Yandex Music handoff reaches Alice");

    reset_media();
    handle_soloist_ws_json("{\"status\":\"playing\",\"is_active\":true}");
    voice("включи плейлист утренний в яндекс музыке");
    check(yandex_music_active == 1 && spotify_is_active == 0 && command_calls == 1 &&
          !strcmp(command, "pause") && suppressed_calls == 0,
          "Yandex playlist handoff pauses playing Spotify without suppressing Alice");

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

    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="soloist-media-") as temp:
    path = pathlib.Path(temp)
    (path / "test.c").write_text(media_prefix + media_production + media_driver, encoding="utf-8")
    executable = path / ("test.exe" if sys.platform == "win32" else "test")
    subprocess.run(compiler + ["-std=c99", "-Wall", "-Wextra", str(path / "test.c"), "-o", str(executable)], check=True)
    sys.exit(subprocess.run([str(executable)]).returncode)
