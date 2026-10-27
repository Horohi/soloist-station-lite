#include <stdint.h>
#include "voice-request.h"

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

/* Builds and sends one AliceCapability method frame via Quasar Native IPC (QuasarMessage field 210).
 * Length: 24 bytes body (0x18, 0x00, 0x00, 0x00)
 * Tag 210 (wire type 2): 0x92, 0x0d
 * Remoting length: 21 bytes (0x15)
 * Tag 7 ("AliceCapability"): 0x3a, 0x0f, 'AliceCapability'
 * Tag 8 ({ 0x08, method }): 0x42, 0x02, 0x08, method
 */
static int send_alice_method_f210(int quasar_fd, unsigned char method) {
    if (quasar_fd < 0) return -1;
    static const unsigned char frame_template[28] = {
        0x18, 0x00, 0x00, 0x00, // length = 24
        0x92, 0x0d,             // tag: field 210, wire type 2
        0x15,                   // remoting length = 21 bytes
        0x3a, 0x0f,             // tag: field 7, len 15
        'A','l','i','c','e','C','a','p','a','b','i','l','i','t','y',
        0x42, 0x02,             // tag: field 8, len 2
        0x08, 0x00              // method field 1 (0x08), method byte (filled at [27])
    };
    unsigned char frame[28];
    for (int i = 0; i < 28; i++) frame[i] = frame_template[i];
    frame[27] = method;

    long n = sys_write(quasar_fd, frame, sizeof(frame));
    return (n == (long)sizeof(frame)) ? 0 : -1;
}

/* Also sends SharedMessage wrapper (field 252 + field 253: 210) for newer/alternate Quasar daemons */
static int send_alice_method_f252(int quasar_fd, unsigned char method) {
    if (quasar_fd < 0) return -1;
    static const unsigned char frame_template[32] = {
        0x1c, 0x00, 0x00, 0x00, // length = 28
        0xe2, 0x0f,             // tag: field 252, wire type 2
        0x15,                   // remoting length = 21 bytes
        0x3a, 0x0f,             // tag: field 7, len 15
        'A','l','i','c','e','C','a','p','a','b','i','l','i','t','y',
        0x42, 0x02,             // tag: field 8, len 2
        0x08, 0x00,             // method field 1 (0x08), method byte (filled at [27])
        0xe8, 0x0f,             // tag: field 253, wire type 0
        0xd2, 0x01              // value = 210 (QuasarMessage::remoting)
    };
    unsigned char frame[32];
    for (int i = 0; i < 32; i++) frame[i] = frame_template[i];
    frame[27] = method;

    long n = sys_write(quasar_fd, frame, sizeof(frame));
    return (n == (long)sizeof(frame)) ? 0 : -1;
}

static int send_alice_method(int quasar_fd, unsigned char method) {
    int r1 = send_alice_method_f210(quasar_fd, method);
    int r2 = send_alice_method_f252(quasar_fd, method);
    return (r1 == 0 || r2 == 0) ? 0 : -1;
}

/* Sends PlaybackControlCapability PAUSE (method kind 1) to pause any native media player */
static void send_playback_pause(int quasar_fd) {
    if (quasar_fd < 0) return;
    // Native field 210: 38 bytes
    static const unsigned char p210[38] = {
        0x22, 0x00, 0x00, 0x00,
        0x92, 0x0d, 0x1f,
        0x3a, 0x19,
        'P','l','a','y','b','a','c','k','C','o','n','t','r','o','l','C','a','p','a','b','i','l','i','t','y',
        0x6a, 0x02, 0x08, 0x01
    };
    sys_write(quasar_fd, p210, sizeof(p210));

    // SharedMessage field 252/253: 42 bytes
    static const unsigned char p252[42] = {
        0x26, 0x00, 0x00, 0x00,
        0xe2, 0x0f, 0x1f,
        0x3a, 0x19,
        'P','l','a','y','b','a','c','k','C','o','n','t','r','o','l','C','a','p','a','b','i','l','i','t','y',
        0x6a, 0x02, 0x08, 0x01,
        0xe8, 0x0f, 0xd2, 0x01
    };
    sys_write(quasar_fd, p252, sizeof(p252));
}

static void send_cancel_alice(int quasar_fd) {
    if (quasar_fd < 0) return;
    int err = 0;
    // Native Quasar remoting directives:
    // kind 3: STOP_CONVERSATION
    // kind 6: CANCEL_DIALOG_AND_CLEAR_QUEUE
    // kind 0x0e: STOP_TTS
    // kind 1: CANCEL_DIALOG
    if (send_alice_method(quasar_fd, 3) < 0) err = 1;
    if (send_alice_method(quasar_fd, 6) < 0) err = 1;
    if (send_alice_method(quasar_fd, 0x0e) < 0) err = 1;
    if (send_alice_method(quasar_fd, 1) < 0) err = 1;

    send_playback_pause(quasar_fd);

    if (!err) {
        log_msg("[bridge] Sent cancel_alice (stop+queue+tts+dialog+pause) to Quasar\n");
    } else {
        log_msg("[bridge] Warning: Failed to send cancel_alice to Quasar socket\n");
    }
}

static int64_t cancel_alice_until_ms = 0;

static void trigger_alice_suppression(int quasar_fd, int duration_ms) {
    cancel_alice_until_ms = now_ms() + duration_ms;
    send_cancel_alice(quasar_fd);
}

static int is_yam_mode = 0;

static const char *get_controller_sh(void) {
    return is_yam_mode ? "./scripts/control.sh" : "/data/opt/soloist/scripts/control.sh";
}

static const char *get_censor_path(void) {
    return is_yam_mode ? "./data/censorship" : "/data/opt/soloist/data/censorship";
}

static int str_equal(const char *a, const char *b);
static uint64_t track_observation_revision;

static int selection_needs_observation(void) {
    const char *pending = is_yam_mode ? "./data/requests/selection.pending" : "/data/opt/soloist/data/requests/selection.pending";
    const char *selection = is_yam_mode ? "./data/requests/selection" : "/data/opt/soloist/data/requests/selection";
    const char *track = is_yam_mode ? "./data/requests/track" : "/data/opt/soloist/data/requests/track";
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
    const char *dir = is_yam_mode ? "./data/requests" : "/data/opt/soloist/data/requests";
    const char *tmp = is_yam_mode ? "./data/requests/track.tmp" : "/data/opt/soloist/data/requests/track.tmp";
    const char *path = is_yam_mode ? "./data/requests/track" : "/data/opt/soloist/data/requests/track";
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
        const char *envp[] = {
            "PATH=/bin:/usr/bin:/sbin:/usr/sbin",
            "SOLOIST_BRIDGE_ADMISSION=1",
            "SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt", 0
        };
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
        const char *path = is_yam_mode ? "./data/requests/generation" : "/data/opt/soloist/data/requests/generation";
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

/* Text and command classification helpers */

static void to_lower_ru(char *dst, const char *src, unsigned long max) {
    unsigned long si = 0, di = 0;
    while (src[si] && di + 4 < max) {
        uint8_t c1 = (uint8_t)src[si];
        if (c1 < 0x80) {
            if (c1 >= 'A' && c1 <= 'Z') dst[di++] = c1 + 32;
            else dst[di++] = c1;
            si++;
        } else if (c1 == 0xd0) {
            uint8_t c2 = (uint8_t)src[si + 1];
            if (c2 == 0x81) { // Ё -> ё
                dst[di++] = 0xd1;
                dst[di++] = 0x91;
            } else if (c2 >= 0x90 && c2 <= 0x9f) { // А-П -> а-п
                dst[di++] = 0xd0;
                dst[di++] = c2 + 0x20;
            } else if (c2 >= 0xa0 && c2 <= 0xaf) { // Р-Я -> р-я
                dst[di++] = 0xd1;
                dst[di++] = c2 - 0x20;
            } else {
                dst[di++] = c1;
                dst[di++] = c2;
            }
            si += 2;
        } else if (c1 == 0xd1) {
            dst[di++] = c1;
            dst[di++] = src[si + 1];
            si += 2;
        } else {
            dst[di++] = c1;
            si++;
        }
    }
    dst[di] = 0;
}

static int str_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    unsigned long hlen = str_len(haystack);
    unsigned long nlen = str_len(needle);
    if (nlen == 0) return 1;
    if (hlen < nlen) return 0;
    for (unsigned long i = 0; i <= hlen - nlen; i++) {
        unsigned long j = 0;
        while (j < nlen && haystack[i + j] == needle[j]) j++;
        if (j == nlen) return 1;
    }
    return 0;
}

static int str_equal(const char *a, const char *b) {
    unsigned long i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int mem_equal(const void *a, const void *b, unsigned long n) {
    const uint8_t *p1 = (const uint8_t *)a;
    const uint8_t *p2 = (const uint8_t *)b;
    for (unsigned long i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return 0;
    }
    return 1;
}

static void str_copy(char *dst, const char *src, unsigned long max_dst) {
    if (!dst || max_dst == 0) return;
    unsigned long i = 0;
    while (src && src[i] && i + 1 < max_dst) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int json_find_str(const char *json, const char *key, char *out, unsigned long max_out) {
    if (!json || !key || !out || max_out == 0) return 0;
    unsigned long klen = str_len(key);
    for (unsigned long i = 0; json[i]; i++) {
        if (json[i] == '"' && mem_equal(json + i + 1, key, klen) && json[i + 1 + klen] == '"') {
            unsigned long idx = i + 1 + klen + 1;
            while (json[idx] == ' ' || json[idx] == ':') idx++;
            if (json[idx] == '"') {
                idx++;
                unsigned long o = 0;
                while (json[idx] && json[idx] != '"' && o + 1 < max_out) {
                    out[o++] = json[idx++];
                }
                out[o] = 0;
                return 1;
            }
        }
    }
    return 0;
}

static int json_find_uint64(const char *json, const char *key, uint64_t *val) {
    if (!json || !key || !val) return 0;
    unsigned long klen = str_len(key);
    for (unsigned long i = 0; json[i]; i++) {
        if (json[i] == '"' && mem_equal(json + i + 1, key, klen) && json[i + 1 + klen] == '"') {
            unsigned long idx = i + 1 + klen + 1;
            while (json[idx] == ' ' || json[idx] == ':') idx++;
            if (json[idx] >= '0' && json[idx] <= '9') {
                uint64_t v = 0;
                while (json[idx] >= '0' && json[idx] <= '9') {
                    v = v * 10 + (uint64_t)(json[idx] - '0');
                    idx++;
                }
                *val = v;
                return 1;
            }
        }
    }
    return 0;
}

static int json_find_int(const char *json, const char *key, int *val) {
    if (!json || !key || !val) return 0;
    unsigned long klen = str_len(key);
    for (unsigned long i = 0; json[i]; i++) {
        if (json[i] == '"' && mem_equal(json + i + 1, key, klen) && json[i + 1 + klen] == '"') {
            unsigned long idx = i + 1 + klen + 1;
            while (json[idx] == ' ' || json[idx] == ':') idx++;
            int sign = 1;
            if (json[idx] == '-') {
                sign = -1;
                idx++;
            }
            if (json[idx] >= '0' && json[idx] <= '9') {
                int v = 0;
                while (json[idx] >= '0' && json[idx] <= '9') {
                    v = v * 10 + (int)(json[idx] - '0');
                    idx++;
                }
                *val = sign * v;
                return 1;
            }
        }
    }
    return 0;
}

/* Word-boundary helpers. Cyrillic bytes (>= 0x80) count as word characters, so
 * "тв" no longer matches inside "молитва" and "бра" no longer matches "брат". */
static int is_word_byte(uint8_t c) {
    return c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/* needle occurs at the start of a word (prefix match: "свет" matches "свет", "света", "светильник") */
static int str_has_word_prefix(const char *haystack, const char *needle) {
    unsigned long hlen = str_len(haystack);
    unsigned long nlen = str_len(needle);
    if (nlen == 0 || hlen < nlen) return 0;
    for (unsigned long i = 0; i + nlen <= hlen; i++) {
        if (i > 0 && is_word_byte((uint8_t)haystack[i - 1])) continue;
        if (mem_equal(haystack + i, needle, nlen)) return 1;
    }
    return 0;
}

/* needle occurs as a whole word */
static int str_has_word(const char *haystack, const char *needle) {
    unsigned long hlen = str_len(haystack);
    unsigned long nlen = str_len(needle);
    if (nlen == 0 || hlen < nlen) return 0;
    for (unsigned long i = 0; i + nlen <= hlen; i++) {
        if (i > 0 && is_word_byte((uint8_t)haystack[i - 1])) continue;
        if (i + nlen < hlen && is_word_byte((uint8_t)haystack[i + nlen])) continue;
        if (mem_equal(haystack + i, needle, nlen)) return 1;
    }
    return 0;
}

/* "включи песню Назад в СССР" / "поставь группу Стоп" / "включи Кино" etc.: a play verb
 * means a catalog search, even if the title contains control words such as "назад",
 * "дальше", "стоп" or "пауза". */
static int is_explicit_search(const char *lower) {
    if (str_has_word_prefix(lower, "выключи") || str_has_word_prefix(lower, "отключи")) {
        return 0;
    }
    int has_verb = str_has_word_prefix(lower, "включи") || str_has_word_prefix(lower, "поставь") ||
                   str_has_word_prefix(lower, "сыграй") || str_has_word_prefix(lower, "запусти") ||
                   str_has_word_prefix(lower, "найди") || str_has_word_prefix(lower, "вруби") ||
                   str_has_word_prefix(lower, "слушать");
    if (!has_verb) return 0;

    // Check if it is a simple resume command rather than a specific track/artist query
    if (str_equal(lower, "включи музыку") || str_equal(lower, "вруби музыку") ||
        str_equal(lower, "включить музыку") || str_equal(lower, "алиса включи музыку") ||
        str_equal(lower, "алиса вруби музыку") || str_equal(lower, "слушать музыку")) {
        return 0;
    }
    return 1;
}

static int spotify_is_active = 0;
static char current_track_uri[128] = {0};
static char last_auto_next_uri[128] = {0};
static int playback_status = 0; // 0=stopped, 1=playing, 2=paused, 3=buffering
static uint64_t track_duration_ms = 0;
static uint64_t base_position_ms = 0;
static int64_t local_recorded_ms = 0;
static int playback_speed = 0;
static int64_t stuck_since_ms = 0;
static int64_t last_auto_next_time = 0;

static void handle_soloist_ws_json(const char *json) {
    char status_str[32];
    if (json_find_str(json, "status", status_str, sizeof(status_str))) {
        if (str_equal(status_str, "playing")) {
            playback_status = 1;
            playback_speed = 1;
            spotify_is_active = 1;
        } else if (str_equal(status_str, "paused")) {
            playback_status = 2;
            playback_speed = 0;
        } else if (str_equal(status_str, "buffering")) {
            playback_status = 3;
            playback_speed = 0;
        } else if (str_equal(status_str, "stopped")) {
            playback_status = 0;
            playback_speed = 0;
            spotify_is_active = 0;
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
    if (json_find_str(json, "uri", uri_buf, sizeof(uri_buf))) {
        if (uri_buf[0] == 's' && uri_buf[1] == 'p' && uri_buf[2] == 'o' && uri_buf[3] == 't') {
            if (!str_equal(current_track_uri, uri_buf)) {
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

    // An explicit catalog search ("включи песню ...") must not be hijacked by control words in the title
    if (!is_explicit_search(lower_buf)) {
        // Check PAUSE / STOP keywords (Instant Action)
        if (str_contains(lower_buf, "пауз") ||
            str_contains(lower_buf, "стоп") ||
            str_contains(lower_buf, "хватит") ||
            str_contains(lower_buf, "замолчи") ||
            str_contains(lower_buf, "останови") ||
            str_contains(lower_buf, "выключи музыку") ||
            str_contains(lower_buf, "отключи музыку")) {
            log_msg("[bridge] Action: PAUSE\n");
            trigger_alice_suppression(quasar_fd, 3000);
            spotify_is_active = 0;
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

        // Resume checks (Fast Action)
        if (str_has_word_prefix(lower_buf, "продолж") ||
            str_has_word(lower_buf, "играй") ||
            str_has_word_prefix(lower_buf, "возобнов") ||
            str_has_word(lower_buf, "заиграй") ||
            str_contains(lower_buf, "вруби музыку") ||
            (str_contains(lower_buf, "включи") && str_contains(lower_buf, "музык"))) {
            log_msg("[bridge] Action: RESUME\n");
            trigger_alice_suppression(quasar_fd, 2500);
            spotify_is_active = 1;
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
            register long x8 __asm__("x8") = 35; // unlinkat
            register long x0 __asm__("x0") = -100; // AT_FDCWD
            register long x1 __asm__("x1") = (long)get_censor_path();
            register long x2 __asm__("x2") = 0;
            __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
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

        // Ignore household smart home commands (word-aware: short tokens must be whole words)
        if (str_has_word_prefix(lower_buf, "свет") ||
            str_has_word_prefix(lower_buf, "ламп") ||
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
            str_has_word_prefix(lower_buf, "bluetooth")) {
            log_msg("[bridge] Household smart command, ignoring\n");
            return;
        }

        trigger_alice_suppression(quasar_fd, 5000);
        spotify_is_active = 1;

        log_msg("[bridge] Action: SEARCH & PLAY query='");
        log_msg(lower_buf);
        log_msg("'\n");
        run_search_and_play(lower_buf);
        return;
    }
}

static uint8_t quasar_buf[65536];
static unsigned long quasar_buf_pos = 0;
static uint8_t ws_buf[8192];
static unsigned long ws_buf_pos = 0;
static struct voice_request_state voice_requests;

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

void _start(void) {
    int test_fd = sys_open("./bin/soloist", 0 /* O_RDONLY */, 0);
    if (test_fd >= 0) {
        sys_close(test_fd);
        is_yam_mode = 1;
        log_msg("[bridge] soloist-bridge v14 started (YAM plugin mode: relative paths)\n");
    } else {
        log_msg("[bridge] soloist-bridge v14 started (Standalone mode: /data/opt/soloist)\n");
    }

    int quasar_fd = -1;
    int soloist_ws_fd = -1;
    int soloist_ws_handshake = 0;
    int64_t last_quasar_retry = 0;
    int64_t last_input_retry = 0;
    int64_t last_soloist_ws_retry = 0;

    int touch_down_start = 0;
    int64_t touch_down_time = 0;
    int touch_is_down = 0;

    while (1) {
        service_controllers();

        int64_t now = now_ms();

        // 1. Ensure Quasar connection
        if (quasar_fd < 0 && now - last_quasar_retry >= 2000) {
            last_quasar_retry = now;
            int fd = sys_socket(2, 1, 0);
            if (fd >= 0) {
                struct sockaddr_in addr;
                addr.sin_family = 2;
                addr.sin_port = 0x9326; // 9875 in big-endian
                addr.sin_addr = 0x0100007f; // 127.0.0.1
                for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;

                if (sys_connect(fd, &addr, sizeof(addr)) == 0) {
                    quasar_fd = fd;
                    quasar_buf_pos = 0;
                    log_msg("[bridge] Connected to Quasar on 127.0.0.1:9875\n");
                } else {
                    sys_close(fd);
                }
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
            int fd = sys_socket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
            if (fd >= 0) {
                struct sockaddr_in addr;
                addr.sin_family = 2;
                addr.sin_port = 0x8223; // 9090 in big-endian
                addr.sin_addr = 0x0100007f; // 127.0.0.1
                for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;

                if (sys_connect(fd, &addr, sizeof(addr)) == 0) {
                    soloist_ws_fd = fd;
                    soloist_ws_handshake = 0;
                    ws_buf_pos = 0;
                    const char *req = "GET / HTTP/1.1\r\nHost: 127.0.0.1:9090\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
                    sys_write(soloist_ws_fd, req, str_len(req));
                    log_msg("[bridge] Connected to Soloist WS on 127.0.0.1:9090\n");
                } else {
                    sys_close(fd);
                }
            }
        }

        // 4. Poll on available fds
        struct pollfd pfd[2 + MAX_INPUT_DEVS];
        int nfds = 0;
        int quasar_idx = -1;
        int soloist_ws_idx = -1;
        int input_start_idx = -1;

        if (quasar_fd >= 0) {
            quasar_idx = nfds;
            pfd[nfds].fd = quasar_fd;
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
        tmo.tv_nsec = 50000000; // 50ms

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

                        const uint8_t *frame_body = quasar_buf + 4;
                        const uint8_t *state_payload = 0;
                        unsigned long state_len = 0;
                        if (voice_state_payload(frame_body, frame_len, &state_payload, &state_len)) {
                            uint64_t alice_state = 0;
                            if (find_field(state_payload, state_len, 1, 0, 0, 0, &alice_state) &&
                                alice_state != 0 && now < cancel_alice_until_ms) {
                                send_cancel_alice(quasar_fd);
                            }
                        }
                        const uint8_t *text_bytes = 0;
                        unsigned long text_len = 0;
                        if (voice_request_read(&voice_requests, frame_body, frame_len, &text_bytes, &text_len)) {
                            handle_voice_recognition(quasar_fd, (const char *)text_bytes, text_len);
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
            }

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

                            if (ev.type == 1 /* EV_KEY */ && (is_play_pause_key || is_touch_voice_key)) {
                                if (ev.value == 1) { // Press
                                    touch_down_start = 1;
                                    touch_down_time = now_ms();
                                    touch_is_down = 1;
                                    // Suppress Alice immediately on press down
                                    trigger_alice_suppression(quasar_fd, 4000);
                                } else if (ev.value == 0) { // Release
                                    if (touch_down_start) {
                                        int64_t dur = now_ms() - touch_down_time;
                                        // Dedicated keys always trigger; touch/voice keys trigger on short tap (< 700ms)
                                        if (is_play_pause_key || dur < 700) {
                                            log_msg("[bridge] Button / Touch TAP detected -> TOGGLE PLAY/PAUSE\n");
                                            trigger_alice_suppression(quasar_fd, 4000);
                                            if (playback_status == 1) {
                                                log_msg("[bridge] Instant action: PAUSE\n");
                                                spotify_is_active = 0;
                                                run_soloist_cmd("pause");
                                            } else if (playback_status == 2) {
                                                log_msg("[bridge] Instant action: PLAY\n");
                                                spotify_is_active = 1;
                                                run_soloist_cmd("play");
                                            } else {
                                                run_toggle();
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
                ws_buf_pos = 0;
            }
        }

        // Auto-Next Watchdog (Issue #4): Detect track end stall caused by apulse missing underflow callback
        if (soloist_ws_fd >= 0 && soloist_ws_handshake) {
            if ((playback_status == 1 || playback_status == 3) && track_duration_ms > 2000) {
                uint64_t current_pos = base_position_ms;
                if (playback_speed == 1 && local_recorded_ms > 0) {
                    int64_t elapsed = now - local_recorded_ms;
                    if (elapsed > 0) current_pos += (uint64_t)elapsed;
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

                if (is_at_end) {
                    if (stuck_since_ms == 0) {
                        stuck_since_ms = now;
                    } else if (now - stuck_since_ms >= 1500 && now - last_auto_next_time >= 5000) {
                        if (!str_equal(last_auto_next_uri, current_track_uri) && current_track_uri[0] != 0) {
                            log_msg("[bridge] Auto-Next: track ended and audio output stalled, advancing to next track\n");
                            str_copy(last_auto_next_uri, current_track_uri, sizeof(last_auto_next_uri));
                            last_auto_next_time = now;
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
    }
}
