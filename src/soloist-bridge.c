#include <stdint.h>
#include "voice-request.h"
#include "duck_shm.h"

/* Minimal Linux system call wrappers for AArch64 (ARM64) */

static inline long sys_read(int fd, void *buf, unsigned long count) {
    register long x8 __asm__("x8") = 63;
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = count;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline long sys_write(int fd, const void *buf, unsigned long count) {
    register long x8 __asm__("x8") = 64;
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = count;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline int sys_open(const char *filename, int flags, int mode) {
    register long x8 __asm__("x8") = 56; // openat
    register long x0 __asm__("x0") = -100; // AT_FDCWD
    register long x1 __asm__("x1") = (long)filename;
    register long x2 __asm__("x2") = flags;
    register long x3 __asm__("x3") = mode;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return (int)x0;
}

static inline int sys_close(int fd) {
    register long x8 __asm__("x8") = 57;
    register long x0 __asm__("x0") = fd;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return (int)x0;
}

static inline int sys_mkdir(const char *path, int mode) {
    register long x8 __asm__("x8") = 34; // mkdirat
    register long x0 __asm__("x0") = -100;
    register long x1 __asm__("x1") = (long)path;
    register long x2 __asm__("x2") = mode;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int)x0;
}

static inline int sys_rename(const char *from, const char *to) {
    register long x8 __asm__("x8") = 38; // renameat
    register long x0 __asm__("x0") = -100;
    register long x1 __asm__("x1") = (long)from;
    register long x2 __asm__("x2") = -100;
    register long x3 __asm__("x3") = (long)to;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return (int)x0;
}

static inline int sys_pipe(int fds[2]) {
    register long x8 __asm__("x8") = 59; // pipe2
    register long x0 __asm__("x0") = (long)fds;
    register long x1 __asm__("x1") = 2048; // O_NONBLOCK
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return (int)x0;
}

static inline int sys_dup3(int from, int to) {
    register long x8 __asm__("x8") = 24;
    register long x0 __asm__("x0") = from;
    register long x1 __asm__("x1") = to;
    register long x2 __asm__("x2") = 0;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int)x0;
}

static inline int sys_signal_default(int signal) {
    struct { unsigned long handler, flags, restorer, mask; } action = {0, 0, 0, 0};
    register long x8 __asm__("x8") = 134; // rt_sigaction
    register long x0 __asm__("x0") = signal;
    register long x1 __asm__("x1") = (long)&action;
    register long x2 __asm__("x2") = 0;
    register long x3 __asm__("x3") = 8; // Kernel sigset size, not libc's sigset_t.
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return (int)x0;
}

static inline int sys_ioctl(int fd, unsigned long req, void *arg) {
    register long x8 __asm__("x8") = 29;
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = req;
    register long x2 __asm__("x2") = (long)arg;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int)x0;
}

static inline int sys_socket(int domain, int type, int protocol) {
    register long x8 __asm__("x8") = 198;
    register long x0 __asm__("x0") = domain;
    register long x1 __asm__("x1") = type;
    register long x2 __asm__("x2") = protocol;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int)x0;
}

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
};

static inline int sys_connect(int sockfd, const struct sockaddr_in *addr, unsigned int addrlen) {
    register long x8 __asm__("x8") = 203;
    register long x0 __asm__("x0") = sockfd;
    register long x1 __asm__("x1") = (long)addr;
    register long x2 __asm__("x2") = addrlen;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int)x0;
}

struct pollfd {
    int fd;
    short events;
    short revents;
};

struct timespec {
    long tv_sec;
    long tv_nsec;
};

static inline int sys_ppoll(struct pollfd *fds, unsigned int nfds, const struct timespec *tmo, const void *sigmask, unsigned long sigsetsize) {
    register long x8 __asm__("x8") = 73;
    register long x0 __asm__("x0") = (long)fds;
    register long x1 __asm__("x1") = nfds;
    register long x2 __asm__("x2") = (long)tmo;
    register long x3 __asm__("x3") = (long)sigmask;
    register long x4 __asm__("x4") = sigsetsize;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
    return (int)x0;
}

static inline long sys_clone(unsigned long flags, void *child_stack) {
    register long x8 __asm__("x8") = 220; // clone
    register long x0 __asm__("x0") = flags;
    register long x1 __asm__("x1") = (long)child_stack;
    register long x2 __asm__("x2") = 0;
    register long x3 __asm__("x3") = 0;
    register long x4 __asm__("x4") = 0;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
    return x0;
}

static inline int sys_execve(const char *filename, const char *const argv[], const char *const envp[]) {
    register long x8 __asm__("x8") = 221;
    register long x0 __asm__("x0") = (long)filename;
    register long x1 __asm__("x1") = (long)argv;
    register long x2 __asm__("x2") = (long)envp;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int)x0;
}

static inline void sys_exit(int code) {
    register long x8 __asm__("x8") = 93;
    register long x0 __asm__("x0") = code;
    __asm__ volatile("svc #0" : : "r"(x0), "r"(x8) : "memory");
    while (1) {}
}

static inline int sys_unlink(const char *path) {
#ifdef __aarch64__
    register long x8 __asm__("x8") = 35; // unlinkat
    register long x0 __asm__("x0") = -100; // AT_FDCWD
    register long x1 __asm__("x1") = (long)path;
    register long x2 __asm__("x2") = 0;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int)x0;
#else
    extern int unlink(const char *path);
    return unlink(path);
#endif
}

static inline int sys_ftruncate(int fd, long length) {
#ifdef __aarch64__
    register long x8 __asm__("x8") = 46;
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = length;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return (int)x0;
#else
    (void)fd; (void)length;
    return -1;
#endif
}

static inline void *sys_mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off) {
#ifdef __aarch64__
    register long x8 __asm__("x8") = 222;
    register long x0 __asm__("x0") = (long)addr;
    register long x1 __asm__("x1") = (long)len;
    register long x2 __asm__("x2") = prot;
    register long x3 __asm__("x3") = flags;
    register long x4 __asm__("x4") = fd;
    register long x5 __asm__("x5") = off;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return (void *)x0;
#else
    (void)addr; (void)len; (void)prot; (void)flags; (void)fd; (void)off;
    return (void *)-1;
#endif
}

static inline int sys_shmget(int key, unsigned long size, int shmflg) {
#ifdef __aarch64__
    register long x8 __asm__("x8") = 194;
    register long x0 __asm__("x0") = key;
    register long x1 __asm__("x1") = size;
    register long x2 __asm__("x2") = shmflg;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int)x0;
#else
    (void)key; (void)size; (void)shmflg;
    return -1;
#endif
}

static inline void *sys_shmat(int shmid, const void *shmaddr, int shmflg) {
#ifdef __aarch64__
    register long x8 __asm__("x8") = 196;
    register long x0 __asm__("x0") = shmid;
    register long x1 __asm__("x1") = (long)shmaddr;
    register long x2 __asm__("x2") = shmflg;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (void *)x0;
#else
    (void)shmid; (void)shmaddr; (void)shmflg;
    return (void *)-1;
#endif
}

static inline int sys_shmdt(const void *shmaddr) {
#ifdef __aarch64__
    register long x8 __asm__("x8") = 197;
    register long x0 __asm__("x0") = (long)shmaddr;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return (int)x0;
#else
    (void)shmaddr;
    return -1;
#endif
}

static inline long sys_wait4(int pid, int *status, int options, void *rusage) {
    register long x8 __asm__("x8") = 260;
    register long x0 __asm__("x0") = pid;
    register long x1 __asm__("x1") = (long)status;
    register long x2 __asm__("x2") = options;
    register long x3 __asm__("x3") = (long)rusage;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

static inline int sys_clock_gettime(int clk_id, struct timespec *tp) {
    register long x8 __asm__("x8") = 113;
    register long x0 __asm__("x0") = clk_id;
    register long x1 __asm__("x1") = (long)tp;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return (int)x0;
}

static inline int sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    register long x8 __asm__("x8") = 101;
    register long x0 __asm__("x0") = (long)req;
    register long x1 __asm__("x1") = (long)rem;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return (int)x0;
}

static inline void sys_msleep(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    sys_nanosleep(&ts, 0);
}

static unsigned long str_len(const char *s) {
    unsigned long len = 0;
    while (s && s[len]) len++;
    return len;
}

static void log_msg(const char *s) {
    sys_write(1, s, str_len(s));
}

static int64_t now_ms(void) {
    struct timespec ts;
    if (sys_clock_gettime(1 /* CLOCK_MONOTONIC */, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
    }
    return 0;
}

static int quasar_uses_f252 = -1; // -1=unknown, 0=direct field 210, 1=SharedMessage 252/253
static int spotify_is_active = 0;
static int spotify_device_is_active = 0; // 1 = this speaker is active Spotify Connect device
static int yandex_music_active = 0;      // 1 = user handed playback to Yandex Music / Radio
static int64_t yandex_music_handoff_ms = 0; // timestamp of explicit handoff to Yandex Music
static int repeat_mode = 0; // 0=off, 1=context, 2=track
static char current_track_uri[128] = {0};
static char current_track_name[128] = {0};
static char current_artist_name[128] = {0};
static char last_auto_next_uri[128] = {0};
static int playback_status = 0; // 0=stopped, 1=playing, 2=paused, 3=buffering
static uint64_t track_duration_ms = 0;
static uint64_t base_position_ms = 0;
static int64_t local_recorded_ms = 0;
static int playback_speed = 0;
static int64_t stuck_since_ms = 0;
static int64_t last_auto_next_time = 0;
static int64_t last_cache_clean_ms = 0;

#include "text-json.h"
#include "quasar-ipc.h"
#include "quasar-volume.h"

static int is_yam_mode = 0;
static char g_ctrl_sh[320] = {0};
static char g_censor_path[320] = {0};
static char g_req_dir[320] = {0};
static char g_sel_pending_path[320] = {0};
static char g_sel_path[320] = {0};
static char g_track_path[320] = {0};
static char g_track_tmp_path[320] = {0};
static char g_gen_path[320] = {0};
static char g_conf_path[320] = {0};
static char g_led_state_path[320] = {0};
static char g_led_req_path[320] = {0};
static char g_tts_req_path[320] = {0};
static char g_yam_env[5][280];
static int g_yam_env_count = 0;

static const char *get_controller_sh(void) {
    if (g_ctrl_sh[0]) return g_ctrl_sh;
    int fd = sys_open("./bin/control.sh", 0 /* O_RDONLY */, 0);
    if (fd >= 0) { sys_close(fd); return "./bin/control.sh"; }
    return is_yam_mode ? "./scripts/control.sh" : "/data/opt/soloist/scripts/control.sh";
}

static const char *get_censor_path(void) {
    if (g_censor_path[0]) return g_censor_path;
    return is_yam_mode ? "./data/censorship" : "/data/opt/soloist/data/censorship";
}

static const char *get_requests_dir(void) {
    if (g_req_dir[0]) return g_req_dir;
    return is_yam_mode ? "./data/requests" : "/data/opt/soloist/data/requests";
}

static const char *get_conf_path(void) {
    if (g_conf_path[0]) return g_conf_path;
    return is_yam_mode ? "./spotify_api.conf" : "/data/opt/soloist/spotify_api.conf";
}

static const char *get_led_state_path(void) {
    if (g_led_state_path[0]) return g_led_state_path;
    return is_yam_mode ? "./data/requests/led_state" : "/data/opt/soloist/data/requests/led_state";
}

static const char *get_led_req_path(void) {
    if (g_led_req_path[0]) return g_led_req_path;
    return is_yam_mode ? "./data/requests/led" : "/data/opt/soloist/data/requests/led";
}

static const char *get_tts_req_path(void) {
    if (g_tts_req_path[0]) return g_tts_req_path;
    return is_yam_mode ? "./data/requests/tts" : "/data/opt/soloist/data/requests/tts";
}

static uint64_t track_observation_revision;

static int selection_needs_observation(void) {
    const char *pending = g_sel_pending_path[0] ? g_sel_pending_path : (is_yam_mode ? "./data/requests/selection.pending" : "/data/opt/soloist/data/requests/selection.pending");
    const char *selection = g_sel_path[0] ? g_sel_path : (is_yam_mode ? "./data/requests/selection" : "/data/opt/soloist/data/requests/selection");
    const char *track = g_track_path[0] ? g_track_path : (is_yam_mode ? "./data/requests/track" : "/data/opt/soloist/data/requests/track");
    int fd = sys_open(pending, 0, 0);
    if (fd < 0) fd = sys_open(selection, 0, 0);
    if (fd < 0) return 0;
    char selected[256], observed[256];
    long n = sys_read(fd, selected, sizeof(selected));
    sys_close(fd);
    if (n <= 0 || n == (long)sizeof(selected)) return 0;
    fd = sys_open(track, 0, 0);
    if (fd < 0) return 0;
    long m = sys_read(fd, observed, sizeof(observed));
    sys_close(fd);
    if (n != m) return 0;
    for (long i = 0; i < n; i++) if (selected[i] != observed[i]) return 0;
    return 1;
}

static void publish_track(const char *uri) {
    const char *dir = get_requests_dir();
    const char *tmp = g_track_tmp_path[0] ? g_track_tmp_path : (is_yam_mode ? "./data/requests/track.tmp" : "/data/opt/soloist/data/requests/track.tmp");
    const char *path = g_track_path[0] ? g_track_path : (is_yam_mode ? "./data/requests/track" : "/data/opt/soloist/data/requests/track");
    sys_mkdir(dir, 0700);
    int fd = sys_open(tmp, 577 /* O_WRONLY|O_CREAT|O_TRUNC */, 0600);
    if (fd < 0) return;
    char record[160], digits[20];
    unsigned long length = str_len(uri), count = 0;
    if (length >= 128) { sys_close(fd); return; }
    for (unsigned long i = 0; i < length; i++) record[i] = uri[i];
    record[length++] = '\n';
    uint64_t revision = (uint64_t)now_ms();
    if (revision <= track_observation_revision) revision = track_observation_revision + 1;
    track_observation_revision = revision;
    do { digits[count++] = (char)('0' + revision % 10); revision /= 10; } while (revision);
    while (count) record[length++] = digits[--count];
    record[length++] = '\n';
    long written = sys_write(fd, record, length);
    sys_close(fd);
    if (written == (long)length) sys_rename(tmp, path);
}

struct controller_command {
    char action[16];
    char arg[512];
    char generation[32];
};
static struct controller_command controller_queue[16];
static unsigned int controller_head, controller_count;
static long controller_pid;
static int controller_fd = -1;

static void service_controllers(void) {
    int status;
    while (sys_wait4(-1, &status, 1 /* WNOHANG */, 0) > 0) {}
    if (controller_pid > 0) {
        char admission = 0;
        long result = sys_read(controller_fd, &admission, 1);
        if (result == -11 /* EAGAIN */ || result == -4 /* EINTR */) return;
        if (result != 1 || admission != 'A') {
            log_msg("[bridge] Command admission failed: ");
            log_msg(controller_queue[controller_head].action);
            log_msg("\n");
        }
        sys_close(controller_fd);
        controller_fd = -1;
        controller_pid = 0;
        controller_head = (controller_head + 1) % 16;
        controller_count--;
    }
    if (!controller_count) return;
    struct controller_command *command = &controller_queue[controller_head];
    int admission_pipe[2];
    if (sys_pipe(admission_pipe) < 0) {
        log_msg("[bridge] Cannot open command admission pipe\n");
        controller_head = (controller_head + 1) % 16;
        controller_count--;
        return;
    }
    long pid = sys_clone(17 /* SIGCHLD */, 0);
    if (pid == 0) {
        sys_close(admission_pipe[0]);
        if (admission_pipe[1] != 3) {
            if (sys_dup3(admission_pipe[1], 3) < 0) sys_exit(1);
            sys_close(admission_pipe[1]);
        }
        // Service launchers may ignore these signals; controllers must be stoppable.
        if (sys_signal_default(1) < 0 || sys_signal_default(2) < 0 || sys_signal_default(15) < 0) sys_exit(1);
        const char *argv[] = { "/bin/sh", get_controller_sh(), command->action,
            command->arg[0] ? command->arg : 0,
            command->generation[0] ? command->generation : 0, 0 };
        const char *envp[9];
        int ei = 0;
        envp[ei++] = "PATH=/bin:/usr/bin:/sbin:/usr/sbin";
        envp[ei++] = "SOLOIST_BRIDGE_ADMISSION=1";
        envp[ei++] = "SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt";
        for (int k = 0; k < g_yam_env_count && ei < 8; k++) {
            envp[ei++] = g_yam_env[k];
        }
        envp[ei] = 0;
        sys_execve("/bin/sh", argv, envp);
        sys_exit(1);
    }
    sys_close(admission_pipe[1]);
    if (pid > 0) {
        controller_pid = pid;
        controller_fd = admission_pipe[0];
    } else {
        sys_close(admission_pipe[0]);
        log_msg("[bridge] Cannot start command admission\n");
        controller_head = (controller_head + 1) % 16;
        controller_count--;
    }
}

static void run_controller(const char *action, const char *arg) {
    unsigned long action_len = str_len(action), arg_len = str_len(arg);
    if (controller_count == 16 || action_len >= 16 || arg_len >= 512) {
        log_msg("[bridge] Command rejected: admission queue full or argument too long\n");
        return;
    }
    struct controller_command *command = &controller_queue[(controller_head + controller_count) % 16];
    for (unsigned long i = 0; i <= action_len; i++) command->action[i] = action[i];
    for (unsigned long i = 0; i < arg_len; i++) command->arg[i] = arg[i];
    command->arg[arg_len] = 0;
    command->generation[0] = 0;
    if (str_equal(action, "auto-next")) { // Observe intent before waiting in the queue.
        const char *path = g_gen_path[0] ? g_gen_path : (is_yam_mode ? "./data/requests/generation" : "/data/opt/soloist/data/requests/generation");
        int fd = sys_open(path, 0, 0);
        long n = fd < 0 ? 0 : sys_read(fd, command->generation, sizeof(command->generation) - 1);
        if (fd >= 0) sys_close(fd);
        if (n <= 0) { command->generation[0] = '0'; n = 1; }
        while (n > 0 && (command->generation[n-1] == '\n' || command->generation[n-1] == '\r')) n--;
        command->generation[n] = 0;
    }
    controller_count++;
    service_controllers();
}

static void run_soloist_cmd(const char *cmd) { run_controller(cmd, 0); }
static void run_soloist_cmd2(const char *cmd, const char *arg) { run_controller(cmd, arg); }
static void run_toggle(void) { run_controller("toggle", 0); }
static void run_search_and_play(const char *query) { run_controller("search", query); }

#ifndef LED_FX_NONE
#define LED_FX_NONE   0
#define LED_FX_PLAY   1
#define LED_FX_NEXT   2
#define LED_FX_PREV   3
#define LED_FX_SEARCH 4
#define LED_FX_ERROR  5

#if defined(__aarch64__)
static void led_fx_trigger_impl(int fx_type);
static void led_fx_clear_impl(void);
static int led_fx_is_skip_recent_impl(int64_t now);
static int led_fx_is_play_recent_impl(int64_t now);
static int led_fx_active_type_impl(void);
static inline void led_fx_trigger(int fx_type) { led_fx_trigger_impl(fx_type); }
static inline void led_fx_clear(void) { led_fx_clear_impl(); }
static inline int led_fx_is_skip_recent(int64_t now) { return led_fx_is_skip_recent_impl(now); }
static inline int led_fx_is_play_recent(int64_t now) { return led_fx_is_play_recent_impl(now); }
static inline int led_fx_active_type(void) { return led_fx_active_type_impl(); }
#else
static inline void led_fx_trigger(int fx_type) { (void)fx_type; }
static inline void led_fx_clear(void) {}
static inline int led_fx_is_skip_recent(int64_t now) { (void)now; return 0; }
static inline int led_fx_is_play_recent(int64_t now) { (void)now; return 0; }
static inline int led_fx_active_type(void) { return LED_FX_NONE; }
#endif
#endif

static void handle_soloist_ws_json(const char *json) {
    extract_track_info(json);

    char msg_type[32] = {0};
    json_find_str(json, "type", msg_type, sizeof(msg_type));

    int vol = -1;
    if (json_find_int(json, "volume", &vol) && vol >= 0 && vol <= 100) {
        current_soloist_vol = vol;
        if (str_equal(msg_type, "playback_state")) {
            if (!ws_initial_vol_synced) {
                ws_initial_vol_synced = 1;
                if (quasar_vol_known && quasar_vol != vol) {
                    need_soloist_vol_sync = 1;
                }
            }
        } else if (str_equal(msg_type, "volume_changed")) {
            ws_initial_vol_synced = 1;
            int64_t now_vol = now_ms();
            int is_our_echo = (last_sent_to_soloist_vol >= 0 &&
                               vol == last_sent_to_soloist_vol &&
                               last_sent_to_soloist_ms > 0 &&
                               (now_vol - last_sent_to_soloist_ms) < 1200);
            if (!is_our_echo) {
                last_user_ws_vol_ms = now_vol;
                pending_quasar_vol = vol;
                pending_quasar_vol_due_ms = (last_sent_to_quasar_ms == 0 || now_vol - last_sent_to_quasar_ms >= 60)
                                            ? now_vol
                                            : (last_sent_to_quasar_ms + 60);
            }
        }
    }

    int active_bool = 0;
    if (json_find_bool(json, "is_active", &active_bool)) {
        spotify_device_is_active = active_bool;
        if (!active_bool) {
            spotify_is_active = 0;
            yandex_music_handoff_ms = 0;
        }
    }

    char status_str[32];
    if (json_find_str(json, "status", status_str, sizeof(status_str))) {
        if (str_equal(status_str, "playing")) {
            playback_status = 1;
            playback_speed = 1;
            spotify_is_active = spotify_device_is_active;
            int64_t ws_now = now_ms();
            int in_handoff_grace = (yandex_music_active && yandex_music_handoff_ms > 0 &&
                                    ws_now >= yandex_music_handoff_ms &&
                                    (ws_now - yandex_music_handoff_ms) < 5000);
            if (spotify_device_is_active && !in_handoff_grace) {
                yandex_music_active = 0;
                yandex_music_handoff_ms = 0;
            }
            if (spotify_device_is_active && led_fx_active_type() == LED_FX_SEARCH) {
                led_fx_trigger(LED_FX_PLAY);
            }
        } else if (str_equal(status_str, "paused")) {
            playback_status = 2;
            playback_speed = 0;
            spotify_is_active = 0;
            if (led_fx_active_type() == LED_FX_PLAY) led_fx_clear();
        } else if (str_equal(status_str, "buffering")) {
            playback_status = 3;
            playback_speed = 0;
        } else if (str_equal(status_str, "stopped")) {
            playback_status = 0;
            playback_speed = 0;
            spotify_is_active = 0;
            if (led_fx_active_type() == LED_FX_PLAY) led_fx_clear();
            last_cache_clean_ms = now_ms();
            run_controller("clean-cache-full", 0);
        }
    }

    uint64_t dur = 0;
    if (json_find_uint64(json, "duration_ms", &dur)) {
        if (dur > 0) track_duration_ms = dur;
    }

    uint64_t pos = 0;
    if (json_find_uint64(json, "position_ms", &pos)) {
        base_position_ms = pos;
        local_recorded_ms = now_ms();
    }

    int spd = 0;
    if (json_find_int(json, "speed", &spd)) {
        playback_speed = spd;
    }

    char uri_buf[128];
    int got_uri = 0;
    if (json && *json == '{') {
        const char *root_end = json_span_end(json, 0);
        const char *opts_val = json_find_key_in_obj(json, root_end, "options");
        if (opts_val && *opts_val == '{') {
            const char *opts_end = json_span_end(opts_val, root_end);
            char rep_buf[16];
            if (json_find_str_bounded(opts_val, opts_end, "repeat", rep_buf, sizeof(rep_buf))) {
                if (str_equal(rep_buf, "track")) repeat_mode = 2;
                else if (str_equal(rep_buf, "context")) repeat_mode = 1;
                else if (str_equal(rep_buf, "off")) repeat_mode = 0;
            }
        }
        const char *item_val = json_find_key_in_obj(json, root_end, "item");
        if (item_val && *item_val == '{') {
            const char *item_end = json_span_end(item_val, root_end);
            got_uri = json_find_str_bounded(item_val, item_end, "uri", uri_buf, sizeof(uri_buf));
        }
    }
    if (!got_uri) {
        got_uri = json_find_str(json, "uri", uri_buf, sizeof(uri_buf));
    }
    if (got_uri) {
        if (uri_buf[0] == 's' && uri_buf[1] == 'p' && uri_buf[2] == 'o' && uri_buf[3] == 't') {
            int is_track_uri = (str_len(uri_buf) >= 14 && mem_equal(uri_buf, "spotify:track:", 14));
            if (is_track_uri && !str_equal(current_track_uri, uri_buf)) {
                int64_t cnow = now_ms();
                if (current_track_uri[0] != 0 && (cnow - last_cache_clean_ms >= 20000)) {
                    last_cache_clean_ms = cnow;
                    run_controller("clean-cache", 0);
                }
                str_copy(current_track_uri, uri_buf, sizeof(current_track_uri));
                publish_track(current_track_uri);
                stuck_since_ms = 0;
                log_msg("[bridge] Current track: ");
                log_msg(current_track_uri);
                log_msg("\n");
            } else if (!is_track_uri && current_track_uri[0] == 0) {
                str_copy(current_track_uri, uri_buf, sizeof(current_track_uri));
                publish_track(current_track_uri);
                stuck_since_ms = 0;
                log_msg("[bridge] Current track: ");
                log_msg(current_track_uri);
                log_msg("\n");
            } else if (selection_needs_observation()) {
                publish_track(current_track_uri);
            }
        }
    }
}

#ifdef __aarch64__
static void set_led_visualizer_impl(int enable);
static inline void set_led_visualizer(int enable) { set_led_visualizer_impl(enable); }
#else
static inline void set_led_visualizer(int enable) { (void)enable; }
#endif

static int is_web_or_culinary_intent(const char *lower) {
    if (!lower) return 0;

    // Explicit music/Spotify markers take absolute priority for music playback
    if (str_contains(lower, "песн") ||
        str_contains(lower, "трек") ||
        str_contains(lower, "музык") ||
        str_contains(lower, "альбом") ||
        str_contains(lower, "плейлист") ||
        str_contains(lower, "радио") ||
        str_contains(lower, "спотифай") ||
        str_contains(lower, "spotify") ||
        str_contains(lower, "исполнител") ||
        str_contains(lower, "групп") ||
        str_contains(lower, "в очередь") ||
        str_contains(lower, "в конец очереди") ||
        str_contains(lower, "следующим") ||
        str_contains(lower, "следующей")) {
        return 0;
    }

    // Web search and general Yandex cloud intents (delegate to Yandex cloud Alice)
    if (str_contains(lower, "интернет") ||
        str_contains(lower, "яндекс") ||
        str_contains(lower, "гугл") ||
        str_contains(lower, "google") ||
        str_contains(lower, "в поиске") ||
        str_contains(lower, "поищи") ||
        str_contains(lower, "в сети") ||
        str_contains(lower, "информаци") ||
        str_contains(lower, "новост") ||
        str_contains(lower, "погод") ||
        str_contains(lower, "пробк") ||
        str_contains(lower, "курс") ||
        str_contains(lower, "перевод") ||
        str_contains(lower, "фильм") ||
        str_contains(lower, "кино") ||
        str_contains(lower, "сериал") ||
        str_contains(lower, "видео") ||
        str_contains(lower, "аптек") ||
        str_contains(lower, "магазин") ||
        str_contains(lower, "найди как") ||
        str_contains(lower, "найди где") ||
        str_contains(lower, "найди что") ||
        str_contains(lower, "найди кто") ||
        str_contains(lower, "найди почему") ||
        str_contains(lower, "найди сколько") ||
        str_contains(lower, "найди куда") ||
        str_contains(lower, "найди когда") ||
        str_contains(lower, "поищи как") ||
        str_contains(lower, "поищи где") ||
        str_contains(lower, "расскажи") ||
        str_contains(lower, "объясни")) {
        return 1;
    }

    // Culinary, cooking, and Mealie recipe intents (delegate to Quasar Bridge & Mealie)
    if (str_contains(lower, "рецепт") ||
        str_contains(lower, "ингредиент") ||
        str_contains(lower, "приготов") ||
        str_contains(lower, "готовк") ||
        str_contains(lower, "кулинар") ||
        str_contains(lower, "блюд") ||
        str_contains(lower, "шаг") ||
        str_contains(lower, "мили") ||
        str_contains(lower, "mealie") ||
        str_contains(lower, "граммовк") ||
        str_contains(lower, "пропорци") ||
        str_contains(lower, "покупк") ||
        str_contains(lower, "духовк")) {
        return 1;
    }

    return 0;
}

static int is_yandex_music_preference(const char *lower) {
    if (!lower) return 0;
    if (str_contains(lower, "яндекс") ||
        str_contains(lower, "я.музык") ||
        str_contains(lower, "моя волна") ||
        str_contains(lower, "моей волн") ||
        str_contains(lower, "радио") ||
        str_contains(lower, "фм") ||
        str_contains(lower, "fm")) {
        return 1;
    }
    return 0;
}

static void handle_voice_recognition(int quasar_fd, const char *text, unsigned long text_len) {
    char lower_buf[512];
    char raw_buf[512];
    if (text_len >= sizeof(raw_buf)) text_len = sizeof(raw_buf) - 1;
    for (unsigned long i = 0; i < text_len; i++) raw_buf[i] = text[i];
    raw_buf[text_len] = 0;

    to_lower_ru(lower_buf, raw_buf, sizeof(lower_buf));

    log_msg("[bridge] Voice command triggered: \"");
    log_msg(lower_buf);
    log_msg("\"\n");

    /* "Алиса, включи эйрплей" belongs to Quasar Bridge's built-in AirPlay
     * receiver, which answers with should_listen=false and starts nothing.
     * Without this hand-off the "включи ..." search branch below treats the
     * phrase as a catalog query for "включи airplay", cancels Alice's reply and
     * starts an unrelated track. An explicit media word keeps a real search:
     * "включи песню Airplay" still goes to Spotify. */
    if (is_airplay_command(lower_buf)) {
        log_msg("[bridge] AirPlay command, leaving for Quasar Bridge\n");
        return;
    }

    // The words after an explicit playlist command belong to its title.
    if (is_named_playlist_request(lower_buf)) {
        if (str_contains(lower_buf, "в яндекс") || str_contains(lower_buf, "на яндекс") ||
            str_contains(lower_buf, "из яндекс") || str_contains(lower_buf, "в я.музык")) {
            yandex_music_active = 1;
            yandex_music_handoff_ms = now_ms();
            spotify_is_active = 0;
            led_fx_clear();
            if (spotify_device_is_active || playback_status == 1 || playback_status == 3) {
                run_soloist_cmd("deactivate");
            }
            return;
        }
        log_msg("[bridge] Action: PLAYLIST SEARCH\n");
        trigger_alice_suppression(quasar_fd, 5000);
        yandex_music_active = 0;
        yandex_music_handoff_ms = 0;
        spotify_is_active = 1;
        led_fx_trigger(LED_FX_SEARCH);
        run_search_and_play(lower_buf);
        return;
    }

    if (is_web_or_culinary_intent(lower_buf)) {
        log_msg("[bridge] Web search or culinary intent, leaving for Alice / Quasar Bridge\n");
        return;
    }

    if (is_yandex_music_preference(lower_buf)) {
        log_msg("[bridge] Yandex Music/Radio preference requested, leaving to native Alice\n");
        yandex_music_active = 1;
        yandex_music_handoff_ms = now_ms();
        spotify_is_active = 0;
        led_fx_clear();
        if (spotify_device_is_active || playback_status == 1 || playback_status == 3) {
            run_soloist_cmd("deactivate");
        }
        return;
    }

    // Check "что играет" / track info query (only when not an explicit search like "включи песню ...")
    if (!yandex_music_active && !is_explicit_search(lower_buf) &&
        (str_contains(lower_buf, "что играет") ||
         str_contains(lower_buf, "что сейчас играет") ||
         str_contains(lower_buf, "что играет сейчас") ||
         str_contains(lower_buf, "какой трек") ||
         str_contains(lower_buf, "какая песня") ||
         str_contains(lower_buf, "какая музыка") ||
         str_contains(lower_buf, "что за трек") ||
         str_contains(lower_buf, "что за песня") ||
         str_contains(lower_buf, "что за музыка") ||
         str_contains(lower_buf, "что за мелодия") ||
         str_contains(lower_buf, "кто поет") ||
         str_contains(lower_buf, "кто поёт") ||
         str_contains(lower_buf, "кто это поет") ||
         str_contains(lower_buf, "кто это поёт") ||
         str_contains(lower_buf, "назови трек") ||
         str_contains(lower_buf, "назови песню") ||
         str_contains(lower_buf, "текущий трек"))) {
        log_msg("[bridge] Action: WHAT IS PLAYING (TTS)\n");
        char phrase[320];
        if ((playback_status == 1 || playback_status == 2 || playback_status == 3) &&
            current_track_name[0] != 0) {
            char *p = phrase;
            const char *pref = (playback_status == 2) ? "На паузе " : "Сейчас играет ";
            while (*pref) *p++ = *pref++;
            const char *t = current_track_name;
            while (*t && (p - phrase) < 180) *p++ = *t++;
            if (current_artist_name[0] != 0) {
                const char *mid = ", исполнитель ";
                while (*mid) *p++ = *mid++;
                const char *a = current_artist_name;
                while (*a && (p - phrase) < 280) *p++ = *a++;
            }
            *p = 0;
        } else {
            str_copy(phrase, "Сейчас ничего не играет", sizeof(phrase));
        }
        send_alice_tts(quasar_fd, phrase);
        return;
    }

    // Queue commands ("поставь следующим ...", "добавь в очередь ...") run before
    // is_explicit_search() so the leading "поставь"/"включи" does not replace playback.
    char queue_query[512];
    if (has_enqueue_query(lower_buf, queue_query, sizeof(queue_query))) {
        log_msg("[bridge] Action: ENQUEUE TRACK query='");
        log_msg(queue_query);
        log_msg("'\n");
        trigger_alice_suppression(quasar_fd, 4000);
        yandex_music_active = 0;
        yandex_music_handoff_ms = 0;
        spotify_is_active = 1;
        led_fx_trigger(LED_FX_SEARCH);
        run_controller("enqueue", queue_query);
        return;
    }

    // Track restart commands ("включи сначала", "начни сначала") run before
    // is_explicit_search() while preserving explicit song title searches.
    if (!yandex_music_active && (spotify_is_active || spotify_device_is_active) &&
        is_restart_track_request(lower_buf)) {
        log_msg("[bridge] Action: RESTART TRACK (seek 0)\n");
        trigger_alice_suppression(quasar_fd, 2500);
        spotify_is_active = 1;
        led_fx_trigger(LED_FX_PREV);
        run_soloist_cmd2("seek", "0");
        return;
    }

    // An explicit catalog search ("включи песню ...") must not be hijacked by control words in the title
    if (!is_explicit_search(lower_buf)) {
        // Check LED Visualizer / Equalizer OFF keywords (NO "выключи" or "отключи" to avoid smart home collisions)
        if (str_contains(lower_buf, "стоп дискотек") ||
            str_contains(lower_buf, "останови дискотек") ||
            str_contains(lower_buf, "стоп эквалайзер") ||
            str_contains(lower_buf, "останови эквалайзер") ||
            str_contains(lower_buf, "стоп светомузык") ||
            str_contains(lower_buf, "останови светомузык") ||
            str_contains(lower_buf, "погаси подсветк") ||
            str_contains(lower_buf, "хватит светить")) {
            log_msg("[bridge] Action: LED VISUALIZER OFF\n");
            trigger_alice_suppression(quasar_fd, 2500);
            set_led_visualizer(0);
            return;
        }

        // Check LED Visualizer / Equalizer ON keywords (STRICTLY NO "включи", "выключи", or "отключи")
        if (!str_contains(lower_buf, "включ") &&
            !str_contains(lower_buf, "выключ") &&
            !str_contains(lower_buf, "отключ")) {
            if (str_contains(lower_buf, "зажигай") ||
                str_contains(lower_buf, "зажги") ||
                str_contains(lower_buf, "покажи эквалайзер") ||
                str_contains(lower_buf, "режим дискотек") ||
                str_contains(lower_buf, "светомузык") ||
                str_contains(lower_buf, "эквалайзер") ||
                str_contains(lower_buf, "дискотек")) {
                log_msg("[bridge] Action: LED VISUALIZER ON (Nothing Phone Equalizer mode)\n");
                trigger_alice_suppression(quasar_fd, 2500);
                set_led_visualizer(1);
                return;
            }
        }

        // Check PAUSE / STOP keywords (Instant Action)
        // Media words belong to Spotify only while this speaker is the active
        // Spotify Connect device. Without this guard an ordinary "Алиса, стоп"
        // aimed at a timer or alarm is consumed here, Alice's cloud reply is
        // suppressed, and her dialog is cancelled; "дальше" during a Yandex
        // Music playlist likewise skips a Spotify track instead. Events from
        // another device of the same account arrive with is_active=false, so
        // this also stops a phone or car from steering the speaker.
        if (!yandex_music_active && (spotify_is_active || spotify_device_is_active)) {
            if (((spotify_device_is_active && (playback_status == 1 || playback_status == 3)) || spotify_is_active) &&
                (str_contains(lower_buf, "пауз") ||
                 str_contains(lower_buf, "стоп") ||
                 str_contains(lower_buf, "хватит") ||
                 str_contains(lower_buf, "замолчи") ||
                 str_contains(lower_buf, "останови") ||
                 str_contains(lower_buf, "выключи музыку") ||
                 str_contains(lower_buf, "отключи музыку"))) {
                log_msg("[bridge] Action: PAUSE\n");
                trigger_alice_suppression(quasar_fd, 3000);
                spotify_is_active = 0;
                led_fx_clear();
                run_soloist_cmd("pause");
                return;
            }

            // Check NEXT TRACK keywords (Instant Action)
            if (str_contains(lower_buf, "дальше") ||
                str_contains(lower_buf, "следующ") ||
                str_contains(lower_buf, "переключи") ||
                str_contains(lower_buf, "пропусти")) {
                log_msg("[bridge] Action: NEXT TRACK\n");
                trigger_alice_suppression(quasar_fd, 3000);
                spotify_is_active = 1;
                led_fx_trigger(LED_FX_NEXT);
                run_soloist_cmd("next");
                return;
            }

            // Check PREVIOUS TRACK keywords (Instant Action)
            if (str_contains(lower_buf, "назад") ||
                str_contains(lower_buf, "предыдущ") ||
                str_contains(lower_buf, "прошл")) {
                log_msg("[bridge] Action: PREVIOUS TRACK\n");
                trigger_alice_suppression(quasar_fd, 3000);
                spotify_is_active = 1;
                led_fx_trigger(LED_FX_PREV);
                run_soloist_cmd("prev");
                return;
            }

            // Check SHUFFLE keywords (Instant Action)
            if (str_contains(lower_buf, "перемешай") ||
                str_contains(lower_buf, "случайный порядок") ||
                str_contains(lower_buf, "случайном порядке") ||
                str_contains(lower_buf, "включи перемешивание")) {
                log_msg("[bridge] Action: SHUFFLE ON\n");
                trigger_alice_suppression(quasar_fd, 2500);
                run_soloist_cmd2("shuffle", "on");
                return;
            }
            if (str_contains(lower_buf, "по порядку") ||
                str_contains(lower_buf, "выключи перемешивание") ||
                str_contains(lower_buf, "отключи перемешивание")) {
                log_msg("[bridge] Action: SHUFFLE OFF\n");
                trigger_alice_suppression(quasar_fd, 2500);
                run_soloist_cmd2("shuffle", "off");
                return;
            }

            // Check REPEAT keywords (Instant Action)
            if (str_contains(lower_buf, "повторяй трек") ||
                str_contains(lower_buf, "повторяй песню") ||
                str_contains(lower_buf, "повторяй этот трек") ||
                str_contains(lower_buf, "повторяй эту песню") ||
                str_contains(lower_buf, "повтор трека") ||
                str_contains(lower_buf, "повтор песни") ||
                str_contains(lower_buf, "повтор одного")) {
                log_msg("[bridge] Action: REPEAT TRACK\n");
                trigger_alice_suppression(quasar_fd, 2500);
                run_soloist_cmd2("repeat", "track");
                return;
            }
            if (str_contains(lower_buf, "зацикли") ||
                str_contains(lower_buf, "повторяй") ||
                str_contains(lower_buf, "включи повтор")) {
                log_msg("[bridge] Action: REPEAT ON\n");
                trigger_alice_suppression(quasar_fd, 2500);
                run_soloist_cmd2("repeat", "context");
                return;
            }
            if (str_contains(lower_buf, "не повторяй") ||
                str_contains(lower_buf, "выключи повтор") ||
                str_contains(lower_buf, "отключи повтор")) {
                log_msg("[bridge] Action: REPEAT OFF\n");
                trigger_alice_suppression(quasar_fd, 2500);
                run_soloist_cmd2("repeat", "off");
                return;
            }
        }

        // Resume checks (Fast Action)
        if (str_has_word_prefix(lower_buf, "продолж") ||
            str_has_word(lower_buf, "играй") ||
            str_has_word_prefix(lower_buf, "возобнов") ||
            str_has_word(lower_buf, "заиграй") ||
            str_contains(lower_buf, "вруби музыку") ||
            (str_contains(lower_buf, "включи") && str_contains(lower_buf, "музык"))) {
            log_msg("[bridge] Action: RESUME\n");
            trigger_alice_suppression(quasar_fd, 2500);
            yandex_music_active = 0;
            yandex_music_handoff_ms = 0;
            spotify_is_active = 1;
            led_fx_trigger(LED_FX_PLAY);
            run_search_and_play("включи музыку");
            return;
        }
    }

    // Censorship toggle
    if (str_contains(lower_buf, "цензур") ||
        str_contains(lower_buf, "без мата") ||
        str_contains(lower_buf, "с матом") ||
        str_contains(lower_buf, "детский режим") ||
        str_contains(lower_buf, "взрослый режим")) {
        trigger_alice_suppression(quasar_fd, 2500);
        if (str_contains(lower_buf, "выключ") ||
            str_contains(lower_buf, "отключ") ||
            str_contains(lower_buf, "с матом") ||
            str_contains(lower_buf, "взрослый режим")) {
            log_msg("[bridge] Action: CENSORSHIP OFF\n");
            sys_unlink(get_censor_path());
        } else {
            log_msg("[bridge] Action: CENSORSHIP ON\n");
            int fd = sys_open(get_censor_path(), 0100 | 01 | 01000 /* O_CREAT|O_WRONLY|O_TRUNC */, 0644);
            if (fd >= 0) {
                sys_write(fd, "1\n", 2);
                sys_close(fd);
            }
        }
        return;
    }

    // Check PLAY / SEARCH / TRACK keywords (Search Action)
    if (str_contains(lower_buf, "включи") ||
        str_contains(lower_buf, "поставь") ||
        str_contains(lower_buf, "сыграй") ||
        str_contains(lower_buf, "запусти") ||
        str_contains(lower_buf, "найди") ||
        str_contains(lower_buf, "вруби") ||
        str_contains(lower_buf, "слушать")) {

        // Explicit media-type/service markers keep track/artist names containing
        // smart-home words (e.g. "включи трек светофор") routed to Spotify, while
        // excluding generic "музык" so "поставь будильник с музыкой" stays with Alice.
        int has_explicit_media =
            str_has_word_prefix(lower_buf, "песн") ||
            str_has_word_prefix(lower_buf, "трек") ||
            str_has_word_prefix(lower_buf, "альбом") ||
            str_has_word_prefix(lower_buf, "групп") ||
            str_has_word_prefix(lower_buf, "исполнител") ||
            str_has_word_prefix(lower_buf, "артист") ||
            str_contains(lower_buf, "спотифай") ||
            str_contains(lower_buf, "spotify");

        // Ignore household smart home commands (word-aware: narrow nouns + safe prefixes
        // so unrelated titles like "светофор", "шторм", or "лампасы" are not dropped).
        if (!has_explicit_media &&
            (str_has_word(lower_buf, "свет") ||
             str_has_word(lower_buf, "света") ||
             str_has_word(lower_buf, "свету") ||
             str_has_word(lower_buf, "светом") ||
             str_has_word(lower_buf, "свете") ||
             str_has_word_prefix(lower_buf, "светильник") ||
             str_has_word(lower_buf, "лампа") ||
             str_has_word(lower_buf, "лампу") ||
             str_has_word(lower_buf, "лампы") ||
             str_has_word(lower_buf, "лампочку") ||
             str_has_word(lower_buf, "лампочки") ||
             str_has_word(lower_buf, "лампочка") ||
             str_has_word_prefix(lower_buf, "люстр") ||
             str_has_word_prefix(lower_buf, "розетк") ||
             str_has_word_prefix(lower_buf, "ночник") ||
             str_has_word_prefix(lower_buf, "подсветк") ||
             str_has_word(lower_buf, "бра") ||
             str_has_word_prefix(lower_buf, "торшер") ||
             str_has_word_prefix(lower_buf, "гирлянд") ||
             str_has_word_prefix(lower_buf, "чайник") ||
             str_has_word_prefix(lower_buf, "пылесос") ||
             str_has_word_prefix(lower_buf, "кондиционер") ||
             str_has_word_prefix(lower_buf, "телевизор") ||
             str_has_word(lower_buf, "тв") ||
             str_has_word_prefix(lower_buf, "таймер") ||
             str_has_word_prefix(lower_buf, "будильник") ||
             str_has_word_prefix(lower_buf, "блютуз") ||
             str_has_word_prefix(lower_buf, "bluetooth") ||
             str_has_word_prefix(lower_buf, "увлажнител") ||
             str_has_word_prefix(lower_buf, "очистител") ||
             str_has_word_prefix(lower_buf, "обогревател") ||
             str_has_word_prefix(lower_buf, "вентилятор") ||
             str_has_word_prefix(lower_buf, "робот") ||
             str_has_word_prefix(lower_buf, "кофеварк") ||
             str_has_word_prefix(lower_buf, "кофемашин") ||
             str_has_word_prefix(lower_buf, "карниз") ||
             str_has_word(lower_buf, "шторы") ||
             str_has_word(lower_buf, "штору") ||
             str_has_word(lower_buf, "штора") ||
             str_has_word(lower_buf, "шторку") ||
             str_has_word(lower_buf, "шторки") ||
             str_has_word(lower_buf, "шторка") ||
             str_has_word_prefix(lower_buf, "выключател") ||
             str_has_word(lower_buf, "реле"))) {
            log_msg("[bridge] Household smart command, ignoring\n");
            return;
        }

        trigger_alice_suppression(quasar_fd, 5000);
        yandex_music_active = 0;
        yandex_music_handoff_ms = 0;
        spotify_is_active = 1;
        led_fx_trigger(LED_FX_SEARCH);

        log_msg("[bridge] Action: SEARCH & PLAY query='");
        log_msg(lower_buf);
        log_msg("'\n");
        run_search_and_play(lower_buf);
        return;
    }
}

/* =========================================================================
 * LED Equalizer / Visualizer & Feedback FX Subsystem
 * ========================================================================= */
#include "led-subsystem.h"

static int64_t g_last_error_tts_ms = 0;

static void check_tts_request(int quasar_fd) {
    const char *req_path = get_tts_req_path();
    int fd = sys_open(req_path, 0 /* O_RDONLY */, 0);
    if (fd < 0) return;

    char buf[128];
    long n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    sys_unlink(req_path);
    if (n <= 0 || quasar_fd < 0) return;
    buf[n] = 0;

    int start = 0;
    while (buf[start] == ' ' || buf[start] == '\t' || buf[start] == '\r' || buf[start] == '\n') start++;
    int end = (int)str_len(buf);
    while (end > start && (buf[end - 1] == ' ' || buf[end - 1] == '\t' || buf[end - 1] == '\r' || buf[end - 1] == '\n')) {
        buf[--end] = 0;
    }
    const char *code = buf + start;
    if (!code[0]) return;

    const char *phrase = 0;
    if (str_equal(code, "cfg")) phrase = "Нет настроек Спотифай";
    else if (str_equal(code, "net")) phrase = "Нет связи со Спотифай";
    else if (str_equal(code, "limit")) phrase = "Спотифай просит подождать";
    else if (str_equal(code, "auth")) phrase = "Нужно заново войти в Спотифай";
    if (!phrase) return;

    int64_t tnow = now_ms();
    if (g_last_error_tts_ms != 0 && tnow >= g_last_error_tts_ms && (tnow - g_last_error_tts_ms) < 15000) {
        log_msg("[bridge] Error TTS skipped due to 15s cooldown\n");
        return;
    }
    g_last_error_tts_ms = tnow;
    send_alice_tts(quasar_fd, phrase);
}

static uint8_t quasar_buf[65536];
static unsigned long quasar_buf_pos = 0;
static uint8_t quasar_io_buf[16384];
static unsigned long quasar_io_buf_pos = 0;
static uint8_t quasar_vol_buf[16384];
static unsigned long quasar_vol_buf_pos = 0;
static uint8_t ws_buf[8192];
static unsigned long ws_buf_pos = 0;
static struct voice_request_state voice_requests;

static void handle_quasar_io_stream(int *fd_ptr, int revents, int64_t now) {
    if (!fd_ptr || *fd_ptr < 0) return;
    if (revents & 0x0001) {
        long n = sys_read(*fd_ptr, quasar_io_buf + quasar_io_buf_pos, sizeof(quasar_io_buf) - quasar_io_buf_pos);
        if (n <= 0) {
            sys_close(*fd_ptr);
            *fd_ptr = -1;
            quasar_io_buf_pos = 0;
            return;
        }
        quasar_io_buf_pos += (unsigned long)n;
        while (quasar_io_buf_pos >= 4) {
            uint32_t fl = (uint32_t)quasar_io_buf[0] |
                          ((uint32_t)quasar_io_buf[1] << 8) |
                          ((uint32_t)quasar_io_buf[2] << 16) |
                          ((uint32_t)quasar_io_buf[3] << 24);
            if (fl > sizeof(quasar_io_buf) - 4) {
                quasar_io_buf_pos = 0;
                break;
            }
            if (quasar_io_buf_pos < 4 + fl) break;
            int media_state = 0;
            if (parse_quasar_io_media_state(quasar_io_buf + 4, fl, &media_state) &&
                media_state == 2 && !yandex_music_active) {
                log_msg("[bridge] Native Quasar media playback detected, yielding Spotify\n");
                yandex_music_active = 1;
                yandex_music_handoff_ms = now;
                spotify_is_active = 0;
                led_fx_clear();
                if (spotify_device_is_active || playback_status == 1 || playback_status == 3) {
                    run_soloist_cmd("deactivate");
                }
            }
            unsigned long rem = quasar_io_buf_pos - (4 + fl);
            for (unsigned long i = 0; i < rem; i++) quasar_io_buf[i] = quasar_io_buf[4 + fl + i];
            quasar_io_buf_pos = rem;
        }
    } else if (revents & (0x0008 | 0x0010 | 0x0020)) {
        sys_close(*fd_ptr);
        *fd_ptr = -1;
        quasar_io_buf_pos = 0;
    }
}

static int connect_local_tcp(uint16_t port_be) {
    int fd = sys_socket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    addr.sin_family = 2;
    addr.sin_port = port_be;
    addr.sin_addr = 0x0100007f; // 127.0.0.1
    for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;
    if (sys_connect(fd, &addr, sizeof(addr)) == 0) return fd;
    sys_close(fd);
    return -1;
}

#define MAX_INPUT_DEVS 8
static int input_fds[MAX_INPUT_DEVS];
static int num_input_fds = 0;

static void open_input_devices(void) {
    for (int i = 0; i < num_input_fds; i++) {
        if (input_fds[i] >= 0) sys_close(input_fds[i]);
    }
    num_input_fds = 0;

    char path[] = "/dev/input/event0";
    char name[64];

    for (int i = 0; i < 8 && num_input_fds < MAX_INPUT_DEVS; i++) {
        path[16] = '0' + i;
        int fd = sys_open(path, 0 /* O_RDONLY */ | 00004000 /* O_NONBLOCK */, 0);
        if (fd < 0) continue;

        for (int k = 0; k < 64; k++) name[k] = 0;
        int ret = sys_ioctl(fd, 0x80404506 /* EVIOCGNAME(64) */, name);
        log_msg("[bridge] Opened input device: ");
        log_msg(path);
        if (ret >= 0) {
            log_msg(" (\"");
            log_msg(name);
            log_msg("\")\n");
        } else {
            log_msg("\n");
        }
        input_fds[num_input_fds++] = fd;
    }
}

static inline long sys_readlinkat(const char *path, char *buf, unsigned long sz) {
    register long x8 __asm__("x8") = 78; // readlinkat
    register long x0 __asm__("x0") = -100; // AT_FDCWD
    register long x1 __asm__("x1") = (long)path;
    register long x2 __asm__("x2") = (long)buf;
    register long x3 __asm__("x3") = (long)sz;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

static void join_path(char *dst, unsigned long max, const char *a, const char *b) {
    unsigned long pos = 0;
    while (*a && pos + 1 < max) dst[pos++] = *a++;
    if (pos > 0 && dst[pos - 1] != '/' && *b && *b != '/' && pos + 1 < max) dst[pos++] = '/';
    while (*b && pos + 1 < max) dst[pos++] = *b++;
    dst[pos] = 0;
}

static void lookup_proc_env(const char *ebuf, long elen, const char *key, char *dst, unsigned long max) {
    dst[0] = 0;
    unsigned long klen = str_len(key);
    long i = 0;
    while (i < elen) {
        long start = i;
        while (i < elen && ebuf[i] != 0) i++;
        long len = i - start;
        if ((unsigned long)len > klen + 1 && ebuf[start + klen] == '=' && mem_equal(ebuf + start, key, klen)) {
            unsigned long vlen = (unsigned long)(len - (long)klen - 1);
            if (vlen >= max) vlen = max - 1;
            for (unsigned long k = 0; k < vlen; k++) dst[k] = ebuf[start + klen + 1 + k];
            dst[vlen] = 0;
            return;
        }
        i++;
    }
}

static void push_yam_env(const char *key, const char *val) {
    if (!val || !val[0] || g_yam_env_count >= 5) return;
    char *dst = g_yam_env[g_yam_env_count++];
    unsigned long pos = 0, max = sizeof(g_yam_env[0]);
    while (*key && pos + 2 < max) dst[pos++] = *key++;
    dst[pos++] = '=';
    while (*val && pos + 1 < max) dst[pos++] = *val++;
    dst[pos] = 0;
}

static void init_runtime_paths(void) {
    char ebuf[8192];
    long elen = 0;
    int efd = sys_open("/proc/self/environ", 0, 0);
    if (efd >= 0) {
        elen = sys_read(efd, ebuf, sizeof(ebuf) - 1);
        sys_close(efd);
        if (elen < 0) elen = 0;
        ebuf[elen] = 0;
    }

    char yam_root[256], yam_pkg[256], yam_etc[256], yam_var[256], yam_run[256];
    lookup_proc_env(ebuf, elen, "YAM_ROOT", yam_root, sizeof(yam_root));
    lookup_proc_env(ebuf, elen, "YAM_PKG", yam_pkg, sizeof(yam_pkg));
    lookup_proc_env(ebuf, elen, "YAM_ETC", yam_etc, sizeof(yam_etc));
    lookup_proc_env(ebuf, elen, "YAM_VAR", yam_var, sizeof(yam_var));
    lookup_proc_env(ebuf, elen, "YAM_RUN", yam_run, sizeof(yam_run));

    char exe_buf[512];
    for (unsigned long i = 0; i < sizeof(exe_buf); i++) exe_buf[i] = 0;
    long exelen = sys_readlinkat("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (exelen > 0) exe_buf[exelen] = 0;

    char pkg_share[256] = {0};
    if (yam_pkg[0]) {
        join_path(pkg_share, sizeof(pkg_share), yam_pkg, "share/horohi.soloist");
    } else if (str_contains(exe_buf, "/share/horohi.soloist/bin/")) {
        const char *needle = "/bin/soloist-bridge";
        for (int i = 0; exe_buf[i]; i++) {
            if (mem_equal(exe_buf + i, needle, 19)) {
                unsigned long n = (unsigned long)i;
                if (n >= sizeof(pkg_share)) n = sizeof(pkg_share) - 1;
                for (unsigned long k = 0; k < n; k++) pkg_share[k] = exe_buf[k];
                pkg_share[n] = 0;
                break;
            }
        }
    } else {
        int probe = sys_open("./share/horohi.soloist/bin/soloist", 0, 0);
        if (probe >= 0) {
            sys_close(probe);
            str_copy(pkg_share, "./share/horohi.soloist", sizeof(pkg_share));
        }
    }

    if (pkg_share[0] || yam_var[0] || yam_etc[0] || str_contains(exe_buf, "/yam/")) {
        if (!yam_root[0]) str_copy(yam_root, "/data/yam", sizeof(yam_root));
        if (!pkg_share[0]) join_path(pkg_share, sizeof(pkg_share), yam_root, "share/horohi.soloist");
        if (!yam_var[0]) join_path(yam_var, sizeof(yam_var), yam_root, "var");
        if (!yam_etc[0]) join_path(yam_etc, sizeof(yam_etc), yam_root, "etc");

        char data_base[256], conf_base[256];
        join_path(data_base, sizeof(data_base), yam_var, "horohi.soloist");
        join_path(conf_base, sizeof(conf_base), yam_etc, "horohi.soloist");

        join_path(g_ctrl_sh, sizeof(g_ctrl_sh), pkg_share, "scripts/control.sh");
        join_path(g_censor_path, sizeof(g_censor_path), data_base, "censorship");
        join_path(g_req_dir, sizeof(g_req_dir), data_base, "requests");
        join_path(g_sel_pending_path, sizeof(g_sel_pending_path), g_req_dir, "selection.pending");
        join_path(g_sel_path, sizeof(g_sel_path), g_req_dir, "selection");
        join_path(g_track_path, sizeof(g_track_path), g_req_dir, "track");
        join_path(g_track_tmp_path, sizeof(g_track_tmp_path), g_req_dir, "track.tmp");
        join_path(g_gen_path, sizeof(g_gen_path), g_req_dir, "generation");
        join_path(g_led_state_path, sizeof(g_led_state_path), g_req_dir, "led_state");
        join_path(g_led_req_path, sizeof(g_led_req_path), g_req_dir, "led");
        join_path(g_tts_req_path, sizeof(g_tts_req_path), g_req_dir, "tts");
        join_path(g_conf_path, sizeof(g_conf_path), conf_base, "spotify_api.conf");

        sys_mkdir(yam_var, 0755);
        sys_mkdir(data_base, 0755);
        sys_mkdir(g_req_dir, 0700);

        push_yam_env("YAM_ROOT", yam_root);
        if (yam_pkg[0]) push_yam_env("YAM_PKG", yam_pkg);
        push_yam_env("YAM_ETC", yam_etc);
        push_yam_env("YAM_VAR", yam_var);
        if (yam_run[0]) push_yam_env("YAM_RUN", yam_run);

        is_yam_mode = 1;
        log_msg("[bridge] soloist-bridge v15 started (YAM v2 mode: ");
        log_msg(pkg_share);
        log_msg(")\n");
        return;
    }

    int test_fd = sys_open("./bin/soloist", 0 /* O_RDONLY */, 0);
    if (test_fd >= 0) {
        sys_close(test_fd);
        is_yam_mode = 1;
        log_msg("[bridge] soloist-bridge v15 started (YAM plugin mode: relative paths)\n");
    } else {
        log_msg("[bridge] soloist-bridge v15 started (Standalone mode: /data/opt/soloist)\n");
    }
}

void _start(void) {
    init_runtime_paths();

    read_led_config();
    led_probe();
    duck_shm_init();

    int quasar_fd = -1;
    int quasar_io_fd = -1;
    int quasar_vol_fd = -1;
    int soloist_ws_fd = -1;
    int soloist_ws_handshake = 0;
    int64_t last_quasar_retry = -2000;
    int64_t last_quasar_io_retry = -5000;
    int64_t last_quasar_vol_retry = -5000;
    int64_t last_input_retry = -2000;
    int64_t last_soloist_ws_retry = -3000;

    int touch_down_start = 0;
    int64_t touch_down_time = 0;
    int touch_is_down = 0;
    int64_t last_touch_release_time = 0;

    while (1) {
        service_controllers();

        int64_t now = now_ms();

        // Fail-safe release if TTS/SPEAKING completion event was ever dropped
        if (g_tts_hold_until_ms > 0 && now >= g_tts_hold_until_ms) {
            g_tts_hold_until_ms = 0;
            g_tts_speaking_seen = 0;
            set_alsa_yield(0, now);
            set_ducking(0, now);
        }
        if (g_alsa_yield_since_ms > 0 && now - g_alsa_yield_since_ms >= 30000) {
            set_alsa_yield(0, now);
            set_ducking(0, now);
        }

        // 1. Ensure Quasar connections (9875 aliced, 9910 iohub_clients, 9921 stereo_pair volume)
        if (quasar_fd < 0 && now - last_quasar_retry >= 2000) {
            last_quasar_retry = now;
            int fd = connect_local_tcp(0x9326); // 9875 in big-endian
            if (fd >= 0) {
                quasar_fd = fd;
                quasar_buf_pos = 0;
                log_msg("[bridge] Connected to Quasar on 127.0.0.1:9875\n");
            }
        }
        if (quasar_io_fd < 0 && now - last_quasar_io_retry >= 5000) {
            last_quasar_io_retry = now;
            int fd = connect_local_tcp(0xb626); // 9910 in big-endian
            if (fd >= 0) {
                quasar_io_fd = fd;
                log_msg("[bridge] Connected to Quasar IOHub on 127.0.0.1:9910\n");
            }
        }
        if (quasar_vol_fd < 0 && now - last_quasar_vol_retry >= 5000) {
            last_quasar_vol_retry = now;
            int fd = connect_local_tcp(0xc126); // 9921 in big-endian
            if (fd >= 0) {
                quasar_vol_fd = fd;
                quasar_vol_buf_pos = 0;
                log_msg("[bridge] Connected to Quasar Volume state on 127.0.0.1:9921\n");
            }
        }

        // 2. Ensure Input devices are open (Multi-device monitoring across all Stations)
        if (num_input_fds == 0 && now - last_input_retry >= 2000) {
            last_input_retry = now;
            open_input_devices();
        }

        // 3. Ensure Soloist WS connection
        if (soloist_ws_fd < 0 && now - last_soloist_ws_retry >= 3000) {
            last_soloist_ws_retry = now;
            int fd = connect_local_tcp(0x8223); // 9090 in big-endian
            if (fd >= 0) {
                soloist_ws_fd = fd;
                soloist_ws_handshake = 0;
                ws_initial_vol_synced = 0;
                current_soloist_vol = -1;
                ws_buf_pos = 0;
                const char *req = "GET / HTTP/1.1\r\nHost: 127.0.0.1:9090\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
                sys_write(soloist_ws_fd, req, str_len(req));
                log_msg("[bridge] Connected to Soloist WS on 127.0.0.1:9090\n");
            }
        }

        // 4. Poll on available fds
        struct pollfd pfd[4 + MAX_INPUT_DEVS];
        int nfds = 0;
        int quasar_idx = -1;
        int quasar_io_idx = -1;
        int quasar_vol_idx = -1;
        int soloist_ws_idx = -1;
        int input_start_idx = -1;

        if (quasar_fd >= 0) {
            quasar_idx = nfds;
            pfd[nfds].fd = quasar_fd;
            pfd[nfds].events = 0x0001; // POLLIN
            pfd[nfds].revents = 0;
            nfds++;
        }
        if (quasar_io_fd >= 0) {
            quasar_io_idx = nfds;
            pfd[nfds].fd = quasar_io_fd;
            pfd[nfds].events = 0x0001; // POLLIN
            pfd[nfds].revents = 0;
            nfds++;
        }
        if (quasar_vol_fd >= 0) {
            quasar_vol_idx = nfds;
            pfd[nfds].fd = quasar_vol_fd;
            pfd[nfds].events = 0x0001; // POLLIN
            pfd[nfds].revents = 0;
            nfds++;
        }
        if (soloist_ws_fd >= 0) {
            soloist_ws_idx = nfds;
            pfd[nfds].fd = soloist_ws_fd;
            pfd[nfds].events = 0x0001; // POLLIN
            pfd[nfds].revents = 0;
            nfds++;
        }
        if (num_input_fds > 0) {
            input_start_idx = nfds;
            for (int i = 0; i < num_input_fds; i++) {
                pfd[nfds].fd = input_fds[i];
                pfd[nfds].events = 0x0001; // POLLIN
                pfd[nfds].revents = 0;
                nfds++;
            }
        }

        struct timespec tmo;
        tmo.tv_sec = 0;
        if (g_led_hw.detected && !alice_owns_leds(now)) {
            if (g_led_fx_enabled && g_led_fx_type != LED_FX_NONE) {
                tmo.tv_nsec = 16000000;
            } else if (g_led_enabled && playback_status == 1 && spotify_device_is_active) {
                tmo.tv_nsec = 33000000;
            } else {
                tmo.tv_nsec = 50000000;
            }
        } else {
            tmo.tv_nsec = 50000000;
        }

        int ret = sys_ppoll(pfd, nfds, &tmo, 0, 0);
        if (ret > 0) {
            // Check Quasar socket
            if (quasar_idx >= 0 && (pfd[quasar_idx].revents & 0x0001)) {
                long n = sys_read(quasar_fd, quasar_buf + quasar_buf_pos, sizeof(quasar_buf) - quasar_buf_pos);
                if (n <= 0) {
                    log_msg("[bridge] Quasar connection closed, reconnecting...\n");
                    sys_close(quasar_fd);
                    quasar_fd = -1;
                    quasar_buf_pos = 0;
                    g_alice_state = 0;
                    g_led_alice_yield_until_ms = 0;
                    g_tts_hold_until_ms = 0;
                    g_tts_speaking_seen = 0;
                    set_alsa_yield(0, now);
                    set_ducking(0, now);
                } else {
                    quasar_buf_pos += n;

                    // Parse framed messages: [uint32_le length][payload]
                    while (quasar_buf_pos >= 4) {
                        uint32_t frame_len = (uint32_t)quasar_buf[0] |
                                             ((uint32_t)quasar_buf[1] << 8) |
                                             ((uint32_t)quasar_buf[2] << 16) |
                                             ((uint32_t)quasar_buf[3] << 24);

                        if (frame_len > sizeof(quasar_buf) - 4) {
                            log_msg("[bridge] Frame length error, resetting buffer\n");
                            quasar_buf_pos = 0;
                            break;
                        }

                        if (quasar_buf_pos < 4 + frame_len) {
                            break;
                        }

                        static int logged_quasar_schema = 0;
                        const uint8_t *frame_body = quasar_buf + 4;
                        uint64_t f253_type = 0;
                        if (find_field(frame_body, frame_len, 3, 2, 0, 0, 0) ||
                            find_field(frame_body, frame_len, 210, 2, 0, 0, 0)) {
                            quasar_uses_f252 = 0;
                        } else if (find_field(frame_body, frame_len, 253, 0, 0, 0, &f253_type) &&
                                   (f253_type == 3 || f253_type == 210)) {
                            quasar_uses_f252 = 1;
                        }
                        if (!logged_quasar_schema && quasar_uses_f252 >= 0) {
                            logged_quasar_schema = 1;
                            log_msg(quasar_uses_f252 == 1
                                ? "[bridge] Quasar protobuf schema: SharedMessage wrapper (field 252/253)\n"
                                : "[bridge] Quasar protobuf schema: direct field 3/210\n");
                        }
                        const uint8_t *state_payload = 0;
                        unsigned long state_len = 0;
                        if (voice_state_payload(frame_body, frame_len, &state_payload, &state_len)) {
                            uint64_t alice_state = 0;
                            if (find_field(state_payload, state_len, 1, 0, 0, 0, &alice_state)) {
                                int64_t st_now = now_ms();
                                if (is_alice_visual_state(alice_state)) {
                                    g_alice_state = alice_state;
                                    if (g_led_alice_yield_until_ms < st_now + 450) {
                                        g_led_alice_yield_until_ms = st_now + 450;
                                    }
                                    led_reset_internal_state();
                                } else {
                                    int64_t settle_ms = (st_now < cancel_alice_until_ms) ? 150 : 450;
                                    if (is_alice_visual_state(g_alice_state) && g_led_alice_yield_until_ms < st_now + settle_ms) {
                                        g_led_alice_yield_until_ms = st_now + settle_ms;
                                    }
                                    g_alice_state = alice_state;
                                }
                                if (alice_state == 1) {
                                    // User said "Alice" (wake-word spotted): clear suppression window immediately
                                    // so the dialog is not killed and user can speak; duck Spotify audio to 20%.
                                    cancel_alice_until_ms = 0;
                                    g_tts_hold_until_ms = 0;
                                    g_tts_speaking_seen = 0;
                                    set_alsa_yield(0, st_now);
                                    set_ducking(1, st_now);
                                } else if (alice_state == 2 || alice_state == 3) {
                                    if (st_now < cancel_alice_until_ms) {
                                        // Suppress Alice cloud processing or TTS response from a hijacked Spotify command
                                        send_cancel_alice(quasar_fd);
                                        set_alsa_yield(0, st_now);
                                        set_ducking(0, st_now);
                                    } else {
                                        // Quasar AliceState enum: 2=SPEAKING, 3=BUSY/THINKING
                                        if (alice_state == 2 && g_tts_hold_until_ms > 0) g_tts_speaking_seen = 1;
                                        set_ducking(1, st_now);
                                        set_alsa_yield(1, st_now);
                                    }
                                } else if (alice_state == 5) {
                                    if (st_now >= cancel_alice_until_ms) {
                                        set_ducking(1, st_now);
                                        set_alsa_yield(1, st_now);
                                    }
                                } else if (alice_state == 0 || alice_state == 6) {
                                    if (g_tts_speaking_seen || st_now >= g_tts_hold_until_ms) {
                                        g_tts_hold_until_ms = 0;
                                        g_tts_speaking_seen = 0;
                                        set_alsa_yield(0, st_now);
                                        set_ducking(0, st_now);
                                    }
                                }
                            }
                        }
                        const uint8_t *text_bytes = 0;
                        unsigned long text_len = 0;
                        if (voice_request_read(&voice_requests, frame_body, frame_len, &text_bytes, &text_len)) {
                            handle_voice_recognition(quasar_fd, (const char *)text_bytes, text_len);
                        }

                        // Parse Remoting messages: CommandSpotterCapability (quick commands without "Алиса")
                        // and PlaybackControlCapability (hand gestures / physical panel controls over IPC)
                        const uint8_t *remoting_body = 0;
                        unsigned long remoting_len = 0;
                        uint64_t rem_type = 0;
                        if (find_field(frame_body, frame_len, 210, 2, &remoting_body, &remoting_len, 0)) {
                            // Direct field 210
                        } else if (find_field(frame_body, frame_len, 253, 0, 0, 0, &rem_type) && rem_type == 210) {
                            find_field(frame_body, frame_len, 252, 2, &remoting_body, &remoting_len, 0);
                        }
                        if (remoting_body && remoting_len > 0) {
                            const uint8_t *cap_name = 0;
                            unsigned long cap_len = 0;
                            if (find_field(remoting_body, remoting_len, 7, 2, &cap_name, &cap_len, 0)) {
                                if (cap_len == 24 && mem_equal(cap_name, "CommandSpotterCapability", 24)) {
                                    static int logged_cmd_cap = 0;
                                    if (!logged_cmd_cap) {
                                        logged_cmd_cap = 1;
                                        log_msg("[bridge] Quasar capability active: CommandSpotterCapability\n");
                                    }
                                    const uint8_t *method_body = 0;
                                    unsigned long method_len = 0;
                                    if (find_field(remoting_body, remoting_len, 15, 2, &method_body, &method_len, 0)) {
                                        uint64_t m_id = 0;
                                        if (find_field(method_body, method_len, 1, 0, 0, 0, &m_id) && m_id == 2) {
                                            const uint8_t *phrase = 0;
                                            unsigned long phrase_len = 0;
                                            if (find_field(method_body, method_len, 3, 2, &phrase, &phrase_len, 0)) {
                                                log_msg("[bridge] Quick spotter phrase spotted: \"");
                                                sys_write(2, phrase, phrase_len);
                                                log_msg("\"\n");
                                                handle_voice_recognition(quasar_fd, (const char *)phrase, phrase_len);
                                            }
                                        }
                                    }
                                } else if (cap_len == 25 && mem_equal(cap_name, "PlaybackControlCapability", 25)) {
                                    static int logged_pb_cap = 0;
                                    if (!logged_pb_cap) {
                                        logged_pb_cap = 1;
                                        log_msg("[bridge] Quasar capability active: PlaybackControlCapability\n");
                                    }
                                    const uint8_t *method_body = 0;
                                    unsigned long method_len = 0;
                                    if (find_field(remoting_body, remoting_len, 13, 2, &method_body, &method_len, 0)) {
                                        uint64_t m_id = 0;
                                        if (find_field(method_body, method_len, 1, 0, 0, 0, &m_id)) {
                                            log_msg("[bridge] Gesture PlaybackControlCapability received\n");
                                            if (!yandex_music_active) {
                                                if (m_id == 0) { // PLAY
                                                    handle_voice_recognition(quasar_fd, "играй", 10);
                                                } else if (m_id == 1) { // PAUSE
                                                    handle_voice_recognition(quasar_fd, "пауза", 10);
                                                } else if (m_id == 2) { // TOGGLE_PLAY_PAUSE
                                                    if (playback_status == 1) {
                                                        handle_voice_recognition(quasar_fd, "пауза", 10);
                                                    } else {
                                                        handle_voice_recognition(quasar_fd, "играй", 10);
                                                    }
                                                } else if (m_id == 6) { // NEXT
                                                    handle_voice_recognition(quasar_fd, "дальше", 12);
                                                } else if (m_id == 7) { // PREV
                                                    handle_voice_recognition(quasar_fd, "назад", 10);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        unsigned long total_consumed = 4 + frame_len;
                        unsigned long remaining = quasar_buf_pos - total_consumed;
                        for (unsigned long i = 0; i < remaining; i++) {
                            quasar_buf[i] = quasar_buf[total_consumed + i];
                        }
                        quasar_buf_pos = remaining;
                    }
                }
            } else if (quasar_idx >= 0 && (pfd[quasar_idx].revents & (0x0008 | 0x0010 | 0x0020))) {
                log_msg("[bridge] Quasar socket error, reconnecting...\n");
                sys_close(quasar_fd);
                quasar_fd = -1;
                quasar_buf_pos = 0;
                g_alice_state = 0;
                g_led_alice_yield_until_ms = 0;
                g_tts_hold_until_ms = 0;
                set_alsa_yield(0, now);
                set_ducking(0, now);
            }

            // Process incoming IOEvents from Quasar IOHub (9910)
            if (quasar_io_idx >= 0) {
                handle_quasar_io_stream(&quasar_io_fd, pfd[quasar_io_idx].revents, now);
            }

            // Read Quasar Volume state stream (9921 stereo_pair)
            if (quasar_vol_idx >= 0 && (pfd[quasar_vol_idx].revents & 0x0001)) {
                long n = sys_read(quasar_vol_fd, quasar_vol_buf + quasar_vol_buf_pos, sizeof(quasar_vol_buf) - quasar_vol_buf_pos);
                if (n <= 0) {
                    sys_close(quasar_vol_fd);
                    quasar_vol_fd = -1;
                    quasar_vol_buf_pos = 0;
                } else {
                    quasar_vol_buf_pos += n;
                    while (quasar_vol_buf_pos >= 4) {
                        uint32_t fl = (uint32_t)quasar_vol_buf[0] |
                                      ((uint32_t)quasar_vol_buf[1] << 8) |
                                      ((uint32_t)quasar_vol_buf[2] << 16) |
                                      ((uint32_t)quasar_vol_buf[3] << 24);
                        if (fl > sizeof(quasar_vol_buf) - 4) {
                            quasar_vol_buf_pos = 0;
                            break;
                        }
                        if (quasar_vol_buf_pos < 4 + fl) break;
                        int qvol = 0;
                        uint64_t qsrc = 0;
                        if (parse_quasar_vol_frame(quasar_vol_buf + 4, fl, &qvol, &qsrc)) {
                            on_quasar_volume_state(qvol, qsrc, now);
                        }
                        unsigned long rem = quasar_vol_buf_pos - (4 + fl);
                        for (unsigned long i = 0; i < rem; i++) quasar_vol_buf[i] = quasar_vol_buf[4 + fl + i];
                        quasar_vol_buf_pos = rem;
                    }
                }
            } else if (quasar_vol_idx >= 0 && (pfd[quasar_vol_idx].revents & (0x0008 | 0x0010 | 0x0020))) {
                sys_close(quasar_vol_fd);
                quasar_vol_fd = -1;
                quasar_vol_buf_pos = 0;
            }

            // shortcut: аппаратный старт подтверждается следующим WS; пересмотреть, если задержка мешает обычному управлению.
            // Check Input devices (Multi-device monitoring across all Stations)
            if (input_start_idx >= 0) {
                for (int i = 0; i < num_input_fds; i++) {
                    int p_idx = input_start_idx + i;
                    if (pfd[p_idx].revents & 0x0001) {
                        struct {
                            uint32_t tv_sec_lo;
                            uint32_t tv_sec_hi;
                            uint32_t tv_usec_lo;
                            uint32_t tv_usec_hi;
                            uint16_t type;
                            uint16_t code;
                            int32_t value;
                        } ev;

                        while (sys_read(input_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                            int is_play_pause_key = (ev.code == 164   /* KEY_PLAYPAUSE */ ||
                                                     ev.code == 119   /* KEY_PAUSE */ ||
                                                     ev.code == 128   /* KEY_STOP */ ||
                                                     ev.code == 166   /* KEY_STOPCD */ ||
                                                     ev.code == 207   /* KEY_PLAY */);
                            int is_touch_voice_key = (ev.code == 0x246 /* KEY_VOICECOMMAND */ ||
                                                      ev.code == 0x100 /* BTN_0 */ ||
                                                      ev.code == 353   /* KEY_SELECT */);
                            int is_next_key = (ev.code == 163 /* KEY_NEXTSONG */ ||
                                               ev.code == 159 /* KEY_FORWARD */ ||
                                               ev.code == 208 /* KEY_FASTFORWARD */);
                            int is_prev_key = (ev.code == 165 /* KEY_PREVIOUSSONG */ ||
                                               ev.code == 158 /* KEY_BACK */ ||
                                               ev.code == 168 /* KEY_REWIND */);

                            if (ev.type == 1 /* EV_KEY */) {
                                if (is_next_key && ev.value == 1) {
                                    log_msg("[bridge] Hardware key NEXT pressed\n");
                                    trigger_alice_suppression(quasar_fd, 3000);
                                    led_fx_trigger(LED_FX_NEXT);
                                    run_soloist_cmd("next");
                                    continue;
                                }
                                if (is_prev_key && ev.value == 1) {
                                    log_msg("[bridge] Hardware key PREV pressed\n");
                                    trigger_alice_suppression(quasar_fd, 3000);
                                    led_fx_trigger(LED_FX_PREV);
                                    run_soloist_cmd("prev");
                                    continue;
                                }
                                if (is_play_pause_key || is_touch_voice_key) {
                                    if (ev.value == 1) { // Press
                                        touch_down_start = 1;
                                        touch_down_time = now_ms();
                                        touch_is_down = 1;
                                        // Suppress Alice immediately on press down
                                        trigger_alice_suppression(quasar_fd, 4000);
                                    } else if (ev.value == 0) { // Release
                                        if (touch_down_start) {
                                            int64_t dur = now_ms() - touch_down_time;
                                            if (is_play_pause_key || dur < 700) {
                                                int64_t now_tap = now_ms();
                                                if (now_tap - last_touch_release_time < 380) {
                                                    // Double tap -> NEXT TRACK!
                                                    log_msg("[bridge] Double-tap detected -> NEXT TRACK\n");
                                                    last_touch_release_time = 0;
                                                    trigger_alice_suppression(quasar_fd, 3000);
                                                    led_fx_trigger(LED_FX_NEXT);
                                                    run_soloist_cmd("next");
                                                } else {
                                                    last_touch_release_time = now_tap;
                                                    log_msg("[bridge] Button / Touch TAP detected -> TOGGLE PLAY/PAUSE\n");
                                                    trigger_alice_suppression(quasar_fd, 4000);
                                                    if (playback_status == 1) {
                                                        log_msg("[bridge] Instant action: PAUSE\n");
                                                        spotify_is_active = 0;
                                                        led_fx_clear();
                                                        run_soloist_cmd("pause");
                                                    } else if (playback_status == 2) {
                                                        log_msg("[bridge] Instant action: PLAY\n");
                                                        led_fx_trigger(LED_FX_PLAY);
                                                        run_soloist_cmd("play");
                                                    } else {
                                                        run_toggle();
                                                    }
                                                }
                                            } else {
                                                log_msg("[bridge] Button / Touch HOLD -> ALICE TRIGGER\n");
                                                cancel_alice_until_ms = 0;
                                            }
                                        }
                                        touch_down_start = 0;
                                        touch_is_down = 0;
                                    }
                                }
                            }
                        }
                    } else if (pfd[p_idx].revents & (0x0008 | 0x0010 | 0x0020)) {
                        log_msg("[bridge] Input device error on fd, reopening...\n");
                        for (int k = 0; k < num_input_fds; k++) sys_close(input_fds[k]);
                        num_input_fds = 0;
                        break;
                    }
                }
            }

            // Check Soloist WS socket
            if (soloist_ws_idx >= 0 && (pfd[soloist_ws_idx].revents & 0x0001)) {
                long n = sys_read(soloist_ws_fd, ws_buf + ws_buf_pos, sizeof(ws_buf) - 1 - ws_buf_pos);
                if (n <= 0) {
                    log_msg("[bridge] Soloist WS connection closed, reconnecting...\n");
                    sys_close(soloist_ws_fd);
                    soloist_ws_fd = -1;
                    soloist_ws_handshake = 0;
                    ws_initial_vol_synced = 0;
                    current_soloist_vol = -1;
                    ws_buf_pos = 0;
                } else {
                    ws_buf_pos += n;
                    ws_buf[ws_buf_pos] = 0;

                    if (!soloist_ws_handshake) {
                        int hdr_end = -1;
                        for (unsigned long i = 0; i + 3 < ws_buf_pos; i++) {
                            if (ws_buf[i] == '\r' && ws_buf[i+1] == '\n' && ws_buf[i+2] == '\r' && ws_buf[i+3] == '\n') {
                                hdr_end = (int)(i + 4);
                                break;
                            }
                        }
                        if (hdr_end > 0) {
                            soloist_ws_handshake = 1;
                            unsigned long rem = ws_buf_pos - hdr_end;
                            for (unsigned long i = 0; i < rem; i++) ws_buf[i] = ws_buf[hdr_end + i];
                            ws_buf_pos = rem;
                            ws_buf[ws_buf_pos] = 0;
                            log_msg("[bridge] Soloist WS Handshake complete\n");
                        }
                    }

                    if (soloist_ws_handshake) {
                        while (ws_buf_pos >= 2) {
                            uint8_t b0 = ws_buf[0];
                            uint8_t b1 = ws_buf[1];
                            int opcode = b0 & 0x0f;
                            unsigned long payload_len = b1 & 0x7f;
                            unsigned long hdr_len = 2;

                            if (payload_len == 126) {
                                if (ws_buf_pos < 4) break;
                                payload_len = ((unsigned long)ws_buf[2] << 8) | ws_buf[3];
                                hdr_len = 4;
                            } else if (payload_len == 127) {
                                if (ws_buf_pos < 10) break;
                                payload_len = ((unsigned long)ws_buf[6] << 24) | ((unsigned long)ws_buf[7] << 16) | ((unsigned long)ws_buf[8] << 8) | ws_buf[9];
                                hdr_len = 10;
                            }

                            if (hdr_len + payload_len > sizeof(ws_buf) - 16) {
                                log_msg("[bridge] Warning: oversize WS frame dropped\n");
                                ws_buf_pos = 0;
                                break;
                            }

                            if (ws_buf_pos < hdr_len + payload_len) break; // Incomplete frame

                            if (opcode == 1) { // Text JSON frame
                                uint8_t save = ws_buf[hdr_len + payload_len];
                                ws_buf[hdr_len + payload_len] = 0;
                                handle_soloist_ws_json((const char *)(ws_buf + hdr_len));
                                ws_buf[hdr_len + payload_len] = save;
                            } else if (opcode == 9) { // Ping frame
                                static const uint8_t pong_frame[] = { 0x8a, 0x80, 0x00, 0x00, 0x00, 0x00 };
                                sys_write(soloist_ws_fd, pong_frame, sizeof(pong_frame));
                            }

                            unsigned long total_consumed = hdr_len + payload_len;
                            unsigned long rem = ws_buf_pos - total_consumed;
                            for (unsigned long i = 0; i < rem; i++) ws_buf[i] = ws_buf[total_consumed + i];
                            ws_buf_pos = rem;
                            ws_buf[ws_buf_pos] = 0;
                        }
                    }
                }
            } else if (soloist_ws_idx >= 0 && (pfd[soloist_ws_idx].revents & (0x0008 | 0x0010 | 0x0020))) {
                log_msg("[bridge] Soloist WS error, reconnecting...\n");
                sys_close(soloist_ws_fd);
                soloist_ws_fd = -1;
                soloist_ws_handshake = 0;
                ws_initial_vol_synced = 0;
                current_soloist_vol = -1;
                ws_buf_pos = 0;
            }
        }

        // Bidirectional Speaker/Alice <-> Soloist Volume Synchronization
        int64_t now_sync = now_ms();
        if (pending_quasar_vol >= 0 && now_sync >= pending_quasar_vol_due_ms) {
            int target = pending_quasar_vol;
            pending_quasar_vol = -1;
            last_sent_to_quasar_vol = target;
            last_sent_to_quasar_ms = now_sync;
            int sent_quasar = 0;
            if (quasar_io_fd >= 0 && !quasar_vol_fallback_amixer) {
                if (send_quasar_volume(quasar_io_fd, target) < 0) {
                    sys_close(quasar_io_fd);
                    quasar_io_fd = -1;
                    quasar_io_buf_pos = 0;
                } else {
                    sent_quasar = 1;
                    if (!quasar_vol_known || abs_diff_int(target, quasar_vol) >= 10) {
                        vol_verify_target = target;
                        vol_verify_prev_qvol = quasar_vol_known ? quasar_vol : -1;
                        vol_verify_sent_ms = now_sync;
                    }
                }
            }
            if (!sent_quasar) {
                vol_verify_target = -1;
                vol_verify_sent_ms = 0;
                char vol_str[12];
                format_uint_ascii(vol_str, target);
                run_controller("volume", vol_str);
            }
        }
        int fallback_target = -1;
        if (check_quasar_vol_fallback(now_sync, &fallback_target)) {
            log_msg("[bridge] Quasar 9910 volume_set unconfirmed on 9921; falling back to ALSA amixer volume\n");
            char vol_str[12];
            format_uint_ascii(vol_str, fallback_target);
            run_controller("volume", vol_str);
        }
        if (need_soloist_vol_sync && quasar_vol_known && soloist_ws_fd >= 0 && soloist_ws_handshake) {
            need_soloist_vol_sync = 0;
            if (quasar_vol >= 0 && quasar_vol != current_soloist_vol) {
                last_sent_to_soloist_vol = quasar_vol;
                last_sent_to_soloist_ms = now_sync;
                current_soloist_vol = quasar_vol;
                if (send_soloist_ws_volume(soloist_ws_fd, quasar_vol) < 0) {
                    sys_close(soloist_ws_fd);
                    soloist_ws_fd = -1;
                    soloist_ws_handshake = 0;
                    ws_initial_vol_synced = 0;
                    current_soloist_vol = -1;
                }
            }
        }

        // Auto-Next / Auto-Repeat Watchdog (Issue #4 / #20): Detect track end stall caused by apulse missing underflow callback
        if (soloist_ws_fd >= 0 && soloist_ws_handshake) {
            if (spotify_device_is_active && (playback_status == 1 || playback_status == 3) && track_duration_ms > 2000) {
                uint64_t current_pos = base_position_ms;
                if (playback_speed == 1 && local_recorded_ms > 0) {
                    int64_t elapsed = now - local_recorded_ms;
                    if (elapsed > 0) current_pos += (uint64_t)elapsed;
                }

                // Clear repeat guard once track playback restarts from beginning (repeat track or 1-track context loop)
                if (current_pos < 5000 && track_duration_ms > 15000 &&
                    last_auto_next_uri[0] != 0 && (now - last_auto_next_time) >= 5000) {
                    last_auto_next_uri[0] = 0;
                }

                int is_at_end = 0;
                // Check condition 1: buffering within the last 3.5 seconds of the track (e.g. at -0:02 / -0:00)
                if (playback_status == 3 && current_pos + 3500 >= track_duration_ms) {
                    is_at_end = 1;
                }
                // Check condition 2: playback position counted past duration
                else if (current_pos >= track_duration_ms) {
                    is_at_end = 1;
                }

                int64_t stall_thresh_ms = (repeat_mode == 2) ? 2500 : 1500;
                if (is_at_end) {
                    if (stuck_since_ms == 0) {
                        stuck_since_ms = now;
                    } else if (now - stuck_since_ms >= stall_thresh_ms && now - last_auto_next_time >= 5000) {
                        if (repeat_mode == 2 && current_track_uri[0] != 0) {
                            log_msg("[bridge] Auto-Repeat: restarting stalled single-track repeat\n");
                            str_copy(last_auto_next_uri, current_track_uri, sizeof(last_auto_next_uri));
                            last_auto_next_time = now;
                            stuck_since_ms = 0;
                            run_controller("auto-repeat", current_track_uri);
                        } else if (!str_equal(last_auto_next_uri, current_track_uri) && current_track_uri[0] != 0) {
                            log_msg("[bridge] Auto-Next: track ended and audio output stalled, advancing to next track\n");
                            str_copy(last_auto_next_uri, current_track_uri, sizeof(last_auto_next_uri));
                            last_auto_next_time = now;
                            stuck_since_ms = 0;
                            run_controller("auto-next", current_track_uri);
                        }
                    }
                } else {
                    stuck_since_ms = 0;
                }
            } else {
                stuck_since_ms = 0;
            }
        }

        // 5. Periodic tmpfs audio cache trim (keeps cache <16MB on 238MB RAM devices)
        if (playback_status == 1 && (now_sync - last_cache_clean_ms >= 45000)) {
            last_cache_clean_ms = now_sync;
            run_controller("clean-cache", 0);
        }

        // 6. Error TTS + LED Visualizer / Equalizer update & request processing
        check_tts_request(quasar_fd);
        check_led_request();
        led_visualizer_tick(now_ms());
    }
}
