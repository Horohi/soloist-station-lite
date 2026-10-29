/*
 * Universal ALSA Rate Converter Shim for 64-bit processes on 32-bit rootfs
 * (Supports Yandex Station Mini 1, Station Midi cucumber, Station Lite 2 plum).
 *
 * Aliases ALSA external rate converter plugin functions (_snd_pcm_rate_samplerate_* and
 * _snd_pcm_rate_speexrate_*) to ALSA's built-in linear rate converter
 * (_snd_pcm_rate_linear_open) in libasound.so.2.
 * Freestanding: requires no external headers or libc.
 */

typedef void snd_pcm_rate_ops_t;

extern int _snd_pcm_rate_linear_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp);

/* samplerate symbols (Station Mini 1 / YNDX-0004) */
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

/* speexrate symbols (Station Midi / cucumber, Lite 2 / plum) */
int _snd_pcm_rate_speexrate_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}

int _snd_pcm_rate_speexrate_best_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}

int _snd_pcm_rate_speexrate_medium_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}

int _snd_pcm_rate_speexrate_order_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}

int _snd_pcm_rate_speexrate_linear_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *opsp) {
    return _snd_pcm_rate_linear_open(version, objp, opsp);
}
