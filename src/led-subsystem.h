#ifndef SOLOIST_LED_SUBSYSTEM_H
#define SOLOIST_LED_SUBSYSTEM_H

#include <stdint.h>

/* =========================================================================
 * LED Equalizer / Visualizer & Feedback FX Subsystem
 * Supports:
 *   - Yandex Station Lite 1: 3 RGB zones (micro:red:ring1..3)
 *   - Yandex Station Lite 2: 3 RGB zones (plum:red:ring1..3, restores led_ring_fb)
 *   - Yandex Station Mini 2: 6 RGB zones (mini2:red:ring1..6)
 *   - Yandex Station Midi:  24 RGB zones (cucumber:red:ring0..23)
 * Default Color: Pure White across all LEDs (Nothing Phone aesthetic).
 * Configurable via spotify_api.conf (LED_VISUALIZER, LED_ANIMATIONS)
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
static int g_led_enabled = 1;       // 1=on white (default), 0=off
static int g_led_color_mode = 0;    // 0=white (Nothing Phone style), 1=rainbow, 2=cyan, 3=red, 4=green, 5=blue, 6=magenta, 7=yellow
static int g_led_brightness = 180;  // 1-255, default 180
static uint64_t g_led_frame = 0;
static int g_led_last_active = 0;
/* Set when the equalizer has painted the ring and not yet erased it. Unlike
 * g_led_last_active it survives Alice borrowing the LEDs, so the "nothing is
 * playing" branch below still knows a stale frame is sitting in the hardware. */
static int g_led_ring_dirty = 0;
static int64_t g_last_led_tick_ms = 0;
static uint8_t g_led_last_rgb[24][3] = {{0}};
static int g_led_rgb_cache_valid = 0;
#ifndef LED_FX_NONE
#define LED_FX_NONE   0
#define LED_FX_PLAY   1
#define LED_FX_NEXT   2
#define LED_FX_PREV   3
#define LED_FX_SEARCH 4
#define LED_FX_ERROR  5
#endif
static int g_led_fx_enabled = 1;
static int g_led_fx_type = LED_FX_NONE;
static int64_t g_led_fx_queued_ms = 0;
static int64_t g_led_fx_start_ms = 0;
static int64_t g_led_fx_last_skip_ms = 0;
static int64_t g_led_fx_last_play_ms = 0;
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

    if (e < fl) {
        fl = (uint32_t)(((uint64_t)fl * 86 + (uint64_t)e * 14) / 100);
    } else {
        fl = (uint32_t)(((uint64_t)fl * 95 + (uint64_t)e * 5) / 100);
    }

    if (e > pk) {
        pk = e;
    } else {
        uint32_t dec = ((uint64_t)fl * 3 < (uint64_t)pk) ? 95 : 98;
        pk = (uint32_t)(((uint64_t)pk * dec + (uint64_t)fl * (100 - dec)) / 100);
    }

    g_band_floor[band_idx] = fl;
    g_band_peak[band_idx] = pk;

    if (e <= 384 || (uint64_t)e * 25 <= (uint64_t)total_rms) return 0;

    uint32_t span = (pk > fl) ? (pk - fl) : 0;
    uint32_t min_span = (uint32_t)(((uint64_t)pk * 30) / 100);
    if (span < min_span) span = min_span;
    if (span < 384) span = 384;

    uint32_t ex = (e > fl) ? (e - fl) : 0;
    uint32_t norm = (ex >= span) ? 255 : (uint32_t)(((uint64_t)ex * 255) / span);

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

            int64_t sfp = g_shm_is_32bit ? (((int64_t)s) / 256) : (((int64_t)s) * 256);

            lpf1a = (lpf1a * 31 + sfp) / 32;
            lpf1b = (lpf1b * 31 + lpf1a) / 32;
            int64_t b = lpf1b;

            lpf2a = (lpf2a * 7 + sfp) / 8;
            int64_t hp2 = sfp - lpf2a;
            lpf_hp2 = (lpf_hp2 * 7 + hp2) / 8;
            int64_t t = hp2 - lpf_hp2;

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
    /* g_led_ring_dirty is intentionally NOT cleared here: this runs whenever
     * Alice takes the LEDs for her own animation, while the last equalizer
     * frame is still lit in hardware. */
}

static void led_all_off(void) {
    led_reset_internal_state();
    g_led_ring_dirty = 0;
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
    /* Hand the ring back to the stock framebuffer trigger.
     * Writing brightness directly detaches the kernel trigger, so after the
     * equalizer lets go Alice's own animations never reach the LEDs again until
     * reboot: on cucumber that trigger is ledfb-ring, and maind renders its
     * animation into the framebuffer that only it reads. Upstream restored the
     * trigger for plum only, so a Midi loses Alice's ring indication. */
    if (g_led_hw.is_plum || g_led_hw.is_zero_indexed) {
        const char *trig = g_led_hw.is_plum ? "led_ring_fb\n" : "ledfb-ring\n";
        for (int z = start; z < end; z++) {
            char tpath[80];
            build_led_path(tpath, sizeof(tpath), "red", z);
            for (int i = 0; tpath[i]; i++) {
                if (tpath[i] == 'b' && tpath[i+1] == 'r') {
                    str_copy(tpath + i, "trigger", sizeof(tpath) - i);
                    break;
                }
            }
            write_sysfs_str(tpath, trig);
        }
    }
}

static void join_path(char *dst, unsigned long max, const char *a, const char *b);

static void set_led_visualizer_impl(int enable) {
    led_probe();
    g_led_enabled = enable ? 1 : 0;
    if (!g_led_enabled && g_led_fx_type == LED_FX_NONE) {
        if (g_led_hw.is_zero_indexed || !alice_owns_leds(now_ms())) {
            /* led_all_off() also reattaches the kernel trigger. On a Midi the
             * reset alone leaves Alice's animation with no path to the LEDs. */
            led_all_off();
        } else {
            led_reset_internal_state();
        }
    }
    const char *state_path = get_led_state_path();
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

static void set_led_fx_enabled_impl(int enable) {
    led_probe();
    g_led_fx_enabled = enable ? 1 : 0;
    if (!g_led_fx_enabled) {
        g_led_fx_type = LED_FX_NONE;
        g_led_fx_start_ms = 0;
        g_led_fx_queued_ms = 0;
        if (!g_led_enabled || playback_status != 1 || !spotify_device_is_active) {
            if (alice_owns_leds(now_ms())) {
                led_reset_internal_state();
            } else {
                led_all_off();
            }
        }
    }
    const char *req_dir = get_requests_dir();
    sys_mkdir(req_dir, 0700);
    char fx_state_path[360];
    join_path(fx_state_path, sizeof(fx_state_path), req_dir, "led_fx_state");
    int fd = sys_open(fx_state_path, 0100 | 01 | 01000 /* O_CREAT|O_WRONLY|O_TRUNC */, 0644);
    if (fd >= 0) {
        if (g_led_fx_enabled) {
            sys_write(fd, "on\n", 3);
        } else {
            sys_write(fd, "off\n", 4);
        }
        sys_close(fd);
    }
    log_msg(g_led_fx_enabled ? "[bridge] LED animations: ON\n" : "[bridge] LED animations: OFF\n");
}

static void parse_led_conf_buf(const char *buf) {
    unsigned long i = 0;
    while (buf[i]) {
        while (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n') i++;
        if (!buf[i]) break;
        if (buf[i] == '#') {
            while (buf[i] && buf[i] != '\n') i++;
            continue;
        }
        if (mem_equal(buf + i, "LED_VISUALIZER=", 15)) {
            i += 15;
            if (buf[i] == '"' || buf[i] == '\'') i++;
            if (mem_equal(buf + i, "off", 3) || buf[i] == '0') {
                g_led_enabled = 0;
            } else if (mem_equal(buf + i, "rainbow", 7)) {
                g_led_enabled = 1;
                g_led_color_mode = 1;
            } else if (mem_equal(buf + i, "cyan", 4)) {
                g_led_enabled = 1;
                g_led_color_mode = 2;
            } else if (mem_equal(buf + i, "green", 5)) {
                g_led_enabled = 1;
                g_led_color_mode = 4;
            } else if (mem_equal(buf + i, "white", 5) ||
                       mem_equal(buf + i, "on", 2) ||
                       buf[i] == '1') {
                g_led_enabled = 1;
                g_led_color_mode = 0;
            }
        } else if (mem_equal(buf + i, "LED_ANIMATIONS=", 15) ||
                   mem_equal(buf + i, "LED_FX=", 7)) {
            i += mem_equal(buf + i, "LED_ANIMATIONS=", 15) ? 15 : 7;
            if (buf[i] == '"' || buf[i] == '\'') i++;
            if (mem_equal(buf + i, "off", 3) || buf[i] == '0') {
                g_led_fx_enabled = 0;
            } else if (mem_equal(buf + i, "on", 2) || buf[i] == '1') {
                g_led_fx_enabled = 1;
            }
        }
        while (buf[i] && buf[i] != '\n') i++;
    }
}

static void read_led_config(void) {
    const char *req_dir = get_requests_dir();
    const char *state_path = get_led_state_path();
    char fx_state_path[360];
    join_path(fx_state_path, sizeof(fx_state_path), req_dir, "led_fx_state");
    sys_mkdir(req_dir, 0700);

    char marker_path[360];
    join_path(marker_path, sizeof(marker_path), req_dir, ".led_v1312_default");
    int mfd = sys_open(marker_path, 0, 0);
    if (mfd < 0) {
        int cfd = sys_open(marker_path, 0100 | 01 | 01000 /* O_CREAT|O_WRONLY|O_TRUNC */, 0644);
        if (cfd >= 0) {
            sys_write(cfd, "1\n", 2);
            sys_close(cfd);
        }
        sys_unlink(state_path);
        sys_unlink(fx_state_path);
        g_led_enabled = 1;
        g_led_color_mode = 0;
        g_led_fx_enabled = 1;
        return;
    }
    sys_close(mfd);

    g_led_enabled = 1;
    g_led_color_mode = 0;
    g_led_fx_enabled = 1;

    const char *conf_path = get_conf_path();
    int fd = sys_open(conf_path, 0, 0);
    if (fd >= 0) {
        char buf[1024];
        long n = sys_read(fd, buf, sizeof(buf) - 1);
        sys_close(fd);
        if (n > 0) {
            buf[n] = 0;
            parse_led_conf_buf(buf);
        }
    }

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

    int fsfd = sys_open(fx_state_path, 0, 0);
    if (fsfd >= 0) {
        char fsbuf[32];
        long fsn = sys_read(fsfd, fsbuf, sizeof(fsbuf) - 1);
        sys_close(fsfd);
        if (fsn > 0) {
            fsbuf[fsn] = 0;
            if (str_contains(fsbuf, "on")) g_led_fx_enabled = 1;
            else if (str_contains(fsbuf, "off")) g_led_fx_enabled = 0;
        }
    }
}

static void check_led_request(void) {
    const char *path = get_led_req_path();
    int fd = sys_open(path, 0, 0);
    if (fd >= 0) {
        char cmd[16];
        long n = sys_read(fd, cmd, sizeof(cmd) - 1);
        sys_close(fd);
        sys_unlink(path);

        if (n > 0) {
            if (cmd[0] == 'f' && n >= 2) {
                if (cmd[1] == '0' || (n >= 3 && cmd[1] == 'o' && cmd[2] == 'f')) {
                    set_led_fx_enabled_impl(0);
                } else if (cmd[1] == '1' || (n >= 3 && cmd[1] == 'o' && cmd[2] == 'n')) {
                    set_led_fx_enabled_impl(1);
                }
            } else if (cmd[0] == '1' || (cmd[0] == 'o' && cmd[1] == 'n')) {
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
            } else if (cmd[0] == 'g') {
                g_led_color_mode = 4;
                set_led_visualizer(1);
            }
        }
    }

    char fx_path[360];
    join_path(fx_path, sizeof(fx_path), get_requests_dir(), "led_fx");
    int ffd = sys_open(fx_path, 0, 0);
    if (ffd >= 0) {
        char fx_cmd[16];
        long fn = sys_read(ffd, fx_cmd, sizeof(fx_cmd) - 1);
        sys_close(ffd);
        sys_unlink(fx_path);

        if (fn > 0) {
            if (fx_cmd[0] == 'e') {
                led_fx_trigger(LED_FX_ERROR);
            } else if (fx_cmd[0] == 'p') {
                int64_t fnow = now_ms();
                int cur_fx = led_fx_active_type();
                if (cur_fx != LED_FX_NEXT && cur_fx != LED_FX_PREV &&
                    !led_fx_is_skip_recent(fnow) && !led_fx_is_play_recent(fnow)) {
                    led_fx_trigger(LED_FX_PLAY);
                }
            } else if (fx_cmd[0] == 's') {
                led_fx_trigger(LED_FX_SEARCH);
            } else if (fx_cmd[0] == 'n') {
                led_fx_trigger(LED_FX_NEXT);
            } else if (fx_cmd[0] == 'b') {
                led_fx_trigger(LED_FX_PREV);
            } else if (fx_cmd[0] == 'c') {
                led_fx_clear();
            }
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

static void led_fx_trigger_impl(int fx_type) {
    if (!g_led_fx_enabled) return;
    int64_t fnow = now_ms();
    g_led_fx_type = fx_type;
    g_led_fx_queued_ms = fnow;
    g_led_fx_start_ms = 0;
    if (fx_type == LED_FX_NEXT || fx_type == LED_FX_PREV) {
        g_led_fx_last_skip_ms = fnow;
    } else if (fx_type == LED_FX_PLAY) {
        g_led_fx_last_play_ms = fnow;
    }
}

static void led_fx_clear_impl(void) {
    g_led_fx_type = LED_FX_NONE;
    g_led_fx_start_ms = 0;
    g_led_fx_queued_ms = 0;
}

static int led_fx_is_skip_recent_impl(int64_t now) {
    return g_led_fx_last_skip_ms > 0 && now >= g_led_fx_last_skip_ms &&
           (now - g_led_fx_last_skip_ms) < 2500;
}

static int led_fx_is_play_recent_impl(int64_t now) {
    return g_led_fx_last_play_ms > 0 && now >= g_led_fx_last_play_ms &&
           (now - g_led_fx_last_play_ms) < 900;
}

static int led_fx_active_type_impl(void) {
    return g_led_fx_type;
}

static void led_fx_spotify_rgb(uint32_t v, int *r, int *g, int *b) {
    *r = 0;
    *g = (int)v;
    *b = (int)((v * 28) / 255);
}

static void led_fx_error_rgb(uint32_t v, int *r, int *g, int *b) {
    *r = (int)v;
    *g = 0;
    *b = 0;
}

static uint32_t led_sin256(uint32_t a) {
    uint32_t idx = (a >> 2) & 63;
    uint32_t frac = a & 3;
    uint32_t s0 = sin64[idx];
    uint32_t s1 = sin64[(idx + 1) & 63];
    return (s0 * (4 - frac) + s1 * frac) / 4;
}

static uint32_t led_gamma(uint32_t x) {
    uint32_t sq = (x * x) / 255;
    uint32_t cb = (sq * x) / 255;
    return (sq + cb * 2) / 3;
}

static uint32_t led_comet_level(uint32_t dist_behind, uint32_t hw_trail, uint32_t hw_lead) {
    if (dist_behind <= hw_trail) {
        uint32_t u = (dist_behind * 128) / hw_trail;
        uint32_t s = led_sin256(64 + u);
        return led_gamma((s > 1) ? (s - 1) : 0);
    }
    uint32_t d_ahead = 256 - dist_behind;
    if (d_ahead <= hw_lead) {
        uint32_t u = (d_ahead * 128) / hw_lead;
        uint32_t s = led_sin256(64 + u);
        return led_gamma((s > 1) ? (s - 1) : 0);
    }
    return 0;
}

static int led_fx_render(int64_t now) {
    int n_zones = g_led_hw.num_zones;
    if (n_zones <= 0 || n_zones > 24) return 0;
    int start = g_led_hw.is_zero_indexed ? 0 : 1;

    uint32_t peak = (g_led_brightness <= 0) ? 0 : ((uint32_t)g_led_brightness + 75);
    if (peak > 255) peak = 255;

    uint32_t hw_trail = (n_zones <= 3) ? 94 : 76;
    uint32_t hw_lead = (n_zones <= 3) ? 70 : 52;

    int64_t elapsed = now - g_led_fx_start_ms;
    if (elapsed < 0) elapsed = 0;

    if (g_led_fx_type == LED_FX_PLAY) {
        if (elapsed >= 720) return 0;
        for (int i = 0; i < n_zones; i++) {
            int z = start + i;
            int64_t ze = elapsed - (((int64_t)((n_zones - i) % n_zones) * 45) / n_zones);
            uint32_t lvl = 0;
            if (ze <= 0) {
                lvl = 0;
            } else if (ze < 140) {
                uint32_t s = led_sin256((192 + (uint32_t)((ze * 128) / 140)) & 255);
                lvl = led_gamma((s > 1) ? (s - 1) : 0);
            } else if (ze < 280) {
                lvl = 255 - (uint32_t)(((ze - 140) * 15) / 140);
            } else if (ze < 675) {
                uint32_t lin = ((uint32_t)(675 - ze) * 255) / 395;
                lvl = (240 * led_gamma(lin)) / 255;
            }
            uint32_t v = (peak * lvl) / 255;
            int r = 0, g = 0, b = 0;
            led_fx_spotify_rgb(v, &r, &g, &b);
            write_zone_rgb_if_changed(i, z, r, g, b);
        }
        return 1;
    }

    if (g_led_fx_type == LED_FX_NEXT || g_led_fx_type == LED_FX_PREV) {
        if (elapsed >= 720) return 0;
        uint32_t step = (uint32_t)((elapsed * 256) / 720);
        uint32_t head = (g_led_fx_type == LED_FX_NEXT) ? ((256 - (step & 255)) & 255) : (step & 255);
        uint32_t env = 255;
        if (elapsed == 0) {
            env = 32;
        } else if (elapsed < 90) {
            env = 32 + (uint32_t)((elapsed * 223) / 90);
        } else if (elapsed >= 540) {
            env = led_gamma((uint32_t)(((720 - elapsed) * 255) / 180));
        }
        for (int i = 0; i < n_zones; i++) {
            int z = start + i;
            uint32_t zpos = ((uint32_t)i * 256) / (uint32_t)n_zones;
            uint32_t dist_behind = (g_led_fx_type == LED_FX_NEXT)
                                   ? ((zpos - head + 256) & 255)
                                   : ((head - zpos + 256) & 255);
            uint32_t shape = led_comet_level(dist_behind, hw_trail, hw_lead);
            uint32_t lvl = (shape * env) / 255;
            uint32_t v = (peak * lvl) / 255;
            int r = 0, g = 0, b = 0;
            led_fx_spotify_rgb(v, &r, &g, &b);
            write_zone_rgb_if_changed(i, z, r, g, b);
        }
        return 1;
    }

    if (g_led_fx_type == LED_FX_SEARCH) {
        if (elapsed >= 10000) return 0;
        int64_t period = 960;
        uint32_t step = (uint32_t)(((elapsed % period) * 256) / period) & 255;
        uint32_t head = (256 - step) & 255;
        uint32_t env = 255;
        if (elapsed == 0) {
            env = 32;
        } else if (elapsed < 120) {
            env = 32 + (uint32_t)((elapsed * 223) / 120);
        } else if (elapsed >= 9700) {
            env = led_gamma((uint32_t)(((10000 - elapsed) * 255) / 300));
        }
        for (int i = 0; i < n_zones; i++) {
            int z = start + i;
            uint32_t zpos = ((uint32_t)i * 256) / (uint32_t)n_zones;
            uint32_t dist_behind = (zpos - head + 256) & 255;
            uint32_t shape = led_comet_level(dist_behind, hw_trail, hw_lead);
            uint32_t lvl = (shape * env) / 255;
            uint32_t v = (peak * lvl) / 255;
            int r = 0, g = 0, b = 0;
            led_fx_spotify_rgb(v, &r, &g, &b);
            write_zone_rgb_if_changed(i, z, r, g, b);
        }
        return 1;
    }

    if (g_led_fx_type == LED_FX_ERROR) {
        if (elapsed >= 780) return 0;
        uint32_t lvl = 0;
        if (elapsed < 320) {
            if (elapsed == 0) {
                lvl = 40;
            } else {
                uint32_t u = (uint32_t)((elapsed * 256) / 320);
                uint32_t s = led_sin256((u + 192) & 255);
                lvl = led_gamma((s > 1) ? (s - 1) : 0);
            }
        } else if (elapsed < 440) {
            lvl = 0;
        } else {
            uint32_t u = (uint32_t)(((elapsed - 440) * 256) / 340);
            uint32_t s = led_sin256((u + 192) & 255);
            lvl = led_gamma((s > 1) ? (s - 1) : 0);
        }
        uint32_t v = (peak * lvl) / 255;
        int r = 0, g = 0, b = 0;
        led_fx_error_rgb(v, &r, &g, &b);
        for (int i = 0; i < n_zones; i++) {
            write_zone_rgb_if_changed(i, start + i, r, g, b);
        }
        return 1;
    }

    return 0;
}

static void led_visualizer_tick(int64_t now) {
    if (!g_led_hw.detected) return;

    // Protect Alice listening/thinking/speaking LED animations from overwrite or zero-clearing
    if (alice_owns_leds(now)) {
        if (g_led_fx_type != LED_FX_NONE && g_led_fx_type != LED_FX_SEARCH && g_led_fx_start_ms != 0) {
            g_led_fx_type = LED_FX_NONE;
            g_led_fx_start_ms = 0;
            g_led_fx_queued_ms = 0;
        }
        /* On a Midi the equalizer detaches ledfb-ring the first time it writes
         * brightness, so handing the ring to Alice means clearing the frame and
         * reattaching the trigger -- otherwise maind animates into a
         * framebuffer nobody reads and the ring stays dark. Other models keep
         * upstream's hands-off policy (their trigger handling differs). */
        if (g_led_hw.is_zero_indexed) {
            if (g_led_last_active || g_led_ring_dirty || g_audio_shm_ptr) {
                led_all_off();
            }
        } else if (g_led_last_active || g_audio_shm_ptr) {
            led_reset_internal_state();
        }
        return;
    }

    if (!g_led_fx_enabled) {
        g_led_fx_type = LED_FX_NONE;
        g_led_fx_start_ms = 0;
        g_led_fx_queued_ms = 0;
    } else if (g_led_fx_type != LED_FX_NONE) {
        if (g_led_fx_start_ms == 0) {
            int64_t wait_limit = (g_led_fx_type == LED_FX_SEARCH) ? 10000 : 2500;
            if (g_led_fx_queued_ms > 0 && now >= g_led_fx_queued_ms && (now - g_led_fx_queued_ms) > wait_limit) {
                g_led_fx_type = LED_FX_NONE;
                g_led_fx_queued_ms = 0;
            } else {
                g_led_fx_start_ms = now;
                g_last_led_tick_ms = 0;
            }
        }
        if (g_led_fx_type != LED_FX_NONE) {
            if (g_last_led_tick_ms != 0 && (now - g_last_led_tick_ms) < 16) {
                return;
            }
            g_last_led_tick_ms = now;
            if (led_fx_render(now)) {
                g_led_last_active = 1;
                g_led_ring_dirty = 1;
                g_led_rgb_cache_valid = 1;
                return;
            }
            g_led_fx_type = LED_FX_NONE;
            g_led_fx_start_ms = 0;
            g_led_fx_queued_ms = 0;
            for (int i = 0; i < 24; i++) g_prev_zone_val[i] = 0;
            g_last_led_tick_ms = 0;
        }
    }

    if (!g_led_enabled || playback_status != 1 || !spotify_device_is_active) {
        if (g_led_last_active || g_led_ring_dirty) {
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
    g_led_ring_dirty = 1;
    g_led_frame++;

    audio_shm_ensure_attached();

    uint32_t bass_energy = 0;
    uint32_t mid_energy = 0;
    uint32_t treble_energy = 0;

    if (g_audio_shm_ptr) {
        analyze_shm_bands(g_audio_shm_ptr, 7680, &bass_energy, &mid_energy, &treble_energy);
    } else {
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

        uint32_t prev = g_prev_zone_val[i];
        if (target_energy > prev) {
            g_prev_zone_val[i] = (prev * 50 + target_energy * 50) / 100;
        } else {
            g_prev_zone_val[i] = (prev * 79 + target_energy * 21) / 100;
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
            led_fx_spotify_rgb(val, &r, &g, &b);
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

#endif
