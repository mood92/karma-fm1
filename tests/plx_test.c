/* SPDX-License-Identifier: GPL-3.0-only */
/* PLX (firmware/mi/plx.cc: Plaits' models): each model in exactly plx_size bytes (nothing written past them),
 * a note triggered and held 1 s then let go: it sounds, stays finite and bounded; its peak and level.
 *   sh tools/plx_host.sh && cc -O2 -Ifirmware/mi tests/plx_test.c build/host/libplx.a -lc++ -lm */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plx.h"

static const char *const NAME[PLX_COUNT] = {"VA", "VCF", "PD", "FM", "6OP A", "6OP B", "6OP C", "GRAIN", "TERRAIN",
                                            "STRMACH", "CHIP", "STRING", "MODAL", "NOISE", "SPEECH", "PARTICLE", "BD",
                                            "SD", "HH"};
int main(void)
{
    int m, fails = 0;
    for (m = 0; m < PLX_COUNT; m++) {
        unsigned n = plx_size(m), i, k;
        unsigned char *mem = malloc(n + 4096);
        double e = 0, peak = 0;
        int bad = 0, guard = 0;
        memset(mem, 0xA5, n + 4096);
        if (plx_init(m, mem, n)) {
            printf("plx: %-8s init FAIL\n", NAME[m]);
            fails++;
            continue;
        }
        for (i = 0; i < 4096u; i++)
            guard += mem[n + i] != 0xA5;
        for (k = 0; k < 44100u * 2u / 16u; k++) {
            plx_params_t p = {k == 0 ? PLX_TRIG_RISE | PLX_TRIG_HIGH : k < 44100u / 16u ? PLX_TRIG_HIGH : PLX_TRIG_LOW,
                              48.0f, 0.5f, 0.5f, 0.5f, 0.8f};
            float o[16], a[16];
            plx_render(mem, &p, o, a, 16);
            for (i = 0; i < 16u; i++) {
                if (!isfinite(o[i]) || fabsf(o[i]) > 4.0f)
                    bad++;
                if (fabsf(o[i]) > peak)
                    peak = fabsf(o[i]);
                e += o[i] * o[i];
            }
        }
        for (i = 0; i < 4096u; i++)
            guard += mem[n + i] != 0xA5;
        {
            int ok = !bad && !guard && peak > 0.01;
            printf("plx: %-8s %6u B  peak %.2f rms %.3f %s\n", NAME[m], n, peak, sqrt(e / (44100.0 * 2)), ok ? "ok" : "FAIL");
            fails += !ok;
        }
        free(mem);
    }
    printf(fails ? "plx: FAILED\n" : "plx: all models ok\n");
    return fails != 0;
}
