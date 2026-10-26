#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"
#define MINIMP3_EXT_IMPLEMENTATION
#include "minimp3/minimp3_ex.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static void write_wav_header(FILE *out, int sample_rate, int channels, int num_samples) {
    int data_bytes = num_samples * sizeof(int16_t);
    int total_bytes = 36 + data_bytes;
    uint8_t h[44] = {
        'R','I','F','F',
        (uint8_t)(total_bytes & 0xFF), (uint8_t)((total_bytes >> 8) & 0xFF), (uint8_t)((total_bytes >> 16) & 0xFF), (uint8_t)((total_bytes >> 24) & 0xFF),
        'W','A','V','E',
        'f','m','t',' ',
        16, 0, 0, 0,
        1, 0,
        (uint8_t)channels, 0,
        (uint8_t)(sample_rate & 0xFF), (uint8_t)((sample_rate >> 8) & 0xFF), (uint8_t)((sample_rate >> 16) & 0xFF), (uint8_t)((sample_rate >> 24) & 0xFF),
        (uint8_t)((sample_rate * channels * 2) & 0xFF), (uint8_t)(((sample_rate * channels * 2) >> 8) & 0xFF), (uint8_t)(((sample_rate * channels * 2) >> 16) & 0xFF), (uint8_t)(((sample_rate * channels * 2) >> 24) & 0xFF),
        (uint8_t)(channels * 2), 0,
        16, 0,
        'd','a','t','a',
        (uint8_t)(data_bytes & 0xFF), (uint8_t)((data_bytes >> 8) & 0xFF), (uint8_t)((data_bytes >> 16) & 0xFF), (uint8_t)((data_bytes >> 24) & 0xFF)
    };
    fwrite(h, 1, 44, out);
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    mp3dec_t mp3d;
    mp3dec_file_info_t info;
    if (mp3dec_load(&mp3d, argv[1], &info, NULL, NULL) != 0) {
        return 1;
    }
    FILE *out = stdout;
    if (argc >= 3) {
        out = fopen(argv[2], "wb");
        if (!out) return 1;
    }
    write_wav_header(out, info.hz, info.channels, info.samples);
    fwrite(info.buffer, sizeof(mp3d_sample_t), info.samples, out);
    if (out != stdout) fclose(out);
    free(info.buffer);
    return 0;
}
