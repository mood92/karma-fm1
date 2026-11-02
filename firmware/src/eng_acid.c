/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID: the TB-303 of X0X (acid303.c: Open303 with the Devilfish ranges, as X0X ported it) as a SLOOP engine for a
 * bass track. One note at a time, as the 303: a step's ACCENT (or a key played hard) is the 303's accent, a step's
 * SLIDE its slide (the next note glides in without a new attack). The pitch comes from the track (its note,
 * transpose, glide, bend); the 303's own envelopes shape the sound, so the presets hold the track's SUS at 127.
 *   WAVE   saw or the 303's square
 *   CUT    the cutoff (314 Hz .. 2.4 kHz), RES the resonance, ENV how far the envelope opens it, DEC its decay
 *   ACC    how much an accent adds
 *   DRV    a soft drive after it (0 none)
 *   SLIDE  how long a slide takes (2 .. 360 ms)
 * Float, on the AC79's FPU (tools/build.py). */
#include "acid303.c"

typedef struct {
    bass303_t b;
    uint8_t init, trig, acc, slide;
    int8_t pot[8];                                    /* the values last given to the 303 (-1: not yet) */
} acid_slot_t;

static const char *const N_ACID_WAVE[] = {"SAW", "SQR"};
static acid_slot_t *acid_slot(track_t *t)
{
    acid_slot_t *A;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    A = (acid_slot_t *)eng_arena_of(t, ENGI_ACID);
    if (!A->init) {                                   /* (a fresh arena is all 0) */
        uint32_t k;
        bass303_init(&A->b);
        for (k = 0; k < 8u; k++)
            A->pot[k] = -1;
        A->init = 1;
    }
    return A;
}

static void acid_note_on(track_t *t, voice_t *v)
{
    acid_slot_t *A = acid_slot(t);
    if (!A || v != &t->v[0])
        return;
    A->trig = 1;                                      /* (the 303 is told in the next render: it needs the pitch) */
    A->acc = v->vel >= 120u;
    A->slide = t->slide_glide && A->b.gate;           /* a step's SLIDE into this one, the last note still held */
}

static void acid_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint8_t POT[8] = {BASS303_WAVE, BASS303_CUTOFF, BASS303_RESO, BASS303_ENVMOD, BASS303_DECAY,
                                   BASS303_ACCENT, BASS303_DRIVE, BASS303_SLIDE};
    acid_slot_t *A = acid_slot(t);
    float y[CTL], f;
    uint32_t i, k;
    if (!A || v != &t->v[0])
        return;
    if (n > CTL)
        n = CTL;
    for (k = 0; k < 8u; k++) {                        /* the knobs, when they move */
        int32_t p = clamp(t->p[P_E0 + k], 0, k ? 127 : 1);
        if (p != A->pot[k]) {
            bass303_set(&A->b, POT[k], p);
            if (k == 6u)
                bass303_set(&A->b, BASS303_DRVTYPE, p ? BASS303_DRV_SOFT : BASS303_DRV_OFF);
            A->pot[k] = (int8_t)p;
        }
    }
    if (A->trig) {
        bass303_note_on(&A->b, (m->pitch16 + 8) >> 4, A->acc, A->slide);
        A->trig = 0;
    } else if (!v->gate && A->b.gate) {
        bass303_note_off(&A->b);
    }
    /* the track's pitch, finer than a note: transpose, glide, bend, LFO */
    f = A->b.tuning * fm_exp2f(((float)m->pitch16 * (1.0f / 16.0f) - 69.0f) * (1.0f / 12.0f));
    A->b.osc_freq = f;
    bass303_render(&A->b, y, (int)n);
    for (i = 0; i < n; i++) {
        float s = y[i] * 96000.0f;                    /* (the 303 peaks near 0.5 on an accent) */
        int32_t q = s > 60000.0f ? 60000 : s < -60000.0f ? -60000 : (int32_t)s;
        out[i] += voice_amp(q, m, i) << 1;
    }
}

static const preset_t ACID_PRESETS[] = {
    /* name, {WAVE, CUT, RES, ENV, DEC, ACC, DRV, SLIDE}, {A D S R}, fenv, mono */
    {"TB ACID", {0, 40, 100, 80, 50, 90, 0, 21}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8)},
    {"TB SQUARE", {1, 50, 90, 70, 60, 80, 0, 21}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8)},
    {"TB DIRTY", {0, 45, 110, 90, 40, 100, 80, 21}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 12, 6)},
    {"TB DEEP", {0, 20, 60, 40, 80, 60, 0, 30}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"TB SQUELCH", {0, 30, 120, 110, 30, 110, 30, 15}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 14, 10)},
    {"TB RUBBER", {1, 30, 70, 60, 90, 70, 20, 40}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 8, 8)},
    /* MAIN MENU: Roland MC-202 bass -- thicker and more raw than a 303: less resonance, more drive, a
     * firmer accent. Appended at the end (see eng_prism.c's note). */
    {"MC202", {1, 55, 75, 60, 55, 75, 60, 25}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 8, 8)},
};

static const engine_t ENG_ACID = {
    .name = "ACID",
    .page_title = {"303", "MORE"},
    .edit = {
        {"WAVE", F_ENUM, 0, 1, 0, N_ACID_WAVE, 0},
        {"CUT", F_PCT, 0, 127, 64, 0, 0},
        {"RES", F_PCT, 0, 127, 64, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"DEC", F_PCT, 0, 127, 64, 0, 0},
        {"ACC", F_PCT, 0, 127, 64, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"SLIDE", F_PCT, 0, 127, 21, 0, 0},
    },
    .presets = ACID_PRESETS,
    .npresets = NELEM(ACID_PRESETS),
    .note_on = acid_note_on,
    .render = acid_render,
    .fil_page = -1,
    .color = 0x47E8,
    .macro = {P_E1, P_E2, P_E3, P_E4},
    .poly = 1,
};
