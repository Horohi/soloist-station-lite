"""Compile the actual bridge controller boundary against host syscall wrappers."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/soloist-bridge.c").read_text()
production = source[source.index("static const char *get_controller_sh"):source.index("/* Text and command classification helpers */")]
loop_start = source.index("    while (1) {", source.index("void _start")) + len("    while (1) {")
loop_prologue = source[loop_start:source.index("        int64_t now = now_ms();", loop_start)]
if "service_controllers" not in production:
    production += "static void service_controllers(void) {}\n"
prefix = r'''
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <poll.h>
#include <signal.h>
static int is_yam_mode=1;
static long sys_clone(unsigned long f,void *s) {(void)f;(void)s;return fork();}
static long sys_wait4(int p,int *s,int o,void *r) {(void)r;long n=waitpid(p,s,o);return n<0?-errno:n;}
static int sys_execve(const char *p,const char *const a[],const char *const e[]) {return execve(p,(char *const *)a,(char *const *)e);}
static void sys_exit(int n) {_exit(n);}
static int sys_open(const char *p,int f,int m) {return open(p,f,m);}
static long sys_read(int f,void *b,unsigned long n) {long r=read(f,b,n);return r<0?-errno:r;}
static long sys_write(int f,const void *b,unsigned long n) {return write(f,b,n);}
static int sys_close(int f) {return close(f);}
static int sys_rename(const char *a,const char *b) {return rename(a,b);}
static int sys_mkdir(const char *p,int m) {return mkdir(p,m);}
static int sys_signal_default(int s) {return signal(s,SIG_DFL)==SIG_ERR?-1:0;}
static int sys_pipe(int f[2]) {if(pipe(f)<0)return -1;fcntl(f[0],F_SETFL,O_NONBLOCK);return 0;}
static int sys_dup3(int from,int to) {return dup2(from,to);}
static unsigned long str_len(const char *s) {return s?strlen(s):0;}
static int str_equal(const char *a,const char *b) {return !strcmp(a,b);}
static void log_msg(const char *s) {fputs(s,stderr);}
static int64_t now_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000+t.tv_nsec/1000000;}
'''
driver = r'''
int main(void) {
    signal(SIGHUP,SIG_IGN); signal(SIGINT,SIG_IGN); signal(SIGTERM,SIG_IGN);
    int events[2]; pipe(events); write(events[1],"TQ",2);
    int64_t start=now_ms();
    run_controller("next",0); run_controller("shuffle","on"); run_controller("shuffle","off");
    struct pollfd p={events[0],POLLIN,0};
    char ready[2]={0}; if(poll(&p,1,0)>0) read(events[0],ready,2);
    int responsive=now_ms()-start<500 && ready[0]=='T' && ready[1]=='Q';
    printf("%s input and Quasar ready events serviced under 500ms during admission contention\n",responsive?"PASS":"FAIL");
    struct timespec tick={0,10000000};
    for(int i=0;i<250;i++){/* MAIN_LOOP_PROLOGUE */ nanosleep(&tick,0);}
    char output[256]={0};FILE *f=fopen("order","r"); if(f){fread(output,1,sizeof(output)-1,f);fclose(f);}
    int ordered=!strcmp(output,"next \nshuffle on\nshuffle off\n");
    printf("%s queued bridge commands retain event order\n",ordered?"PASS":"FAIL");
    unsigned long ignored=0;f=fopen("ignored","r");if(f){fscanf(f,"%lx",&ignored);fclose(f);}
    int signals=f && !(ignored & ((1ul<<(SIGHUP-1))|(1ul<<(SIGINT-1))|(1ul<<(SIGTERM-1))));
    printf("%s controller resets service-inherited ignored termination signals\n",signals?"PASS":"FAIL");
    memset(output,0,sizeof(output));f=fopen("environment","r");if(f){fread(output,1,sizeof(output)-1,f);fclose(f);}
    int environment=!strcmp(output,"unset:unset");
    printf("%s native controller launch does not contaminate shell library environment\n",environment?"PASS":"FAIL");
    unlink("order");run_controller("fail",0);run_controller("shuffle","on");
    for(int i=0;i<100;i++){/* MAIN_LOOP_PROLOGUE */ nanosleep(&tick,0);}
    memset(output,0,sizeof(output));f=fopen("order","r");if(f){fread(output,1,sizeof(output)-1,f);fclose(f);}
    int recovered=!strcmp(output,"shuffle on\n");
    printf("%s child failure before admission does not stall FIFO\n",recovered?"PASS":"FAIL");
    return !(responsive && ordered && signals && recovered && environment);
}
'''
driver = driver.replace("/* MAIN_LOOP_PROLOGUE */", loop_prologue)
with tempfile.TemporaryDirectory(prefix="soloist-admission-") as directory:
    temp = pathlib.Path(directory)
    (temp / "scripts").mkdir()
    (temp / "scripts/control.sh").write_text('#!/bin/sh\n[ "$1" != fail ] || exit 7\n[ "$1" != next ] || sleep 1\nprintf "%s %s\\n" "$1" "$2" >> order\nprintf "%s:%s" "${LD_LIBRARY_PATH-unset}" "${LD_PRELOAD-unset}" > environment\nawk \'/^SigIgn:/ {print $2}\' /proc/$$/status > ignored\n[ "${SOLOIST_BRIDGE_ADMISSION:-}" != 1 ] || printf A >&3\n')
    (temp / "test.c").write_text(prefix + production + driver)
    compiler = [sys.argv[1], "cc"] if len(sys.argv) > 1 else ["cc"]
    executable = temp / "test"
    subprocess.run(compiler + ["-std=c99", "-Wall", "-Wextra", str(temp / "test.c"), "-o", str(executable)], check=True)
    raise SystemExit(subprocess.run([str(executable)], cwd=temp).returncode)
