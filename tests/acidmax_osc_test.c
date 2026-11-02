/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID MAX oscillator shapes (acid303.c, WAVE 2..9) through the 303 voice: every shape at five POSITIONs, MOTION off
 * and on. Checks no NaN / Inf, a peak that does not clip, no silent shape, a DC offset that stays small, and that
 * WAVE 0 and 1 (the 303's own saw and square) are bit-identical to the voice before the shapes were added (their
 * hashes are in tests/golden.txt via regress.c; here they just have to render). Writes build/host/acidmax_shapes.wav
 * (every shape, a second each) for a listen.
 *   cc -O1 -ffp-contract=off -Ifirmware/src -o build/host/acidmax_osc_test tests/acidmax_osc_test.c -lm */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "acid303.c"

static const char *const NAMES[] = {"SAW", "SQR", "MORPH", "FOLD", "FORMANT", "WT CLASSIC", "WT SYNC", "WT VOCAL", "WT FM",
                                    "WT DIGITAL"};

typedef struct { double peak, rms, dc; int bad; } stat_t;

static void render(bass303_t *b, float *y, int n, int note)
{
    int i;
    bass303_note_on(b, note, 0, 0);
    b->osc_freq = 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
    for (i = 0; i < n; i += 128) {
        int m = n - i < 128 ? n - i : 128;
        if (i == (n / 128 / 2) * 128)
            bass303_note_off(b);
        bass303_render(b, y + i, m);
    }
}

static stat_t measure(const float *y, int n)
{
    stat_t s = {0, 0, 0, 0};
    int i;
    double sum = 0, sq = 0;
    for (i = 0; i < n; i++) {
        double v = y[i];
        if (!(v == v) || fabs(v) > 1e6)
            s.bad++;
        if (fabs(v) > s.peak)
            s.peak = fabs(v);
        sum += v;
        sq += v * v;
    }
    s.dc = sum / n;
    s.rms = sqrt(sq / n);
    return s;
}

int main(void)
{
    static bass303_t b;
    static float y[44100], all[44100 * 10];
    FILE *f;
    int w, p, fails = 0;
    double avg[10];
    for (w = 0; w < 10; w++) {
        int motion;
        avg[w] = 0;
        for (motion = 0; motion < 2; motion++) {
            for (p = 0; p < 5; p++) {
                stat_t s;
                memset(&b, 0, sizeof b);
                bass303_init(&b);
                bass303_set(&b, BASS303_WAVE, w);
                bass303_set(&b, BASS303_POS, p * 127 / 4);
                bass303_set(&b, BASS303_MOTION, motion ? 100 : 0);
                bass303_set(&b, BASS303_CUTOFF, 100);          /* open, so the shape itself is heard */
                bass303_set(&b, BASS303_RESO, 30);
                bass303_set(&b, BASS303_ENVMOD, 60);
                render(&b, y, 44100, 45);                       /* A2 */
                s = measure(y, 44100);
                if (s.bad || s.peak < 0.01 || s.peak > 2.0 || fabs(s.dc) > 0.05) {
                    printf("FAIL %-11s pos %d motion %d: peak %.3f rms %.4f dc %.4f bad %d\n", NAMES[w], p * 127 / 4, motion,
                           s.peak, s.rms, s.dc, s.bad);
                    fails++;
                } else if (motion == 0 && (p == 0 || p == 2 || p == 4)) {
                    printf("ok   %-11s pos %3d: peak %.3f rms %.4f dc %+.4f\n", NAMES[w], p * 127 / 4, s.peak, s.rms, s.dc);
                }
                if (!motion)
                    avg[w] += s.rms / 5.0;
                if (!motion && p == 2)
                    memcpy(all + 44100 * w, y, sizeof y);
            }
        }
    }
    f = fopen("build/host/acidmax_shapes.wav", "wb");
    if (f) {
        uint32_t n = 44100u * 10u, db = n * 2u, sr = 44100u, br = sr * 2u, v;
        uint16_t u, ch = 1, bits = 16, al = 2;
        int16_t *pcm = malloc(db);
        float pk = 0;
        uint32_t i;
        for (i = 0; i < n; i++)
            if (fabsf(all[i]) > pk)
                pk = fabsf(all[i]);
        for (i = 0; i < n; i++)
            pcm[i] = (int16_t)(all[i] / (pk > 0 ? pk : 1) * 30000.0f);
        fwrite("RIFF", 1, 4, f); v = 36 + db; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
        u = 1; fwrite(&u, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&al, 2, 1, f);
        fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&db, 4, 1, f); fwrite(pcm, 2, n, f);
        fclose(f);
        free(pcm);
    }
    if (getenv("CAL"))
        for (w = 0; w < 10; w++)
            printf("cal %-11s avg rms %.4f  gain to the saw %.2f\n", NAMES[w], avg[w], avg[0] / avg[w]);
    printf(fails ? "acidmax osc: %d FAIL\n" : "acidmax osc: 10 shapes x 5 positions x motion on / off ok\n", fails);
    return fails ? 1 : 0;
}
