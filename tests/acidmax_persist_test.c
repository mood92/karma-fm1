/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID MAX: the engine's own bytes (track_t.ax) through a project (project.c proj_ax_put / proj_ax_get: the hidden drum
 * track's step area), a preset load, and an engine change. Build with -DMM_ACID=1 (as the MAIN MENU ACID firmware).
 *   cc -O1 -w -DMM_ACID=1 -Ibuild/gen -Ifirmware/src -o build/host/acidmax_persist_test tests/acidmax_persist_test.c \
 *      build/host/libplx.a -lstdc++ -lm */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? ENGI_ACIDMAX : 0u; }
#include "../firmware/src/project.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(void)
{
    static project_t q;
    uint32_t k, i;
    int ok = 1;
    const engine_t *e = &ENG_ACIDMAX;
    check("ENG_ACIDMAX has 28 own parameters in 7 titled pages", e->nax == 28 && e->ax && e->ax_title && e->nax % 4 == 0);
    for (k = 0; k < NPART; k++) {
        trk[k].eng_req = ENGI_ACIDMAX;
        for (i = 0; i < AXN; i++)
            trk[k].ax[i] = i < e->nax ? (int16_t)clamp((int32_t)(k * 17u + i * 3u) % 100 - 10, e->ax[i].min, e->ax[i].max) : 0;
    }
    proj_capture(&q);
    check("the project object is checksummed and fits", proj_ok(&q));
    {
        int16_t keep[NPART][AXN];
        for (k = 0; k < NPART; k++)
            for (i = 0; i < AXN; i++) {
                keep[k][i] = trk[k].ax[i];
                trk[k].ax[i] = 99;
            }
        proj_apply(&q, 1);
        for (k = 0; k < NPART; k++)
            for (i = 0; i < AXN; i++)
                if (trk[k].ax[i] != keep[k][i])
                    ok = 0;
        check("capture -> apply restores every ax byte of the three tracks", ok);
    }
    memset(&q.t[TRK_DRUM].step, 0, sizeof q.t[TRK_DRUM].step);
    q.sum = proj_sum(&q);
    proj_apply(&q, 1);
    ok = 1;
    for (k = 0; k < NPART; k++)
        for (i = 0; i < e->nax; i++)
            if (trk[k].ax[i] != e->ax[i].def)
                ok = 0;
    check("a project without the mark gives the descriptors' defaults", ok);
    printf(bad ? "acidmax persist: FAIL\n" : "acidmax persist: ok\n");
    return bad != 0;
}
