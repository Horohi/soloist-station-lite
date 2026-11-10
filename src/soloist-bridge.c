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

static struct soloist_duck_shm *g_duck_shm = 0;
static int g_ducking_active = 0;
static int g_alsa_yield_active = 0;
static int64_t g_alsa_yield_since_ms = 0;
static int64_t g_tts_hold_until_ms = 0;
static uint64_t g_alice_state = 0;
static int64_t g_led_alice_yield_until_ms = 0;

static void duck_shm_init(void) {
    sys_unlink(SOLOIST_YIELD_DEFAULT_PATH);
    if (g_duck_shm) return;
    int fd = sys_open(SOLOIST_DUCK_DEFAULT_PATH, 0100 | 02 /* O_CREAT | O_RDWR */, 0644);
    if (fd < 0) return;
    sys_ftruncate(fd, SOLOIST_DUCK_SHM_SIZE);
    void *p = sys_mmap(0, SOLOIST_DUCK_SHM_SIZE, 3 /* PROT_READ|PROT_WRITE */, 1 /* MAP_SHARED */, fd, 0);
    sys_close(fd);
    if ((long)p < 0 && (long)p > -4096) return;
    g_duck_shm = (struct soloist_duck_shm *)p;
    g_duck_shm->magic = SOLOIST_DUCK_MAGIC;
    __atomic_store_n(&g_duck_shm->until_ms, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_duck_shm->active, 0, __ATOMIC_RELEASE);
}

static void set_ducking(int active, int64_t now) {
    g_ducking_active = active ? 1 : 0;
    if (!g_duck_shm) return;
    if (active) {
        __atomic_store_n(&g_duck_shm->until_ms, now + 60000, __ATOMIC_RELEASE);
        __atomic_store_n(&g_duck_shm->active, 1, __ATOMIC_RELEASE);
    } else {
        __atomic_store_n(&g_duck_shm->active, 0, __ATOMIC_RELEASE);
    }
}

static inline int is_alice_visual_state(uint64_t st) {
    return (st == 1 || st == 2 || st == 3 || st == 5);
}

/* Yield /sys/class/leds whenever maind renders Alice listening/thinking/speaking LEDs */
static inline int alice_owns_leds(int64_t now) {
    return is_alice_visual_state(g_alice_state) ||
           g_ducking_active ||
           now < g_tts_hold_until_ms ||
           now < g_led_alice_yield_until_ms;
}

static void set_alsa_yield(int enable, int64_t now) {
    if (enable) {
        if (!g_alsa_yield_active) {
            int fd = sys_open(SOLOIST_YIELD_DEFAULT_PATH, 0100 | 01 /* O_CREAT | O_WRONLY */, 0644);
            if (fd >= 0) sys_close(fd);
            g_alsa_yield_active = 1;
        }
        if (g_alsa_yield_since_ms == 0) g_alsa_yield_since_ms = now;
    } else if (g_alsa_yield_active || g_alsa_yield_since_ms != 0) {
        sys_unlink(SOLOIST_YIELD_DEFAULT_PATH);
        g_alsa_yield_active = 0;
        g_alsa_yield_since_ms = 0;
    }
}

static int64_t cancel_alice_until_ms = 0;

/* Sends Alice START_VOCALIZE directive so Alice speaks natively on the speaker */
static int send_alice_tts(int quasar_fd, const char *text) {
    if (quasar_fd < 0 || !text || !*text) return -1;

    // Cancel any in-flight cloud dialog so Alice won't also answer from Yandex Music,
    // clear suppression window so our START_VOCALIZE speech isn't aborted, duck PCM,
    // and yield /dev/snd/pcmC0D0p so 32-bit Alice can open dmix cleanly while playing.
    send_cancel_alice(quasar_fd);
    cancel_alice_until_ms = 0;
    int64_t tnow = now_ms();
    g_tts_hold_until_ms = tnow + 1500;
    set_ducking(1, tnow);
    set_alsa_yield(1, tnow);
    sys_msleep(80);

    unsigned long text_len = str_len(text);
    if (text_len > 350) text_len = 350;

    // Build TVocalizeDirective (schema fields: 1=Text, 2=SynthesisMode(2=Interrupt)):
    uint8_t voc_dir[400];
    unsigned long v_pos = 0;
    voc_dir[v_pos++] = 0x0a; // tag 1 wire 2
    if (text_len >= 128) {
        voc_dir[v_pos++] = (uint8_t)((text_len & 0x7f) | 0x80);
        voc_dir[v_pos++] = (uint8_t)(text_len >> 7);
    } else {
        voc_dir[v_pos++] = (uint8_t)text_len;
    }
    for (unsigned long i = 0; i < text_len; i++) voc_dir[v_pos++] = (uint8_t)text[i];
    voc_dir[v_pos++] = 0x10; // tag 2 wire 0
    voc_dir[v_pos++] = 0x02; // value 2 (Interrupt)

    // AliceCapabilityMethod:
    // field 1: method = 18 (tag 0x08, val 0x12)
    // field 13: vocalize_directive (tag (13<<3)|2 = 106 = 0x6a)
    uint8_t meth_msg[450];
    unsigned long m_pos = 0;
    meth_msg[m_pos++] = 0x08;
    meth_msg[m_pos++] = 0x12; // 18: START_VOCALIZE
    meth_msg[m_pos++] = 0x6a; // field 13 wire 2
    if (v_pos >= 128) {
        meth_msg[m_pos++] = (uint8_t)((v_pos & 0x7f) | 0x80);
        meth_msg[m_pos++] = (uint8_t)(v_pos >> 7);
    } else {
        meth_msg[m_pos++] = (uint8_t)v_pos;
    }
    for (unsigned long i = 0; i < v_pos; i++) meth_msg[m_pos++] = voc_dir[i];

    // Remoting:
    // field 7: "AliceCapability" (tag 0x3a, len 15)
    // field 8: meth_msg (tag 0x42)
    uint8_t remoting[500];
    unsigned long r_pos = 0;
    remoting[r_pos++] = 0x3a;
    remoting[r_pos++] = 15;
    const char *cap = "AliceCapability";
    for (int i = 0; i < 15; i++) remoting[r_pos++] = (uint8_t)cap[i];
    remoting[r_pos++] = 0x42;
    if (m_pos >= 128) {
        remoting[r_pos++] = (uint8_t)((m_pos & 0x7f) | 0x80);
        remoting[r_pos++] = (uint8_t)(m_pos >> 7);
    } else {
        remoting[r_pos++] = (uint8_t)m_pos;
    }
    for (unsigned long i = 0; i < m_pos; i++) remoting[r_pos++] = meth_msg[i];

    // Build frame for field 210:
    // tag 210 (wire 2): 0x92, 0x0d
    uint8_t f210[600];
    unsigned long p210_pos = 4;
    f210[p210_pos++] = 0x92;
    f210[p210_pos++] = 0x0d;
    if (r_pos >= 128) {
        f210[p210_pos++] = (uint8_t)((r_pos & 0x7f) | 0x80);
        f210[p210_pos++] = (uint8_t)(r_pos >> 7);
    } else {
        f210[p210_pos++] = (uint8_t)r_pos;
    }
    for (unsigned long i = 0; i < r_pos; i++) f210[p210_pos++] = remoting[i];
    uint32_t body_len = (uint32_t)(p210_pos - 4);
    f210[0] = (uint8_t)(body_len & 0xff);
    f210[1] = (uint8_t)((body_len >> 8) & 0xff);
    f210[2] = (uint8_t)((body_len >> 16) & 0xff);
    f210[3] = (uint8_t)((body_len >> 24) & 0xff);

    long n = sys_write(quasar_fd, f210, p210_pos);
    log_msg("[bridge] Sent Alice TTS START_VOCALIZE: ");
    log_msg(text);
    log_msg("\n");
    return (n == (long)p210_pos) ? 0 : -1;
}

static void trigger_alice_suppression(int quasar_fd, int duration_ms) {
    int64_t tnow = now_ms();
    cancel_alice_until_ms = tnow + duration_ms;
    g_tts_hold_until_ms = 0;
    set_ducking(0, tnow);
    set_alsa_yield(0, tnow);
    if (is_alice_visual_state(g_alice_state) && g_led_alice_yield_until_ms < tnow + 450) {
        g_led_alice_yield_until_ms = tnow + 450;
    }
    send_cancel_alice(quasar_fd);
}

/* Format unsigned int into decimal ASCII; returns character count written (excluding NUL). */
static unsigned long format_uint_ascii(char *dst, int val) {
    if (val <= 0) {
        dst[0] = '0';
        dst[1] = 0;
        return 1;
    }
    char digits[12];
    int dcnt = 0;
    int temp = val;
    while (temp > 0 && dcnt < 12) {
        digits[dcnt++] = (char)('0' + (temp % 10));
        temp /= 10;
    }
    unsigned long len = 0;
    while (dcnt > 0) dst[len++] = digits[--dcnt];
    dst[len] = 0;
    return len;
}

/* Pure bitwise IEEE-754 float <-> int (0..100) conversions without libm/libgcc. */
static uint32_t int_to_f32_bits(int v) {
    if (v <= 0) return 0;
    if (v > 100) v = 100;
    uint32_t u = (uint32_t)v;
    int msb = 0;
    uint32_t tmp = u;
    while (tmp >>= 1) msb++;
    uint32_t mant = (u << (23 - msb)) & 0x7fffffu;
    uint32_t exp = (uint32_t)(127 + msb);
    return (exp << 23) | mant;
}

static int f32_bits_to_int(uint32_t bits) {
    if (bits & 0x80000000u) return 0;
    int exp = (int)((bits >> 23) & 0xffu) - 127;
    if (exp < -1) return 0;
    if (exp == -1) return 1;
    if (exp > 7) return 100;
    uint32_t sig = (1u << 23) | (bits & 0x7fffffu);
    int shift = 23 - exp;
    int val = (int)((sig + (1u << (shift - 1))) >> shift);
    if (val > 100) val = 100;
    return val;
}

static int find_fixed32(const uint8_t *data, unsigned long len, uint32_t field_num, uint32_t *val_out) {
    unsigned long offset = 0;
    while (offset < len) {
        uint64_t tag = parse_varint(data, len, &offset);
        uint32_t fnum = (uint32_t)(tag >> 3);
        int wire = (int)(tag & 7);
        if (fnum == 0) return 0;
        if (wire == 0) {
            parse_varint(data, len, &offset);
        } else if (wire == 2) {
            uint64_t flen = parse_varint(data, len, &offset);
            if (flen > len - offset) return 0;
            offset += flen;
        } else if (wire == 1) {
            if (8 > len - offset) return 0;
            offset += 8;
        } else if (wire == 5) {
            if (4 > len - offset) return 0;
            if (fnum == field_num) {
                if (val_out) {
                    *val_out = (uint32_t)data[offset] |
                               ((uint32_t)data[offset + 1] << 8) |
                               ((uint32_t)data[offset + 2] << 16) |
                               ((uint32_t)data[offset + 3] << 24);
                }
                return 1;
            }
            offset += 4;
        } else {
            return 0;
        }
    }
    return 0;
}

/* Parse Quasar StereoPair (field 198 -> 1 -> 11 VolumeManager.State) on port 9921. */
static int parse_quasar_vol_frame(const uint8_t *frame, unsigned long frame_len, int *vol_out, uint64_t *src_out) {
    const uint8_t *sp_body = 0;
    unsigned long sp_len = 0;
    uint64_t msg_type = 0;
    if (!find_field(frame, frame_len, 198, 2, &sp_body, &sp_len, 0)) {
        if (!find_field(frame, frame_len, 253, 0, 0, 0, &msg_type) || msg_type != 198 ||
            !find_field(frame, frame_len, 252, 2, &sp_body, &sp_len, 0)) {
            return 0;
        }
    }
    const uint8_t *state_body = 0;
    unsigned long state_len = 0;
    if (!find_field(sp_body, sp_len, 1, 2, &state_body, &state_len, 0)) return 0;

    const uint8_t *vol_body = 0;
    unsigned long vol_len = 0;
    if (!find_field(state_body, state_len, 11, 2, &vol_body, &vol_len, 0)) return 0;

    uint32_t pct_bits = 0;
    int has_pct = find_fixed32(vol_body, vol_len, 6, &pct_bits);
    uint64_t is_muted = 0;
    int has_mute = find_field(vol_body, vol_len, 3, 0, 0, 0, &is_muted);
    if (!has_pct && !has_mute) return 0;

    uint64_t src = 0;
    find_field(vol_body, vol_len, 7, 0, 0, 0, &src);

    int v = (has_mute && is_muted) ? 0 : (has_pct ? f32_bits_to_int(pct_bits) : 0);
    if (vol_out) *vol_out = v;
    if (src_out) *src_out = src;
    return 1;
}

/* Send silent volume_set directive (ESourceOther=1000, IsLedSilent=1) via Quasar iohub_clients (port 9910). */
static int send_quasar_volume(int io_fd, int vol_pct) {
    if (io_fd < 0) return -1;
    if (vol_pct < 0) vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;

    uint32_t fbits = int_to_f32_bits(vol_pct);

    // PayloadRaw = TVolumeSetDirective { 1: float PercentLevel, 2: Mode=0, 3: Source=1000 }
    // Wrapped inside TDirective -> DirectiveList -> ExternalDirectives -> IOControlCommand(95) -> QuasarMessage(88)
    uint8_t frame[57] = {
        53, 0x00, 0x00, 0x00, // frame length = 53 bytes
        0xc2, 0x05, 50,       // QuasarMessage field 88 (io_control), len 50
        0xfa, 0x05, 47,       // IOControlCommand field 95 (external_directives), len 47
        0x0a, 45,             // ExternalDirectives field 1 (directive_list), len 45
        0x0a, 43,             // DirectiveList field 1 (directives), len 43
        0x0a, 13, 'c','l','i','e','n','t','_','a','c','t','i','o','n', // TDirective.Type
        0x12, 10, 'v','o','l','u','m','e','_','s','e','t',             // TDirective.Name
        0x20, 0x01,           // TDirective.IgnoreAnswer = 1
        0x40, 0x01,           // TDirective.IsLedSilent = 1
        0x7a, 10,             // TDirective.PayloadRaw, len 10
        0x0d,                 // TVolumeSetDirective field 1 (PercentLevel, wire 5)
        (uint8_t)(fbits & 0xff),
        (uint8_t)((fbits >> 8) & 0xff),
        (uint8_t)((fbits >> 16) & 0xff),
        (uint8_t)((fbits >> 24) & 0xff),
        0x10, 0x00,           // TVolumeSetDirective field 2 (Mode = 0 Default)
        0x18, 0xe8, 0x07      // TVolumeSetDirective field 3 (Source = 1000 ESourceOther)
    };

    long n = sys_write(io_fd, frame, sizeof(frame));
    return (n == (long)sizeof(frame)) ? 0 : -1;
}

/* Send masked WebSocket set_volume JSON frame to Soloist WS (port 9090). */
static int send_soloist_ws_volume(int ws_fd, int vol_pct) {
    if (ws_fd < 0) return -1;
    if (vol_pct < 0) vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;

    char json[64];
    const char *hdr = "{\"type\":\"command\",\"command\":\"set_volume\",\"volume\":";
    unsigned long pos = 0;
    while (hdr[pos]) { json[pos] = hdr[pos]; pos++; }
    pos += format_uint_ascii(json + pos, vol_pct);
    json[pos++] = '}';

    uint8_t frame[72];
    frame[0] = 0x81; // FIN + text frame
    frame[1] = (uint8_t)(0x80 | pos); // MASK + payload length (<126)
    static const uint8_t mask[4] = { 0x12, 0x34, 0x56, 0x78 };
    for (int i = 0; i < 4; i++) frame[2 + i] = mask[i];
    for (unsigned long i = 0; i < pos; i++) frame[6 + i] = (uint8_t)json[i] ^ mask[i & 3];

    long n = sys_write(ws_fd, frame, 6 + pos);
    return (n == (long)(6 + pos)) ? 0 : -1;
}

static int is_yam_mode = 0;

static const char *get_controller_sh(void) {
    int fd = sys_open("./bin/control.sh", 0 /* O_RDONLY */, 0);
    if (fd >= 0) { sys_close(fd); return "./bin/control.sh"; }
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

static int json_find_str_bounded(const char *json, const char *end, const char *key,
                                 char *out, unsigned long max_out) {
    if (!json || !key || !out || max_out == 0) return 0;
    unsigned long klen = str_len(key);
    for (const char *p = json; *p && (!end || p < end); p++) {
        if (*p == '"' && (!end || p + 1 + klen < end) &&
            mem_equal(p + 1, key, klen) && p[1 + klen] == '"') {
            const char *v = p + 1 + klen + 1;
            while (*v && (!end || v < end) && (*v == ' ' || *v == ':')) v++;
            if (*v == '"') {
                v++;
                unsigned long o = 0;
                while (*v && (!end || v < end) && *v != '"') {
                    if (*v == '\\' && v[1] && (!end || v + 1 < end)) v++;
                    if (o + 1 < max_out) out[o++] = *v;
                    v++;
                }
                out[o] = 0;
                return 1;
            }
        }
    }
    return 0;
}

static int json_find_str(const char *json, const char *key, char *out, unsigned long max_out) {
    return json_find_str_bounded(json, 0, key, out, max_out);
}

/* Advances past a JSON string starting at opening '"'; returns pointer after closing '"'. */
static const char *json_skip_str(const char *p, const char *end) {
    if (!p || *p != '"') return p;
    p++;
    while (*p && (!end || p < end)) {
        if (*p == '\\' && p[1] && (!end || p + 1 < end)) {
            p += 2;
            continue;
        }
        if (*p == '"') return p + 1;
        p++;
    }
    return p;
}

/* Given pointer at '{' or '[', returns pointer just after the matching '}' or ']'. */
static const char *json_span_end(const char *p, const char *end) {
    if (!p || (*p != '{' && *p != '[')) return p;
    int depth = 0;
    while (*p && (!end || p < end)) {
        if (*p == '"') {
            p = json_skip_str(p, end);
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') {
            depth--;
            if (depth == 0) return p + 1;
        }
        p++;
    }
    return p;
}

/* Finds value pointer for direct child key inside JSON object [obj, end). */
static const char *json_find_key_in_obj(const char *obj, const char *end, const char *key) {
    if (!obj || *obj != '{') return 0;
    unsigned long klen = str_len(key);
    int depth = 0;
    const char *p = obj;
    while (*p && (!end || p < end)) {
        if (*p == '"') {
            int match = (depth == 1 && (!end || p + 1 + klen < end) &&
                         mem_equal(p + 1, key, klen) && p[1 + klen] == '"');
            p = json_skip_str(p, end);
            if (match) {
                while (*p && (!end || p < end) && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                return p;
            }
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') {
            depth--;
            if (depth <= 0) break;
        }
        p++;
    }
    return 0;
}

static int json_find_bool(const char *json, const char *key, int *val) {
    if (!json || !key || !val) return 0;
    unsigned long klen = str_len(key);
    for (unsigned long i = 0; json[i]; i++) {
        if (json[i] == '"' && mem_equal(json + i + 1, key, klen) && json[i + 1 + klen] == '"') {
            unsigned long idx = i + 1 + klen + 1;
            while (json[idx] == ' ' || json[idx] == ':') idx++;
            if (mem_equal(json + idx, "true", 4)) {
                *val = 1;
                return 1;
            }
            if (mem_equal(json + idx, "false", 5)) {
                *val = 0;
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
static int spotify_device_is_active = 0; // 1 = this speaker is active Spotify Connect device
static char current_track_uri[128] = {0};
static char current_track_name[128] = {0};
static char current_artist_name[128] = {0};
static int quasar_vol = -1;
static int quasar_vol_known = 0;
static int last_quasar_reported_vol = -1;
static int current_soloist_vol = -1;
static int ws_initial_vol_synced = 0;
static int need_soloist_vol_sync = 0;
static int pending_quasar_vol = -1;
static int64_t pending_quasar_vol_due_ms = 0;
static int last_sent_to_quasar_vol = -1;
static int64_t last_sent_to_quasar_ms = 0;
static int last_sent_to_soloist_vol = -1;
static int64_t last_sent_to_soloist_ms = 0;
static int64_t last_user_ws_vol_ms = 0;

static int abs_diff_int(int a, int b) {
    return a >= b ? (a - b) : (b - a);
}

static void on_quasar_volume_state(int qvol, uint64_t src, int64_t now) {
    if (qvol < 0) qvol = 0;
    if (qvol > 100) qvol = 100;

    if (!quasar_vol_known) {
        quasar_vol_known = 1;
        quasar_vol = qvol;
        last_quasar_reported_vol = qvol;
        if (last_user_ws_vol_ms == 0 && (current_soloist_vol != qvol || !ws_initial_vol_synced)) {
            need_soloist_vol_sync = 1;
        }
        return;
    }

    quasar_vol = qvol;
    if (qvol == last_quasar_reported_vol) {
        return;
    }
    last_quasar_reported_vol = qvol;

    int is_our_echo = (src == 1000 &&
                       last_sent_to_quasar_vol >= 0 &&
                       last_sent_to_quasar_ms > 0 &&
                       (now - last_sent_to_quasar_ms) < 1200 &&
                       abs_diff_int(qvol, last_sent_to_quasar_vol) <= 6);
    if (!is_our_echo) {
        if (pending_quasar_vol >= 0 && now >= pending_quasar_vol_due_ms - 60) {
            pending_quasar_vol = -1;
        }
        if (qvol != current_soloist_vol) {
            need_soloist_vol_sync = 1;
        }
    }
}
static char last_auto_next_uri[128] = {0};
static int playback_status = 0; // 0=stopped, 1=playing, 2=paused, 3=buffering
static uint64_t track_duration_ms = 0;
static uint64_t base_position_ms = 0;
static int64_t local_recorded_ms = 0;
static int playback_speed = 0;
static int64_t stuck_since_ms = 0;
static int64_t last_auto_next_time = 0;

static void append_artist_name(char *dst, unsigned long max_dst, const char *name) {
    if (!name || !*name) return;
    unsigned long len = str_len(dst);
    unsigned long nlen = str_len(name);
    if (len > 0) {
        if (len + 2 + nlen >= max_dst) return;
        dst[len++] = ',';
        dst[len++] = ' ';
    } else if (nlen >= max_dst) {
        nlen = max_dst - 1;
    }
    for (unsigned long i = 0; i < nlen && len + 1 < max_dst; i++) {
        dst[len++] = name[i];
    }
    dst[len] = 0;
}

static void extract_track_info(const char *json) {
    if (!json || *json != '{') return;

    const char *root_end = json_span_end(json, 0);
    const char *item_val = json_find_key_in_obj(json, root_end, "item");

    if (item_val) {
        if (mem_equal(item_val, "null", 4)) {
            current_track_name[0] = 0;
            current_artist_name[0] = 0;
            return;
        }
        if (*item_val == '{') {
            const char *item_end = json_span_end(item_val, root_end);
            char buf[128] = {0};

            // Reset metadata only when an item payload carries decorations/name
            const char *dec = json_find_key_in_obj(item_val, item_end, "decorations");
            if (dec && *dec == '{') {
                const char *dec_end = json_span_end(dec, item_end);
                const char *id = json_find_key_in_obj(dec, dec_end, "identity");
                if (id && *id == '{') {
                    const char *id_end = json_span_end(id, dec_end);
                    if (json_find_str_bounded(id, id_end, "name", buf, sizeof(buf))) {
                        str_copy(current_track_name, buf, sizeof(current_track_name));
                    }
                }

                current_artist_name[0] = 0;
                const char *creators = json_find_key_in_obj(dec, dec_end, "creators");
                if (creators && *creators == '[') {
                    const char *cr_end = json_span_end(creators, dec_end);
                    const char *p = creators + 1;
                    int artist_count = 0;
                    while (p < cr_end && artist_count < 3) {
                        while (p < cr_end && *p != '{' && *p != ']') p++;
                        if (p >= cr_end || *p != '{') break;
                        const char *el_end = json_span_end(p, cr_end);
                        const char *ent = json_find_key_in_obj(p, el_end, "entity");
                        const char *ent_end = (ent && *ent == '{') ? json_span_end(ent, el_end) : el_end;
                        const char *ent_obj = (ent && *ent == '{') ? ent : p;
                        const char *cdec = json_find_key_in_obj(ent_obj, ent_end, "decorations");
                        const char *cid_scope = (cdec && *cdec == '{') ? cdec : ent_obj;
                        const char *cid_end = (cdec && *cdec == '{') ? json_span_end(cdec, ent_end) : ent_end;
                        const char *cid = json_find_key_in_obj(cid_scope, cid_end, "identity");
                        if (cid && *cid == '{') {
                            const char *cid_obj_end = json_span_end(cid, cid_end);
                            char abuf[128] = {0};
                            if (json_find_str_bounded(cid, cid_obj_end, "name", abuf, sizeof(abuf))) {
                                append_artist_name(current_artist_name, sizeof(current_artist_name), abuf);
                                artist_count++;
                            }
                        }
                        p = el_end;
                    }
                }
            } else if (json_find_str_bounded(item_val, item_end, "name", buf, sizeof(buf))) {
                str_copy(current_track_name, buf, sizeof(current_track_name));
                if (json_find_str_bounded(item_val, item_end, "artist", buf, sizeof(buf))) {
                    str_copy(current_artist_name, buf, sizeof(current_artist_name));
                }
            }
            return;
        }
    }

    // Fallback for flat diagnostic payloads without nested "item"
    char name_buf[128] = {0};
    char artist_buf[128] = {0};
    if (json_find_str_bounded(json, root_end, "name", name_buf, sizeof(name_buf)) &&
        !str_contains(json, "\"device_name\"")) {
        str_copy(current_track_name, name_buf, sizeof(current_track_name));
    }
    if (json_find_str_bounded(json, root_end, "artist", artist_buf, sizeof(artist_buf))) {
        str_copy(current_artist_name, artist_buf, sizeof(current_artist_name));
    }
}

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
            run_controller("clean-cache-full", 0);
        }
    }

    int active_bool = 0;
    if (json_find_bool(json, "is_active", &active_bool)) {
        spotify_device_is_active = active_bool;
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
                if (current_track_uri[0] != 0) {
                    run_controller("clean-cache", 0);
                }
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
        str_contains(lower, "групп")) {
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

    if (is_web_or_culinary_intent(lower_buf)) {
        log_msg("[bridge] Web search or culinary intent, leaving for Alice / Quasar Bridge\n");
        return;
    }

    if (is_yandex_music_preference(lower_buf)) {
        log_msg("[bridge] Yandex Music/Radio preference requested, leaving to native Alice\n");
        return;
    }

    // Check "что играет" / track info query (only when not an explicit search like "включи песню ...")
    if (!is_explicit_search(lower_buf) &&
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
            str_has_word_prefix(lower_buf, "bluetooth") ||
            str_has_word_prefix(lower_buf, "увлажнител") ||
            str_has_word_prefix(lower_buf, "очистител") ||
            str_has_word_prefix(lower_buf, "обогревател") ||
            str_has_word_prefix(lower_buf, "вентилятор") ||
            str_has_word_prefix(lower_buf, "робот") ||
            str_has_word_prefix(lower_buf, "кофеварк") ||
            str_has_word_prefix(lower_buf, "кофемашин") ||
            str_has_word_prefix(lower_buf, "карниз") ||
            str_has_word_prefix(lower_buf, "штор") ||
            str_has_word_prefix(lower_buf, "выключател") ||
            str_has_word_prefix(lower_buf, "реле")) {
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

/* =========================================================================
 * LED Equalizer / Visualizer Subsystem (Nothing Phone style / Multi-zone)
 * Supports:
 *   - Yandex Station Lite 1: 3 RGB zones (micro:red:ring1..3)
 *   - Yandex Station Lite 2: 3 RGB zones (plum:red:ring1..3, restores led_ring_fb)
 *   - Yandex Station Mini 2: 6 RGB zones (mini2:red:ring1..6)
 *   - Yandex Station Midi:  24 RGB zones (cucumber:red:ring0..23)
 * Default Color: Pure White across all LEDs (Nothing Phone aesthetic).
 * Configurable via spotify_api.conf (LED_VISUALIZER=white|rainbow|cyan, LED_BRIGHTNESS=0..255)
 * ========================================================================= */

static const uint8_t sin64[64] = {
    128, 140, 152, 164, 176, 187, 198, 208, 217, 226, 233, 240, 245, 249, 252, 254,
    255, 254, 252, 249, 245, 240, 233, 226, 217, 208, 198, 187, 176, 164, 152, 140,
    128, 115, 103,  91,  79,  68,  57,  47,  38,  29,  22,  15,  10,   6,   3,   1,
      1,   1,   3,   6,  10,  15,  22,  29,  38,  47,  57,  68,  79,  91, 103, 115
};

struct led_hardware_info {
    int probed;
    int detected;
    int num_zones;
    int is_zero_indexed; // 1 for cucumber (ring0..ring23), 0 for micro/plum/mini2 (ring1..ringN)
    char prefix[32];     // e.g. "/sys/class/leds/micro:"
    int is_plum;         // requires restoring trigger led_ring_fb
};

static struct led_hardware_info g_led_hw = {0};
static int g_led_enabled = 0;       // 0=off (default), 1=on
static int g_led_color_mode = 0;    // 0=white (Nothing Phone style), 1=rainbow, 2=cyan, 3=red, 4=green, 5=blue, 6=magenta, 7=yellow
static int g_led_brightness = 180;  // 1-255, default 180
static uint64_t g_led_frame = 0;
static int g_led_last_active = 0;
static int64_t g_last_led_tick_ms = 0;
static uint8_t g_led_last_rgb[24][3] = {{0}};
static int g_led_rgb_cache_valid = 0;
static uint32_t g_bass_level = 0;
static uint32_t g_treble_level = 0;
static int32_t *g_audio_shm_ptr = 0;
static uint32_t g_band_floor[3] = {0};
static uint32_t g_band_peak[3] = {0};
static int g_band_init[3] = {0};
static int g_shm_is_32bit = 0;
static uint32_t g_prev_zone_val[24] = {0};

static inline uint32_t isqrt(uint64_t n) {
    uint64_t root = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= root + bit) {
            n -= (root + bit);
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}

static void audio_shm_ensure_attached(void) {
    if (g_audio_shm_ptr) return;
    int shmid = sys_shmget(0x401, 61440, 0444);
    if (shmid >= 0) {
        void *ptr = sys_shmat(shmid, 0, 010000 /* SHM_RDONLY */);
        if ((long)ptr > 0 && (long)ptr != -1) {
            g_audio_shm_ptr = (int32_t *)ptr;
        }
    }
}

static void audio_shm_detach(void) {
    if (g_audio_shm_ptr) {
        sys_shmdt(g_audio_shm_ptr);
        g_audio_shm_ptr = 0;
    }
}

// Adaptive per-band baseline/peak envelope with noise gate and cubic gamma curve
static uint32_t compute_band_energy(int band_idx, uint32_t e, uint32_t total_rms) {
    if (band_idx < 0 || band_idx >= 3) return 0;

    if (!g_band_init[band_idx]) {
        g_band_floor[band_idx] = (uint32_t)(((uint64_t)e * 92) / 100);
        g_band_peak[band_idx] = (uint32_t)(((uint64_t)e * 115) / 100);
        g_band_init[band_idx] = 1;
    }

    uint32_t fl = g_band_floor[band_idx];
    uint32_t pk = g_band_peak[band_idx];

    // Floor tracks sustained signal slowly up and faster down
    if (e < fl) {
        fl = (uint32_t)(((uint64_t)fl * 86 + (uint64_t)e * 14) / 100);
    } else {
        fl = (uint32_t)(((uint64_t)fl * 95 + (uint64_t)e * 5) / 100);
    }

    // Peak tracks beats instantaneously up and decays smoothly toward floor
    if (e > pk) {
        pk = e;
    } else {
        uint32_t dec = ((uint64_t)fl * 3 < (uint64_t)pk) ? 95 : 98;
        pk = (uint32_t)(((uint64_t)pk * dec + (uint64_t)fl * (100 - dec)) / 100);
    }

    g_band_floor[band_idx] = fl;
    g_band_peak[band_idx] = pk;

    // Suppress digital silence/very quiet noise (<1.5 LSB) and out-of-band filter bleed (<4% of total RMS)
    if (e <= 384 || (uint64_t)e * 25 <= (uint64_t)total_rms) return 0;

    uint32_t span = (pk > fl) ? (pk - fl) : 0;
    uint32_t min_span = (uint32_t)(((uint64_t)pk * 30) / 100);
    if (span < min_span) span = min_span;
    if (span < 384) span = 384;

    uint32_t ex = (e > fl) ? (e - fl) : 0;
    uint32_t norm = (ex >= span) ? 255 : (uint32_t)(((uint64_t)ex * 255) / span);

    // 25% deadzone keeps sustained beds dark while blending quad/cubic gamma smoothly
    if (norm <= 64) return 0;

    uint32_t scaled = ((norm - 64) * 255) / (255 - 64);
    uint32_t sq = (scaled * scaled) / 255;
    uint32_t cb = (sq * scaled) / 255;
    return (sq + cb * 2) / 3;
}

// Fixed-point (<<8) 3-band 2-pole crossover across Left channel dmix buffer (7680 frames @ 48kHz)
static void analyze_shm_bands(const int32_t *shm, int num_frames,
                              uint32_t *bass_out, uint32_t *mid_out, uint32_t *treble_out) {
    *bass_out = 0;
    *mid_out = 0;
    *treble_out = 0;
    if (!shm || num_frames <= 0) return;

    for (int pass = 0; pass < 2; pass++) {
        int64_t total_sum_sq = 0;
        int64_t bass_sum_sq = 0;
        int64_t mid_sum_sq = 0;
        int64_t treble_sum_sq = 0;
        int64_t lpf1a = 0, lpf1b = 0;
        int64_t lpf2a = 0, lpf_hp2 = 0;
        int64_t mid_lp = 0;
        int mode_switched = 0;

        for (int f = 0; f < num_frames; f++) {
            int32_t s = shm[f * 2];
            if (!g_shm_is_32bit && (s > 65535 || s < -65535)) {
                g_shm_is_32bit = 1;
                for (int k = 0; k < 3; k++) {
                    g_band_init[k] = 0;
                    g_band_floor[k] = 0;
                    g_band_peak[k] = 0;
                }
                mode_switched = 1;
                break;
            }

            // Normalize both S16_LE and S32_LE dmix buffers into S16<<8 fixed-point scale
            int64_t sfp = g_shm_is_32bit ? (((int64_t)s) / 256) : (((int64_t)s) * 256);

            // Bass: 2-pole LPF ~240Hz (1/32, -12dB/oct)
            lpf1a = (lpf1a * 31 + sfp) / 32;
            lpf1b = (lpf1b * 31 + lpf1a) / 32;
            int64_t b = lpf1b;

            // Treble: 2-pole HPF ~955Hz (1/8, -12dB/oct)
            lpf2a = (lpf2a * 7 + sfp) / 8;
            int64_t hp2 = sfp - lpf2a;
            lpf_hp2 = (lpf_hp2 * 7 + hp2) / 8;
            int64_t t = hp2 - lpf_hp2;

            // Mid: bandpass (~240..955Hz)
            mid_lp = (mid_lp * 7 + (sfp - lpf1a)) / 8;
            int64_t m = mid_lp;

            total_sum_sq += sfp * sfp;
            bass_sum_sq += b * b;
            mid_sum_sq += m * m;
            treble_sum_sq += t * t;
        }

        if (mode_switched) continue;

        uint32_t total_rms = isqrt((uint64_t)(total_sum_sq / num_frames));
        uint32_t bass_rms = isqrt((uint64_t)(bass_sum_sq / num_frames));
        uint32_t mid_rms = isqrt((uint64_t)(mid_sum_sq / num_frames));
        uint32_t treble_rms = isqrt((uint64_t)(treble_sum_sq / num_frames));

        *bass_out = compute_band_energy(0, bass_rms, total_rms);
        *mid_out = compute_band_energy(1, mid_rms, total_rms);
        *treble_out = compute_band_energy(2, treble_rms, total_rms);
        return;
    }
}

static void write_sysfs_int(const char *path, int val) {
    int fd = sys_open(path, 01 /* O_WRONLY */, 0);
    if (fd < 0) return;
    char buf[16];
    int len = 0;
    if (val <= 0) {
        buf[0] = '0';
        buf[1] = '\n';
        len = 2;
    } else {
        if (val > 255) val = 255;
        char tmp[10];
        int tp = 0;
        int n = val;
        while (n > 0) {
            tmp[tp++] = '0' + (n % 10);
            n /= 10;
        }
        while (tp > 0) {
            buf[len++] = tmp[--tp];
        }
        buf[len++] = '\n';
    }
    sys_write(fd, buf, len);
    sys_close(fd);
}

static void write_sysfs_str(const char *path, const char *str) {
    int fd = sys_open(path, 01 /* O_WRONLY */, 0);
    if (fd < 0) return;
    sys_write(fd, str, str_len(str));
    sys_close(fd);
}

static void build_led_path(char *dst, unsigned long max, const char *color, int zone_idx) {
    unsigned long p = 0;
    const char *pr = g_led_hw.prefix;
    while (*pr && p + 1 < max) dst[p++] = *pr++;
    while (*color && p + 1 < max) dst[p++] = *color++;
    const char *mid = ":ring";
    while (*mid && p + 1 < max) dst[p++] = *mid++;
    if (zone_idx >= 10) {
        if (p + 1 < max) dst[p++] = '0' + (zone_idx / 10);
        if (p + 1 < max) dst[p++] = '0' + (zone_idx % 10);
    } else {
        if (p + 1 < max) dst[p++] = '0' + zone_idx;
    }
    const char *suf = "/brightness";
    while (*suf && p + 1 < max) dst[p++] = *suf++;
    dst[p] = 0;
}

static void led_probe(void) {
    if (g_led_hw.probed) return;
    g_led_hw.probed = 1;

    // Check Station Lite 1 (micro)
    int fd = sys_open("/sys/class/leds/micro:red:ring1/brightness", 0, 0);
    if (fd >= 0) {
        sys_close(fd);
        g_led_hw.detected = 1;
        g_led_hw.num_zones = 3;
        g_led_hw.is_zero_indexed = 0;
        g_led_hw.is_plum = 0;
        str_copy(g_led_hw.prefix, "/sys/class/leds/micro:", sizeof(g_led_hw.prefix));
        log_msg("[bridge] LEDs detected: Station Lite 1 (3 RGB zones)\n");
        return;
    }
    // Check Station Lite 2 (plum)
    fd = sys_open("/sys/class/leds/plum:red:ring1/brightness", 0, 0);
    if (fd >= 0) {
        sys_close(fd);
        g_led_hw.detected = 1;
        g_led_hw.num_zones = 3;
        g_led_hw.is_zero_indexed = 0;
        g_led_hw.is_plum = 1;
        str_copy(g_led_hw.prefix, "/sys/class/leds/plum:", sizeof(g_led_hw.prefix));
        log_msg("[bridge] LEDs detected: Station Lite 2 (3 RGB zones, Plum)\n");
        return;
    }
    // Check Station Mini 2 (mini2)
    fd = sys_open("/sys/class/leds/mini2:red:ring1/brightness", 0, 0);
    if (fd >= 0) {
        sys_close(fd);
        g_led_hw.detected = 1;
        g_led_hw.num_zones = 6;
        g_led_hw.is_zero_indexed = 0;
        g_led_hw.is_plum = 0;
        str_copy(g_led_hw.prefix, "/sys/class/leds/mini2:", sizeof(g_led_hw.prefix));
        log_msg("[bridge] LEDs detected: Station Mini 2 (6 RGB zones)\n");
        return;
    }
    // Check Station Midi (cucumber)
    fd = sys_open("/sys/class/leds/cucumber:red:ring0/brightness", 0, 0);
    if (fd >= 0) {
        sys_close(fd);
        g_led_hw.detected = 1;
        g_led_hw.num_zones = 24;
        g_led_hw.is_zero_indexed = 1;
        g_led_hw.is_plum = 0;
        str_copy(g_led_hw.prefix, "/sys/class/leds/cucumber:", sizeof(g_led_hw.prefix));
        log_msg("[bridge] LEDs detected: Station Midi (24 RGB zones)\n");
        return;
    }
}

static void led_reset_internal_state(void) {
    audio_shm_detach();
    for (int i = 0; i < 24; i++) {
        g_prev_zone_val[i] = 0;
        g_led_last_rgb[i][0] = 0;
        g_led_last_rgb[i][1] = 0;
        g_led_last_rgb[i][2] = 0;
    }
    for (int k = 0; k < 3; k++) {
        g_band_floor[k] = 0;
        g_band_peak[k] = 0;
        g_band_init[k] = 0;
    }
    g_shm_is_32bit = 0;
    g_led_last_active = 0;
    g_led_rgb_cache_valid = 0;
    g_last_led_tick_ms = 0;
}

static void led_all_off(void) {
    led_reset_internal_state();
    if (!g_led_hw.detected) return;
    int start = g_led_hw.is_zero_indexed ? 0 : 1;
    int end = start + g_led_hw.num_zones;
    char path[80];
    for (int z = start; z < end; z++) {
        build_led_path(path, sizeof(path), "red", z);
        write_sysfs_int(path, 0);
        build_led_path(path, sizeof(path), "green", z);
        write_sysfs_int(path, 0);
        build_led_path(path, sizeof(path), "blue", z);
        write_sysfs_int(path, 0);
    }
    if (g_led_hw.is_plum) {
        for (int z = start; z < end; z++) {
            char tpath[80];
            build_led_path(tpath, sizeof(tpath), "red", z);
            for (int i = 0; tpath[i]; i++) {
                if (tpath[i] == 'b' && tpath[i+1] == 'r') {
                    str_copy(tpath + i, "trigger", sizeof(tpath) - i);
                    break;
                }
            }
            write_sysfs_str(tpath, "led_ring_fb\n");
        }
    }
}

static void set_led_visualizer_impl(int enable) {
    led_probe();
    g_led_enabled = enable ? 1 : 0;
    if (!g_led_enabled) {
        if (alice_owns_leds(now_ms())) {
            led_reset_internal_state();
        } else {
            led_all_off();
        }
    }
    const char *state_path = is_yam_mode ? "./data/requests/led_state" : "/data/opt/soloist/data/requests/led_state";
    int fd = sys_open(state_path, 0100 | 01 | 01000 /* O_CREAT|O_WRONLY|O_TRUNC */, 0644);
    if (fd >= 0) {
        if (g_led_enabled) {
            sys_write(fd, "on\n", 3);
        } else {
            sys_write(fd, "off\n", 4);
        }
        sys_close(fd);
    }
    log_msg(g_led_enabled ? "[bridge] LED visualizer: ON (Nothing Phone Equalizer)\n" : "[bridge] LED visualizer: OFF\n");
}

static void read_led_config(void) {
    const char *conf_path = is_yam_mode ? "./spotify_api.conf" : "/data/opt/soloist/spotify_api.conf";
    int fd = sys_open(conf_path, 0, 0);
    if (fd >= 0) {
        char buf[1024];
        long n = sys_read(fd, buf, sizeof(buf) - 1);
        sys_close(fd);
        if (n > 0) {
            buf[n] = 0;
            if (str_contains(buf, "LED_VISUALIZER=on") || str_contains(buf, "LED_VISUALIZER=1")) {
                g_led_enabled = 1;
            } else if (str_contains(buf, "LED_VISUALIZER=rainbow")) {
                g_led_enabled = 1;
                g_led_color_mode = 1;
            } else if (str_contains(buf, "LED_VISUALIZER=cyan")) {
                g_led_enabled = 1;
                g_led_color_mode = 2;
            } else if (str_contains(buf, "LED_VISUALIZER=white")) {
                g_led_enabled = 1;
                g_led_color_mode = 0;
            }
        }
    }

    const char *state_path = is_yam_mode ? "./data/requests/led_state" : "/data/opt/soloist/data/requests/led_state";
    int sfd = sys_open(state_path, 0, 0);
    if (sfd >= 0) {
        char sbuf[32];
        long sn = sys_read(sfd, sbuf, sizeof(sbuf) - 1);
        sys_close(sfd);
        if (sn > 0) {
            sbuf[sn] = 0;
            if (str_contains(sbuf, "on")) g_led_enabled = 1;
            else if (str_contains(sbuf, "off")) g_led_enabled = 0;
        }
    }
}

static void check_led_request(void) {
    const char *path = is_yam_mode ? "./data/requests/led" : "/data/opt/soloist/data/requests/led";
    int fd = sys_open(path, 0, 0);
    if (fd < 0) return;
    char cmd[16];
    long n = sys_read(fd, cmd, sizeof(cmd) - 1);
    sys_close(fd);
    sys_unlink(path);

    if (n > 0) {
        if (cmd[0] == '1' || (cmd[0] == 'o' && cmd[1] == 'n')) {
            set_led_visualizer(1);
        } else if (cmd[0] == '0' || (cmd[0] == 'o' && cmd[1] == 'f')) {
            set_led_visualizer(0);
        } else if (cmd[0] == 'w') {
            g_led_color_mode = 0;
            set_led_visualizer(1);
        } else if (cmd[0] == 'r') {
            g_led_color_mode = 1;
            set_led_visualizer(1);
        } else if (cmd[0] == 'c') {
            g_led_color_mode = 2;
            set_led_visualizer(1);
        }
    }
}

static void write_zone_rgb_if_changed(int i, int z, int r, int g, int b) {
    char path[80];
    if (!g_led_rgb_cache_valid || g_led_last_rgb[i][0] != (uint8_t)r) {
        build_led_path(path, sizeof(path), "red", z);
        write_sysfs_int(path, r);
        g_led_last_rgb[i][0] = (uint8_t)r;
    }
    if (!g_led_rgb_cache_valid || g_led_last_rgb[i][1] != (uint8_t)g) {
        build_led_path(path, sizeof(path), "green", z);
        write_sysfs_int(path, g);
        g_led_last_rgb[i][1] = (uint8_t)g;
    }
    if (!g_led_rgb_cache_valid || g_led_last_rgb[i][2] != (uint8_t)b) {
        build_led_path(path, sizeof(path), "blue", z);
        write_sysfs_int(path, b);
        g_led_last_rgb[i][2] = (uint8_t)b;
    }
}

static void led_visualizer_tick(int64_t now) {
    if (!g_led_hw.detected) return;

    // Protect Alice listening/thinking/speaking LED animations from overwrite or zero-clearing
    if (alice_owns_leds(now)) {
        if (g_led_last_active || g_audio_shm_ptr) {
            led_reset_internal_state();
        }
        return;
    }

    if (!g_led_enabled || playback_status != 1 || !spotify_device_is_active) {
        if (g_led_last_active) {
            led_all_off();
        }
        return;
    }

    // Pace DSP analysis & sysfs LED updates cleanly (~30 FPS) regardless of socket wakeup bursts
    if (g_last_led_tick_ms != 0 && (now - g_last_led_tick_ms) < 30) {
        return;
    }
    g_last_led_tick_ms = now;

    g_led_last_active = 1;
    g_led_frame++;

    audio_shm_ensure_attached();

    uint32_t bass_energy = 0;
    uint32_t mid_energy = 0;
    uint32_t treble_energy = 0;

    if (g_audio_shm_ptr) {
        analyze_shm_bands(g_audio_shm_ptr, 7680, &bass_energy, &mid_energy, &treble_energy);
    } else {
        // Fallback simulation if SHM is unavailable
        uint32_t bar_pos = g_led_frame % 40;
        if (bar_pos == 0 || bar_pos == 10 || bar_pos == 20 || bar_pos == 30 || bar_pos == 26) {
            g_bass_level = 255;
        } else {
            g_bass_level = (g_bass_level * 78) / 100;
        }
        if ((bar_pos % 4) == 0) {
            g_treble_level = 220;
        } else {
            g_treble_level = (g_treble_level * 55) / 100;
        }
        uint32_t s1 = sin64[(g_led_frame * 2) % 64];
        uint32_t s2 = sin64[(g_led_frame * 3 + 16) % 64];
        uint32_t sim_mid = (s1 * 7 + s2 * 3) / 10;

        bass_energy = g_bass_level;
        mid_energy = sim_mid;
        treble_energy = g_treble_level;
    }

    int n_zones = g_led_hw.num_zones;
    int start = g_led_hw.is_zero_indexed ? 0 : 1;

    for (int i = 0; i < n_zones; i++) {
        int z = start + i;
        uint32_t target_energy = 0;

        if (n_zones == 3) {
            if (i == 0) target_energy = bass_energy;
            else if (i == 1) target_energy = mid_energy;
            else target_energy = treble_energy;
        } else if (n_zones == 6) {
            if (i == 0) target_energy = bass_energy;
            else if (i == 1) target_energy = (bass_energy + mid_energy) / 2;
            else if (i == 2) target_energy = mid_energy;
            else if (i == 3) target_energy = (mid_energy + treble_energy) / 2;
            else if (i == 4) target_energy = treble_energy;
            else target_energy = (treble_energy + bass_energy) / 2;
        } else {
            uint32_t fraction = ((uint32_t)i * 3) / (uint32_t)n_zones;
            if (fraction == 0) target_energy = bass_energy;
            else if (fraction == 1) target_energy = mid_energy;
            else target_energy = treble_energy;
        }

        if (target_energy > 255) target_energy = 255;

        // Smooth rise and exponential release prevent harsh strobe flashes at 30 FPS
        uint32_t prev = g_prev_zone_val[i];
        if (target_energy > prev) {
            g_prev_zone_val[i] = (prev * 50 + target_energy * 50) / 100;
        } else {
            g_prev_zone_val[i] = (prev * 85 + target_energy * 15) / 100;
        }

        uint32_t val = (g_prev_zone_val[i] * (uint32_t)g_led_brightness) / 255;

        int r = 0, g = 0, b = 0;
        if (g_led_color_mode == 0) {
            r = val; g = val; b = val;
        } else if (g_led_color_mode == 1) {
            uint32_t hue = (g_led_frame * 2 + (uint32_t)i * (64 / (uint32_t)n_zones)) % 64;
            r = (sin64[hue] * val) / 255;
            g = (sin64[(hue + 21) % 64] * val) / 255;
            b = (sin64[(hue + 42) % 64] * val) / 255;
        } else if (g_led_color_mode == 2) {
            r = 0; g = val; b = val;
        } else if (g_led_color_mode == 3) {
            r = val; g = 0; b = 0;
        } else if (g_led_color_mode == 4) {
            r = 0; g = val; b = 0;
        } else if (g_led_color_mode == 5) {
            r = 0; g = 0; b = val;
        } else if (g_led_color_mode == 6) {
            r = val; g = 0; b = val;
        } else if (g_led_color_mode == 7) {
            r = val; g = val; b = 0;
        }

        write_zone_rgb_if_changed(i, z, r, g, b);
    }
    g_led_rgb_cache_valid = 1;
}

static uint8_t quasar_buf[65536];
static unsigned long quasar_buf_pos = 0;
static uint8_t quasar_vol_buf[16384];
static unsigned long quasar_vol_buf_pos = 0;
static uint8_t ws_buf[8192];
static unsigned long ws_buf_pos = 0;
static struct voice_request_state voice_requests;

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

void _start(void) {
    int test_fd = sys_open("./bin/soloist", 0 /* O_RDONLY */, 0);
    if (test_fd >= 0) {
        sys_close(test_fd);
        is_yam_mode = 1;
        log_msg("[bridge] soloist-bridge v15 started (YAM plugin mode: relative paths)\n");
    } else {
        log_msg("[bridge] soloist-bridge v15 started (Standalone mode: /data/opt/soloist)\n");
    }

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
        int led_running = (g_led_enabled && g_led_hw.detected && playback_status == 1 && spotify_device_is_active && !alice_owns_leds(now));
        tmo.tv_nsec = led_running ? 33000000 : 50000000;

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

                        const uint8_t *frame_body = quasar_buf + 4;
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
                                    if (is_alice_visual_state(g_alice_state) && g_led_alice_yield_until_ms < st_now + 450) {
                                        g_led_alice_yield_until_ms = st_now + 450;
                                    }
                                    g_alice_state = alice_state;
                                }
                                if (alice_state == 1) {
                                    // User said "Alice" (wake-word spotted): clear suppression window immediately
                                    // so the dialog is not killed and user can speak; duck Spotify audio to 20%.
                                    cancel_alice_until_ms = 0;
                                    g_tts_hold_until_ms = 0;
                                    set_alsa_yield(0, st_now);
                                    set_ducking(1, st_now);
                                } else if (alice_state == 2 || alice_state == 3) {
                                    if (st_now < cancel_alice_until_ms) {
                                        // Suppress Alice cloud processing or TTS response from a hijacked Spotify command
                                        send_cancel_alice(quasar_fd);
                                        set_alsa_yield(0, st_now);
                                        set_ducking(0, st_now);
                                    } else {
                                        if (alice_state == 2) g_tts_hold_until_ms = 0;
                                        set_ducking(1, st_now);
                                        set_alsa_yield(1, st_now);
                                    }
                                } else if (alice_state == 5) {
                                    if (st_now >= cancel_alice_until_ms) {
                                        set_ducking(1, st_now);
                                        set_alsa_yield(1, st_now);
                                    }
                                } else if (alice_state == 0 || alice_state == 6) {
                                    if (st_now >= g_tts_hold_until_ms) {
                                        g_tts_hold_until_ms = 0;
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
                                    const uint8_t *method_body = 0;
                                    unsigned long method_len = 0;
                                    if (find_field(remoting_body, remoting_len, 13, 2, &method_body, &method_len, 0)) {
                                        uint64_t m_id = 0;
                                        if (find_field(method_body, method_len, 1, 0, 0, 0, &m_id)) {
                                            log_msg("[bridge] Gesture PlaybackControlCapability received\n");
                                            if (m_id == 1) { // PAUSE
                                                handle_voice_recognition(quasar_fd, "пауза", 10);
                                            } else if (m_id == 2) { // PLAY / RESUME
                                                handle_voice_recognition(quasar_fd, "играй", 10);
                                            } else if (m_id == 3) { // NEXT
                                                handle_voice_recognition(quasar_fd, "дальше", 12);
                                            } else if (m_id == 4) { // PREV
                                                handle_voice_recognition(quasar_fd, "назад", 10);
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

            // Drain incoming IOEvents from Quasar IOHub (9910)
            if (quasar_io_idx >= 0 && (pfd[quasar_io_idx].revents & (0x0001 | 0x0008 | 0x0010 | 0x0020))) {
                uint8_t drain[4096];
                long n = (pfd[quasar_io_idx].revents & 0x0001) ? sys_read(quasar_io_fd, drain, sizeof(drain)) : -1;
                if (n <= 0) {
                    sys_close(quasar_io_fd);
                    quasar_io_fd = -1;
                }
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
                                    spotify_is_active = 1;
                                    run_soloist_cmd("next");
                                    continue;
                                }
                                if (is_prev_key && ev.value == 1) {
                                    log_msg("[bridge] Hardware key PREV pressed\n");
                                    trigger_alice_suppression(quasar_fd, 3000);
                                    spotify_is_active = 1;
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
                                                    spotify_is_active = 1;
                                                    run_soloist_cmd("next");
                                                } else {
                                                    last_touch_release_time = now_tap;
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
            if (quasar_io_fd >= 0) {
                if (send_quasar_volume(quasar_io_fd, target) < 0) {
                    sys_close(quasar_io_fd);
                    quasar_io_fd = -1;
                }
            } else {
                char vol_str[12];
                format_uint_ascii(vol_str, target);
                run_controller("volume", vol_str);
            }
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

        // Auto-Next Watchdog (Issue #4): Detect track end stall caused by apulse missing underflow callback
        if (soloist_ws_fd >= 0 && soloist_ws_handshake) {
            if (spotify_device_is_active && (playback_status == 1 || playback_status == 3) && track_duration_ms > 2000) {
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

        // 5. LED Visualizer / Equalizer update & request processing
        check_led_request();
        led_visualizer_tick(now_ms());
    }
}
