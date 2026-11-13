/*
 * LD_PRELOAD ALSA PCM ducking shim for Soloist / apulse.
 *
 * Intercepts apulse's PLT calls to libasound (snd_pcm_writei + hw_params)
 * right at the ALSA period boundary (~20ms latency), smoothly attenuating
 * Spotify PCM down to 20% while Alice is listening/thinking/speaking without
 * modifying hardware Master mixer levels.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "duck_shm.h"

/* ALSA snd_pcm_format_t numeric IDs */
#define ALSA_FMT_S16_LE 2
#define ALSA_FMT_S32_LE 10
#define ALSA_FMT_FLOAT_LE 14

#define DUCK_LEVEL 0.20f
#define DUCK_ATTACK_SEC 0.08f
#define DUCK_RELEASE_SEC 0.30f
#define MAX_TRACKED_PCMS 16

struct duck_pcm_entry {
    void *pcm;
    int format;
    uint32_t rate;
    uint32_t channels;
    float gain;
};

typedef int (*fn_set_format)(void *, void *, int);
typedef int (*fn_set_channels)(void *, void *, unsigned int);
typedef int (*fn_set_rate)(void *, void *, unsigned int, int);
typedef int (*fn_set_rate_near)(void *, void *, unsigned int *, int *);
typedef long (*fn_writei)(void *, const void *, unsigned long);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static fn_set_format g_orig_set_format = NULL;
static fn_set_channels g_orig_set_channels = NULL;
static fn_set_rate g_orig_set_rate = NULL;
static fn_set_rate_near g_orig_set_rate_near = NULL;
static fn_writei g_orig_writei = NULL;

static const struct soloist_duck_shm *g_shm = NULL;
static int64_t g_last_open_try_ms = -1000;

static struct duck_pcm_entry g_pcms[MAX_TRACKED_PCMS];
static unsigned g_next_slot = 0;
static uint8_t *g_scratch = NULL;
static size_t g_scratch_cap = 0;

static int64_t mono_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
    }
    return 0;
}

static void *resolve_next(const char *sym, const char *ver) {
    void *p = ver ? dlvsym(RTLD_NEXT, sym, ver) : NULL;
    return p ? p : dlsym(RTLD_NEXT, sym);
}

static void resolve_originals_locked(void) {
    if (!g_orig_set_format) g_orig_set_format = (fn_set_format)resolve_next("snd_pcm_hw_params_set_format", "ALSA_0.9");
    if (!g_orig_set_channels) g_orig_set_channels = (fn_set_channels)resolve_next("snd_pcm_hw_params_set_channels", "ALSA_0.9");
    if (!g_orig_set_rate) g_orig_set_rate = (fn_set_rate)resolve_next("snd_pcm_hw_params_set_rate", "ALSA_0.9");
    if (!g_orig_set_rate_near) g_orig_set_rate_near = (fn_set_rate_near)resolve_next("snd_pcm_hw_params_set_rate_near", "ALSA_0.9.0rc4");
    if (!g_orig_writei) g_orig_writei = (fn_writei)resolve_next("snd_pcm_writei", "ALSA_0.9");
}

static void ensure_shm_mapped_locked(int64_t now) {
    if (g_shm) return;
    if (now - g_last_open_try_ms < 200) return;
    g_last_open_try_ms = now;

    const char *path = getenv("SOLOIST_DUCK_PATH");
    if (!path || !path[0]) path = SOLOIST_DUCK_DEFAULT_PATH;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;

    struct stat st;
    if (fstat(fd, &st) == 0 && (size_t)st.st_size >= sizeof(struct soloist_duck_shm)) {
        void *p = mmap(NULL, SOLOIST_DUCK_SHM_SIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (p != MAP_FAILED) {
            g_shm = (const struct soloist_duck_shm *)p;
        }
    }
    close(fd);
}

static int is_ducking_busy_locked(int64_t now) {
    ensure_shm_mapped_locked(now);
    if (!g_shm) return 0;
    if (g_shm->magic != SOLOIST_DUCK_MAGIC) return 0;
    uint32_t act = __atomic_load_n(&g_shm->active, __ATOMIC_ACQUIRE);
    if (!act) return 0;
    int64_t until = __atomic_load_n(&g_shm->until_ms, __ATOMIC_ACQUIRE);
    return (now < until);
}

static struct duck_pcm_entry *get_or_alloc_pcm_locked(void *pcm) {
    if (!pcm) return NULL;
    for (unsigned i = 0; i < MAX_TRACKED_PCMS; i++) {
        if (g_pcms[i].pcm == pcm) return &g_pcms[i];
    }
    unsigned idx = g_next_slot++ % MAX_TRACKED_PCMS;
    g_pcms[idx].pcm = pcm;
    g_pcms[idx].format = ALSA_FMT_S16_LE;
    g_pcms[idx].rate = 48000;
    g_pcms[idx].channels = 2;
    g_pcms[idx].gain = 1.0f;
    return &g_pcms[idx];
}

static struct duck_pcm_entry *find_pcm_locked(void *pcm) {
    for (unsigned i = 0; i < MAX_TRACKED_PCMS; i++) {
        if (g_pcms[i].pcm == pcm) return &g_pcms[i];
    }
    return NULL;
}

static int32_t clamp_s16(int64_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int32_t)v;
}

static int32_t clamp_s32(int64_t v) {
    if (v > 2147483647LL) return 2147483647;
    if (v < -2147483648LL) return (int32_t)(-2147483648LL);
    return (int32_t)v;
}

static inline float step_duck_gain(float gain, float target, float atk_step, float rel_step) {
    if (gain > target) {
        gain -= atk_step;
        return (gain < target) ? target : gain;
    }
    if (gain < target) {
        gain += rel_step;
        return (gain > target) ? target : gain;
    }
    return gain;
}

static size_t format_sample_bytes(int fmt) {
    if (fmt == ALSA_FMT_S16_LE) return 2;
    if (fmt == ALSA_FMT_S32_LE || fmt == ALSA_FMT_FLOAT_LE) return 4;
    return 0;
}

static void apply_duck_gain(uint8_t *dst, const uint8_t *src, size_t frames,
                            struct duck_pcm_entry *st, int busy) {
    uint32_t channels = st->channels;
    uint32_t rate = st->rate;
    if (channels == 0 || rate == 0) return;

    float target = busy ? DUCK_LEVEL : 1.0f;
    float atk_step = (1.0f - DUCK_LEVEL) / (DUCK_ATTACK_SEC * (float)rate);
    float rel_step = (1.0f - DUCK_LEVEL) / (DUCK_RELEASE_SEC * (float)rate);
    float gain = st->gain;

    if (st->format == ALSA_FMT_FLOAT_LE) {
        const float *sf = (const float *)src;
        float *df = (float *)dst;
        for (size_t f = 0; f < frames; f++) {
            gain = step_duck_gain(gain, target, atk_step, rel_step);
            for (uint32_t c = 0; c < channels; c++) {
                size_t idx = f * channels + c;
                df[idx] = sf[idx] * gain;
            }
        }
    } else if (st->format == ALSA_FMT_S16_LE) {
        const int16_t *si = (const int16_t *)src;
        int16_t *di = (int16_t *)dst;
        for (size_t f = 0; f < frames; f++) {
            gain = step_duck_gain(gain, target, atk_step, rel_step);
            uint32_t q15 = (uint32_t)(gain * 32768.0f + 0.5f);
            for (uint32_t c = 0; c < channels; c++) {
                size_t idx = f * channels + c;
                di[idx] = (int16_t)clamp_s16(((int64_t)si[idx] * q15) >> 15);
            }
        }
    } else {
        const int32_t *si = (const int32_t *)src;
        int32_t *di = (int32_t *)dst;
        for (size_t f = 0; f < frames; f++) {
            gain = step_duck_gain(gain, target, atk_step, rel_step);
            uint32_t q15 = (uint32_t)(gain * 32768.0f + 0.5f);
            for (uint32_t c = 0; c < channels; c++) {
                size_t idx = f * channels + c;
                di[idx] = clamp_s32(((int64_t)si[idx] * q15) >> 15);
            }
        }
    }

    st->gain = (!busy && gain >= 0.9999f) ? 1.0f : gain;
}

int snd_pcm_hw_params_set_format(void *pcm, void *params, int format) {
    pthread_mutex_lock(&g_lock);
    resolve_originals_locked();
    fn_set_format orig = g_orig_set_format;
    pthread_mutex_unlock(&g_lock);
    if (!orig) return -1;
    int rc = orig(pcm, params, format);
    if (rc >= 0) {
        pthread_mutex_lock(&g_lock);
        struct duck_pcm_entry *st = get_or_alloc_pcm_locked(pcm);
        if (st) {
            st->format = format;
            st->gain = 1.0f;
        }
        pthread_mutex_unlock(&g_lock);
    }
    return rc;
}

int snd_pcm_hw_params_set_channels(void *pcm, void *params, unsigned int val) {
    pthread_mutex_lock(&g_lock);
    resolve_originals_locked();
    fn_set_channels orig = g_orig_set_channels;
    pthread_mutex_unlock(&g_lock);
    if (!orig) return -1;
    int rc = orig(pcm, params, val);
    if (rc >= 0 && val > 0) {
        pthread_mutex_lock(&g_lock);
        struct duck_pcm_entry *st = get_or_alloc_pcm_locked(pcm);
        if (st) st->channels = val;
        pthread_mutex_unlock(&g_lock);
    }
    return rc;
}

int snd_pcm_hw_params_set_rate(void *pcm, void *params, unsigned int val, int dir) {
    pthread_mutex_lock(&g_lock);
    resolve_originals_locked();
    fn_set_rate orig = g_orig_set_rate;
    pthread_mutex_unlock(&g_lock);
    if (!orig) return -1;
    int rc = orig(pcm, params, val, dir);
    if (rc >= 0 && val > 0) {
        pthread_mutex_lock(&g_lock);
        struct duck_pcm_entry *st = get_or_alloc_pcm_locked(pcm);
        if (st) st->rate = val;
        pthread_mutex_unlock(&g_lock);
    }
    return rc;
}

int snd_pcm_hw_params_set_rate_near(void *pcm, void *params, unsigned int *val, int *dir) {
    pthread_mutex_lock(&g_lock);
    resolve_originals_locked();
    fn_set_rate_near orig = g_orig_set_rate_near;
    pthread_mutex_unlock(&g_lock);
    if (!orig) return -1;
    int rc = orig(pcm, params, val, dir);
    if (rc >= 0 && val && *val > 0) {
        pthread_mutex_lock(&g_lock);
        struct duck_pcm_entry *st = get_or_alloc_pcm_locked(pcm);
        if (st) st->rate = *val;
        pthread_mutex_unlock(&g_lock);
    }
    return rc;
}

long snd_pcm_writei(void *pcm, const void *buffer, unsigned long size) {
    pthread_mutex_lock(&g_lock);
    resolve_originals_locked();
    fn_writei orig = g_orig_writei;
    if (!orig) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    int64_t now = mono_ms();
    int busy = is_ducking_busy_locked(now);
    struct duck_pcm_entry *st = find_pcm_locked(pcm);
    if (!st && pcm) st = get_or_alloc_pcm_locked(pcm);

    size_t sbytes = st ? format_sample_bytes(st->format) : 0;
    if (!buffer || size == 0 || !st || sbytes == 0 || st->channels == 0 || (!busy && st->gain >= 0.9999f)) {
        if (st && !busy) st->gain = 1.0f;
        pthread_mutex_unlock(&g_lock);
        return orig(pcm, buffer, size);
    }

    size_t nbytes = size * sbytes * st->channels;
    if (nbytes > g_scratch_cap) {
        uint8_t *grown = (uint8_t *)realloc(g_scratch, nbytes);
        if (!grown) {
            pthread_mutex_unlock(&g_lock);
            return orig(pcm, buffer, size);
        }
        g_scratch = grown;
        g_scratch_cap = nbytes;
    }

    float initial_gain = st->gain;
    apply_duck_gain(g_scratch, (const uint8_t *)buffer, size, st, busy);
    long rc = orig(pcm, g_scratch, size);
    if (rc < 0 || (unsigned long)rc < size) {
        st->gain = initial_gain;
        if (rc > 0) apply_duck_gain(g_scratch, (const uint8_t *)buffer, (size_t)rc, st, busy);
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}
