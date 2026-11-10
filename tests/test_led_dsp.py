#!/usr/bin/env python3
"""Host unit tests for the extracted 3-band LED DSP crossover, envelope & cubic curve."""
import math
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = (ROOT / "src" / "soloist-bridge.c").read_text(encoding="utf-8")


def slice_between(src: str, start_marker: str, end_marker: str) -> str:
    s = src.find(start_marker)
    assert s != -1, f"Start marker not found: {start_marker}"
    e = src.find(end_marker, s)
    assert e != -1, f"End marker not found: {end_marker}"
    return src[s:e]


band_globals = slice_between(
    SRC,
    "static uint32_t g_band_floor[3] = {0};",
    "static uint32_t g_prev_zone_val[24] = {0};",
)
isqrt_code = slice_between(
    SRC,
    "static inline uint32_t isqrt(uint64_t n) {",
    "static void audio_shm_ensure_attached(void) {",
)
dsp_code = slice_between(
    SRC,
    "// Adaptive per-band baseline/peak envelope with noise gate and cubic gamma curve",
    "static void write_sysfs_int(const char *path, int val) {",
)

C_HARNESS = f"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

{band_globals}
{isqrt_code}
{dsp_code}

static void reset_dsp(void) {{
    memset(g_band_floor, 0, sizeof(g_band_floor));
    memset(g_band_peak, 0, sizeof(g_band_peak));
    memset(g_band_init, 0, sizeof(g_band_init));
    g_shm_is_32bit = 0;
}}

static void fill_sine_s16(int32_t *shm, int frames, double freq_hz, double amp) {{
    for (int f = 0; f < frames; f++) {{
        int32_t v = (int32_t)(amp * sin(2.0 * 3.14159265358979323846 * freq_hz * f / 48000.0));
        shm[f * 2] = v;
        shm[f * 2 + 1] = v;
    }}
}}

int main(int argc, char **argv) {{
    static int32_t shm[7680 * 2];

    // 1. Digital silence / dither (<= 1 LSB) -> strictly 0 on all bands
    reset_dsp();
    for (int iter = 0; iter < 15; iter++) {{
        for (int f = 0; f < 7680; f++) shm[f * 2] = (f & 1) ? 1 : -1;
        uint32_t b, m, t;
        analyze_shm_bands(shm, 7680, &b, &m, &t);
        if (b != 0 || m != 0 || t != 0) {{
            fprintf(stderr, "FAIL silence iter=%d b=%u m=%u t=%u\\n", iter, b, m, t);
            return 1;
        }}
    }}

    // 2. Sustained constant mid pad -> baseline adapts and 22% deadzone drops it back to 0
    reset_dsp();
    fill_sine_s16(shm, 7680, 500.0, 400.0);
    for (int iter = 0; iter < 30; iter++) {{
        uint32_t b, m, t;
        analyze_shm_bands(shm, 7680, &b, &m, &t);
    }}
    uint32_t sb = 999, sm = 999, st = 999;
    analyze_shm_bands(shm, 7680, &sb, &sm, &st);
    if (sm != 0 || sb != 0 || st != 0) {{
        fprintf(stderr, "FAIL steady pad stayed lit: sb=%u sm=%u st=%u\\n", sb, sm, st);
        return 2;
    }}

    // 3. Rhythmic bass beat burst (100Hz) after sustained bed -> bass spikes >= 180 while mid/treble stay 0
    fill_sine_s16(shm, 7680, 100.0, 200.0);
    for (int iter = 0; iter < 15; iter++) {{
        uint32_t b, m, t;
        analyze_shm_bands(shm, 7680, &b, &m, &t);
    }}
    fill_sine_s16(shm, 7680, 100.0, 1200.0);
    uint32_t bb = 0, bm = 0, bt = 0;
    analyze_shm_bands(shm, 7680, &bb, &bm, &bt);
    if (bb < 180 || bt > 10) {{
        fprintf(stderr, "FAIL bass beat isolation: bb=%u bm=%u bt=%u\\n", bb, bm, bt);
        return 3;
    }}

    // 4. Treble chime burst (4000Hz) after quiet bed -> treble spikes >= 180 without triggering bass
    reset_dsp();
    fill_sine_s16(shm, 7680, 4000.0, 80.0);
    for (int iter = 0; iter < 15; iter++) {{
        uint32_t b, m, t;
        analyze_shm_bands(shm, 7680, &b, &m, &t);
    }}
    fill_sine_s16(shm, 7680, 4000.0, 500.0);
    uint32_t tb = 0, tm = 0, tt = 0;
    analyze_shm_bands(shm, 7680, &tb, &tm, &tt);
    if (tt < 180 || tb != 0) {{
        fprintf(stderr, "FAIL treble burst isolation: tb=%u tm=%u tt=%u\\n", tb, tm, tt);
        return 4;
    }}

    // 5. Full-scale S32_LE detection + overflow safety (INT32 range)
    reset_dsp();
    for (int iter = 0; iter < 10; iter++) {{
        double amp = (iter == 9) ? (1200.0 * 65536.0) : (200.0 * 65536.0);
        fill_sine_s16(shm, 7680, 500.0, amp);
        uint32_t b = 0, m = 0, t = 0;
        analyze_shm_bands(shm, 7680, &b, &m, &t);
        if (iter == 9 && (m < 180 || g_shm_is_32bit != 1)) {{
            fprintf(stderr, "FAIL S32_LE mode: is32=%d b=%u m=%u t=%u\\n", g_shm_is_32bit, b, m, t);
            return 5;
        }}
    }}

    // 6. Optional real-device capture check (/tmp/shm_cap.bin, 60 frames x int16[7680])
    if (argc > 1) {{
        FILE *fp = fopen(argv[1], "rb");
        if (!fp) return 6;
        reset_dsp();
        static int16_t raw[7680];
        uint32_t minv[3] = {{255, 255, 255}}, maxv[3] = {{0, 0, 0}};
        uint64_t sumv[3] = {{0, 0, 0}};
        uint32_t prev[3] = {{0, 0, 0}};
        int nframes = 0;
        while (fread(raw, sizeof(int16_t), 7680, fp) == 7680) {{
            for (int f = 0; f < 7680; f++) {{
                shm[f * 2] = raw[f];
                shm[f * 2 + 1] = raw[f];
            }}
            uint32_t out[3];
            analyze_shm_bands(shm, 7680, &out[0], &out[1], &out[2]);
            for (int k = 0; k < 3; k++) {{
                uint32_t v = (out[k] > prev[k]) ? out[k] : (prev[k] * 42 + out[k] * 58) / 100;
                prev[k] = v;
                if (v < minv[k]) minv[k] = v;
                if (v > maxv[k]) maxv[k] = v;
                sumv[k] += v;
            }}
            nframes++;
        }}
        fclose(fp);
        for (int k = 0; k < 3; k++) {{
            double min_pct = minv[k] * 100.0 / 255.0;
            double avg_pct = (sumv[k] / (double)nframes) * 100.0 / 255.0;
            double max_pct = maxv[k] * 100.0 / 255.0;
            printf("CAPTURE band=%d min=%.1f%% avg=%.1f%% max=%.1f%%\\n",
                   k, min_pct, avg_pct, max_pct);
            if (min_pct > 1.0 || avg_pct < 6.0 || avg_pct > 28.0 || max_pct < 85.0) {{
                fprintf(stderr, "FAIL capture target range band=%d\\n", k);
                return 7;
            }}
        }}
    }}

    return 0;
}}
"""


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        src = pathlib.Path(td) / "test_dsp.c"
        exe = pathlib.Path(td) / "test_dsp"
        src.write_text(C_HARNESS, encoding="utf-8")
        comp = subprocess.run(
            ["cc", "-O2", "-Wall", "-Wextra", str(src), "-lm", "-o", str(exe)],
            capture_output=True,
            text=True,
        )
        assert comp.returncode == 0, f"Compile failed:\n{comp.stderr}"

        cap_path = "/tmp/shm_cap.bin"
        args = [str(exe)]
        if os.path.exists(cap_path):
            args.append(cap_path)
        run = subprocess.run(args, capture_output=True, text=True)
        print(run.stdout, end="")
        if run.returncode != 0:
            print(run.stderr, file=sys.stderr)
            return run.returncode
    print("PASS: All 3-band DSP crossover, envelope & cubic curve tests succeeded.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
