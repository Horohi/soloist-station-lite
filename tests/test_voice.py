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
    sys.exit(subprocess.run([str(executable)]).returncode)
