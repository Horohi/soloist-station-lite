#ifndef SOLOIST_QUASAR_IPC_H
#define SOLOIST_QUASAR_IPC_H

#include <stdint.h>
#include "voice-request.h"
#include "duck_shm.h"

/* Builds and sends one AliceCapability method frame via Quasar Native IPC (QuasarMessage field 210). */
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

/* Also sends SharedMessage wrapper (field 252 + field 253: 210) for newer/alternate Quasar daemons. */
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

/* Sends PlaybackControlCapability PAUSE (method kind 1) to pause any native media player. */
static void send_playback_pause(int quasar_fd) {
    if (quasar_fd < 0) return;
    static const unsigned char p210[38] = {
        0x22, 0x00, 0x00, 0x00,
        0x92, 0x0d, 0x1f,
        0x3a, 0x19,
        'P','l','a','y','b','a','c','k','C','o','n','t','r','o','l','C','a','p','a','b','i','l','i','t','y',
        0x6a, 0x02, 0x08, 0x01
    };
    sys_write(quasar_fd, p210, sizeof(p210));

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
static int g_tts_speaking_seen = 0;
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
        int probe = sys_open(SOLOIST_YIELD_DEFAULT_PATH, 0, 0);
        if (probe >= 0) {
            sys_close(probe);
        } else {
            int fd = sys_open(SOLOIST_YIELD_DEFAULT_PATH, 0100 | 01 /* O_CREAT | O_WRONLY */, 0644);
            if (fd >= 0) sys_close(fd);
        }
        g_alsa_yield_active = 1;
        if (g_alsa_yield_since_ms == 0) g_alsa_yield_since_ms = now;
    } else if (g_alsa_yield_active || g_alsa_yield_since_ms != 0) {
        sys_unlink(SOLOIST_YIELD_DEFAULT_PATH);
        g_alsa_yield_active = 0;
        g_alsa_yield_since_ms = 0;
    }
}

static int64_t cancel_alice_until_ms = 0;

/* Sends Alice START_VOCALIZE directive so Alice speaks natively on the speaker. */
static int send_alice_tts(int quasar_fd, const char *text) {
    if (quasar_fd < 0 || !text || !*text) return -1;

    send_cancel_alice(quasar_fd);
    cancel_alice_until_ms = 0;
    int64_t tnow = now_ms();
    g_tts_speaking_seen = 0;
    g_tts_hold_until_ms = tnow + 4000;
    set_ducking(1, tnow);
    set_alsa_yield(1, tnow);
    int alsa_released = 0;
    for (int waited_ms = 20; waited_ms <= 400; waited_ms += 20) {
        sys_msleep(20);
        if (waited_ms >= 80 && sys_shmget(1024, 0, 0) == -2 /* ENOENT */) {
            alsa_released = 1;
            break;
        }
    }
    if (!alsa_released) {
        log_msg("[bridge] Alice TTS cancelled: ALSA SHM still present or inaccessible\n");
        return -1;
    }

    unsigned long text_len = str_len(text);
    if (text_len > 350) text_len = 350;

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

    uint8_t frame[600];
    unsigned long pos = 4;
    if (quasar_uses_f252 == 1) {
        frame[pos++] = 0xe2;
        frame[pos++] = 0x0f; // tag 252 wire 2
    } else {
        frame[pos++] = 0x92;
        frame[pos++] = 0x0d; // tag 210 wire 2
    }
    if (r_pos >= 128) {
        frame[pos++] = (uint8_t)((r_pos & 0x7f) | 0x80);
        frame[pos++] = (uint8_t)(r_pos >> 7);
    } else {
        frame[pos++] = (uint8_t)r_pos;
    }
    for (unsigned long i = 0; i < r_pos; i++) frame[pos++] = remoting[i];
    if (quasar_uses_f252 == 1) {
        frame[pos++] = 0xe8;
        frame[pos++] = 0x0f; // tag 253 wire 0
        frame[pos++] = 0xd2;
        frame[pos++] = 0x01; // value 210 (QuasarMessage::remoting)
    }
    uint32_t body_len = (uint32_t)(pos - 4);
    frame[0] = (uint8_t)(body_len & 0xff);
    frame[1] = (uint8_t)((body_len >> 8) & 0xff);
    frame[2] = (uint8_t)((body_len >> 16) & 0xff);
    frame[3] = (uint8_t)((body_len >> 24) & 0xff);

    long n = sys_write(quasar_fd, frame, pos);
    log_msg("[bridge] Sent Alice TTS START_VOCALIZE: ");
    log_msg(text);
    log_msg("\n");
    return (n == (long)pos) ? 0 : -1;
}

static void trigger_alice_suppression(int quasar_fd, int duration_ms) {
    int64_t tnow = now_ms();
    cancel_alice_until_ms = tnow + duration_ms;
    g_tts_hold_until_ms = 0;
    set_ducking(0, tnow);
    set_alsa_yield(0, tnow);
    if (is_alice_visual_state(g_alice_state)) {
        g_led_alice_yield_until_ms = tnow + 150;
    }
    send_cancel_alice(quasar_fd);
}

/* Parse Quasar IOEvent (port 9910) AppState media activity.
 * Returns 1 if an AppState field was present and sets *media_active_out:
 *   2 = native media playing/buffering (Yandex Music / Radio / Bluetooth)
 *   1 = native media paused
 *   0 = idle / stopped / finished / failed / empty AppState
 * Returns 0 if frame is not an IOEvent AppState update. */
static int parse_quasar_io_media_state(const uint8_t *frame_body, unsigned long frame_len, int *media_active_out) {
    const uint8_t *io_body = 0;
    unsigned long io_len = 0;
    uint64_t msg_type = 0;
    if (find_field(frame_body, frame_len, 85, 2, &io_body, &io_len, 0)) {
        // Direct IOEvent field 85
    } else if (find_field(frame_body, frame_len, 253, 0, 0, 0, &msg_type) && msg_type == 85 &&
               find_field(frame_body, frame_len, 252, 2, &io_body, &io_len, 0)) {
        // SharedMessage wrapper (field 252/253=85)
    } else {
        return 0;
    }

    const uint8_t *app_body = 0;
    unsigned long app_len = 0;
    if (!find_field(io_body, io_len, 5, 2, &app_body, &app_len, 0)) {
        return 0;
    }

    int state = 0;

    const uint8_t *ac_body = 0;
    unsigned long ac_len = 0;
    if (find_field(app_body, app_len, 6, 2, &ac_body, &ac_len, 0) && ac_len > 0) {
        uint64_t ac_state = 0;
        if (find_field(ac_body, ac_len, 2, 0, 0, 0, &ac_state)) {
            if (ac_state == 2 || ac_state == 3) state = 2;
            else if (ac_state == 4 && state < 1) state = 1;
        }
    }

    const uint8_t *m_body = 0;
    unsigned long m_len = 0;
    if (find_field(app_body, app_len, 2, 2, &m_body, &m_len, 0) && m_len > 0) {
        const uint8_t *tid = 0;
        unsigned long tid_len = 0;
        if (find_field(m_body, m_len, 1, 2, &tid, &tid_len, 0) && tid_len > 0) {
            uint64_t paused = 0;
            find_field(m_body, m_len, 3, 0, 0, 0, &paused);
            if (!paused) state = 2;
            else if (state < 1) state = 1;
        }
    }

    const uint8_t *r_body = 0;
    unsigned long r_len = 0;
    if (find_field(app_body, app_len, 4, 2, &r_body, &r_len, 0) && r_len > 0) {
        const uint8_t *rid = 0;
        unsigned long rid_len = 0;
        if (find_field(r_body, r_len, 1, 2, &rid, &rid_len, 0) && rid_len > 0) {
            uint64_t paused = 0;
            find_field(r_body, r_len, 3, 0, 0, 0, &paused);
            if (!paused) state = 2;
            else if (state < 1) state = 1;
        }
    }

    const uint8_t *bt_body = 0;
    unsigned long bt_len = 0;
    if (find_field(app_body, app_len, 5, 2, &bt_body, &bt_len, 0) && bt_len > 0) {
        const uint8_t *meta = 0;
        unsigned long meta_len = 0;
        uint64_t last_play = 0;
        int has_bt_meta = find_field(bt_body, bt_len, 2, 2, &meta, &meta_len, 0) && meta_len > 0;
        int has_bt_ts = find_field(bt_body, bt_len, 4, 0, 0, 0, &last_play) && last_play > 0;
        if (has_bt_meta || has_bt_ts) {
            uint64_t paused = 0;
            find_field(bt_body, bt_len, 1, 0, 0, 0, &paused);
            if (!paused) state = 2;
            else if (state < 1) state = 1;
        }
    }

    if (media_active_out) *media_active_out = state;
    return 1;
}

#endif
