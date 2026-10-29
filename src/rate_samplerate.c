/*
 * ALSA Rate Converter Shim for 64-bit processes on 32-bit rootfs (e.g. Yandex Station Mini 1 / YNDX-0004).
 *
 * Aliases ALSA external rate converter plugin functions (_snd_pcm_rate_samplerate_*)
 * to ALSA's built-in linear rate converter (_snd_pcm_rate_linear_open) in libasound.so.2.
 * Freestanding: requires no external headers or libc.
 */

typedef void snd_pcm_rate_ops_t;

extern int _snd_pcm_rate_linear_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp);

int _snd_pcm_rate_samplerate_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}

int _snd_pcm_rate_samplerate_best_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}

int _snd_pcm_rate_samplerate_medium_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}

int _snd_pcm_rate_samplerate_order_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}

int _snd_pcm_rate_samplerate_linear_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}
