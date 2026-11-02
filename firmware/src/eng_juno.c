/* SPDX-License-Identifier: GPL-3.0-only */
/* JUNO: the Juno-106 voice of FM-106 (juno_dsp.c: its DCO, IR3109 ladder and BBD chorus; PatttF, GPL-3.0, after
 * ultramaster KR-106) as a SLOOP engine, up to 6 voices. The track's ENV is the Juno's one envelope (the VCA, and
 * the filter by ENV), its LFO the Juno's LFO (LFO > PIT vibrato, > FLT the filter, > SHP the pulse width).
 *   WAVE   SAW, PLS (the pulse) or S+P (both), as the Juno's two switches
 *   PW     the pulse width, 50 .. 95 %
 *   SUB    the sub-oscillator, an octave down
 *   NOISE  the noise generator
 *   CUT    the cutoff, RES the resonance (self-oscillates at the top), ENV how far the envelope opens it
 *   CHOR   the chorus: OFF, I, II, I+II (the fast vibrato of both buttons)
 * The filter follows the keyboard half way, as the Juno's KYBD at the middle. */
#include "juno_dsp.c"

typedef struct {
    jn_dco_t dco;
    jn_vcf_t vcf;
    int64_t dc;                  /* the 1.59 Hz coupling after the DCO */
} jn_voice_t;
typedef struct {
    jn_voice_t v[NVOICE];
    jn_chorus_t cho;
    uint8_t init;
} juno_slot_t;

static const char *const N_JN_WAVE[] = {"SAW", "PLS", "S+P"};
static const char *const N_JN_CHO[] = {"OFF", "I", "II", "I+II"};

static juno_slot_t *juno_slot(track_t *t)
{
    juno_slot_t *J;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    J = (juno_slot_t *)eng_arena_of(t, ENGI_JUNO);
    if (!J->init) {                                   /* (a fresh arena is all 0) */
        uint32_t k;
        for (k = 0; k < NVOICE; k++) {
            J->v[k].dco.noise_seed = 123456789u + k * 987654321u;
            J->v[k].vcf.noise_seed = 123456789u + k * 31u;
        }
        J->init = 1;
    }
    return J;
}

static void juno_note_on(track_t *t, voice_t *v)
{
    (void)juno_slot(t);                               /* (the DCO runs free, as the Juno's) */
    (void)v;
}

static void juno_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    juno_slot_t *J = juno_slot(t);
    jn_voice_t *V;
    jn_coef_t k;
    uint32_t i, wave = (uint32_t)clamp(p[P_E0], 0, 2), sub = (uint32_t)clamp(p[P_E2], 0, 127);
    uint32_t noi = (uint32_t)clamp(p[P_E3], 0, 127);
    int32_t pw, cut;
    if (!J)
        return;
    V = &J->v[(uint32_t)(v - t->v) % NVOICE];
    pw = (clamp(p[P_E1], 0, 127) * 105 + ((m->shape - (64 << 8)) >> 8) * 105) / 127;   /* + LFO > SHP: the PWM */
    cut = (p[P_E4] << 8) + m->cutoff + mulq15(m->envq15, p[P_E6] * 256) + (m->pitch16 - 60 * 16) * 6;
    jn_vcf_coef(&k, (uint32_t)clamp(cut, 0, 32767) >> 1, (uint32_t)clamp(p[P_E5], 0, 127));
    for (i = 0; i < n; i++) {
        int32_t x = jn_dco(&V->dco, m->inc, wave != 1u, wave != 0u, pw, sub, noi);
        x = jn_vcf_run(&V->vcf, &k, jn_dc_block(x, &V->dc));
        out[i] += voice_amp(x, m, i) << 1;           /* (a voice peaks near VOICE_FS, as ANALOG's) */
    }
}

/* after the voices: the chorus on the part */
static void juno_post(track_t *t, int32_t *out, uint32_t n)
{
    juno_slot_t *J = juno_slot(t);
    uint32_t i, mode = (uint32_t)clamp(t->p[P_E7], 0, 3);
    if (!J || !mode)
        return;
    for (i = 0; i < n; i++)                          /* (the part's sum, up to 6 voices: a quarter into the
                                                      * chorus, whose limiter starts at -2.5 dB of Q15) */
        out[i] = jn_chorus(&J->cho, mode, out[i] >> 2) << 2;
}

static const preset_t JUNO_PRESETS[] = {
    /* name, {WAVE, PW, SUB, NOISE, CUT, RES, ENV, CHOR}, {A D S R}, fenv, mono */
    /* Rhodes Juno Sketch reference, CHORDS bank (juno-tagged entries) */
    {"DEEP HOUSE", {0, 64, 44, 0, 48, 44, 57, 1}, {0, 16, 32, 16}, 0, 0, FX(0, 0, 23, 23)},
    {"MIN9 STAB", {2, 64, 25, 0, 51, 38, 51, 1}, {0, 18, 25, 18}, 0, 0, FX(0, 0, 23, 25), XP(P_CHORD + 1, 3)},
    {"DUB CHORD", {2, 64, 25, 0, 44, 38, 57, 1}, {0, 14, 13, 18}, 0, 0, FX(0, 0, 50, 27)},
    {"CHORUS STAB", {2, 64, 0, 0, 57, 25, 57, 3}, {0, 14, 19, 14}, 0, 0, FX(0, 0, 18, 23)},
    {"FILTER STAB", {0, 64, 38, 0, 25, 57, 108, 1}, {0, 13, 6, 14}, 0, 0, FX(0, 0, 23, 18)},
    {"DETROIT STAB", {2, 64, 51, 0, 53, 64, 76, 2}, {0, 10, 13, 14}, 0, 0, FX(0, 0, 32, 18)},
    {"ORGAN STAB", {1, 64, 76, 0, 79, 13, 19, 1}, {0, 9, 108, 7}, 0, 0, FX(0, 0, 18, 18)},
    {"SOFT STAB", {1, 64, 25, 0, 38, 13, 38, 3}, {0, 23, 25, 23}, 0, 0, FX(0, 0, 18, 32)},
    {"ACID CHORD", {0, 64, 0, 0, 32, 102, 89, 0}, {0, 11, 6, 11}, 0, 0, FX(0, 0, 27, 14)},
    {"ANALOG KEYS", {2, 44, 25, 0, 51, 19, 57, 1}, {0, 50, 25, 27}, 0, 0, FX(0, 0, 9, 23)},
    {"GLASS PLUCK", {1, 23, 0, 0, 44, 70, 70, 1}, {0, 20, 0, 32}, 0, 0, FX(0, 0, 32, 32)},
    {"DUSTY KEYS", {2, 53, 32, 4, 38, 13, 44, 1}, {0, 41, 38, 27}, 0, 0, FX(0, 0, 18, 27)},
    /* PADS bank (all juno-tagged) */
    {"LUSH CHORDS", {2, 64, 32, 0, 64, 25, 32, 3}, {1, 54, 95, 41}, 0, 0, FX(0, 0, 14, 32)},
    {"WARM PAD", {2, 64, 38, 0, 51, 19, 19, 3}, {30, 90, 114, 63}, 0, 0, FX(0, 0, 14, 41)},
    {"DARK PAD", {2, 64, 38, 0, 19, 38, 76, 1}, {125, 127, 114, 63}, 0, 0, FX(0, 0, 14, 45)},
    {"NIGHT PAD", {0, 64, 38, 0, 44, 25, 25, 1}, {40, 90, 108, 54}, 0, 0, FX(0, 0, 27, 41)},
    {"PWM STRINGS", {1, 64, 0, 0, 70, 13, 13, 2}, {18, 68, 108, 50}, 0, 0, FX(0, 0, 9, 36)},
    {"90S STRINGS", {2, 64, 0, 0, 64, 13, 13, 3}, {20, 68, 114, 54}, 0, 0, FX(0, 0, 9, 41)},
    {"STRING MCH", {2, 64, 0, 0, 70, 6, 6, 3}, {13, 90, 121, 50}, 0, 0, FX(0, 0, 9, 36)},
    {"CHOIR PAD", {1, 64, 0, 5, 41, 38, 13, 3}, {40, 90, 114, 72}, 0, 0, FX(0, 0, 14, 50)},
    {"AIR PAD", {1, 64, 0, 38, 70, 19, 13, 2}, {50, 90, 114, 72}, 0, 0, FX(0, 0, 18, 54)},
    /* BASS bank (juno-tagged entries) */
    {"JUNO BASS", {1, 64, 89, 0, 38, 32, 51, 0}, {0, 18, 64, 7}, 0, 1, FX(0, 0, 0, 3)},
    {"PLUCK BASS", {0, 64, 64, 0, 41, 57, 70, 0}, {0, 11, 19, 7}, 0, 1, FX(0, 0, 7, 5)},
    {"HOLLOW BASS", {1, 38, 0, 0, 36, 38, 44, 0}, {0, 23, 76, 7}, 0, 1, FX(0, 0, 0, 3)},
    {"SUB BASS", {1, 64, 127, 0, 28, 19, 25, 0}, {0, 23, 102, 9}, 0, 1, FX(0, 0, 0, 5)},
    /* LEADS bank (juno-tagged entries) */
    {"SQUARE LEAD", {1, 64, 0, 0, 70, 25, 25, 1}, {0, 27, 102, 14}, 0, 1, FX(0, 0, 27, 23)},
    {"SAW LEAD", {2, 64, 25, 0, 76, 38, 38, 2}, {0, 27, 102, 14}, 0, 1, FX(0, 0, 27, 23)},
    {"SOFT LEAD", {0, 64, 25, 0, 64, 38, 32, 1}, {2, 36, 102, 23}, 0, 1, FX(0, 0, 27, 27)},
    {"MUTE TRUMPET", {2, 64, 0, 0, 44, 44, 64, 0}, {2, 23, 95, 11}, 0, 1, FX(0, 0, 23, 27), XP(P_GLIDE + 1, 18)},
    {"JUNO BRASS", {0, 64, 25, 0, 38, 25, 83, 1}, {3, 27, 76, 23}, 0, 0, FX(0, 0, 7, 20)},
    {"SOFT BRASS", {0, 64, 0, 0, 36, 19, 57, 1}, {6, 45, 89, 23}, 0, 0, FX(0, 0, 11, 27)},
    /* FX bank (juno-tagged entries) */
    {"NOISE SWEEP", {2, 64, 0, 44, 32, 70, 70, 2}, {60, 113, 89, 68}, 0, 0, FX(0, 0, 23, 45)},
};

static const engine_t ENG_JUNO = {
    .name = "JUNO",
    .page_title = {"DCO", "VCF"},
    .edit = {
        {"WAVE", F_ENUM, 0, 2, 0, N_JN_WAVE, 0},
        {"PW", F_PCT, 0, 127, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"NOISE", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_PCT, 0, 127, 64, 0, 0},
        {"RES", F_PCT, 0, 127, 0, 0, 0},
        {"ENV", F_PCT, 0, 127, 40, 0, 0},
        {"CHOR", F_ENUM, 0, 3, 1, N_JN_CHO, 0},
    },
    .presets = JUNO_PRESETS,
    .npresets = NELEM(JUNO_PRESETS),
    .note_on = juno_note_on,
    .render = juno_render,
    .post = juno_post,
    .fil_page = 1,
    .color = 0x07FF,
    .macro = {P_E4, P_E5, P_E6, P_E7},
    .poly = 6,
};
