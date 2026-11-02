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
    {"JUNO BRASS", {0, 0, 0, 0, 34, 20, 70, 1}, {12, 60, 80, 30}, 0, 0, FX(0, 0, 0, 10)},
    {"JUNO STRING", {2, 40, 0, 0, 70, 10, 10, 2}, {60, 70, 110, 70}, 0, 0, FX(0, 0, 0, 20), XP(P_LD_SHP + 1, 40, P_LRATE + 1, 40)},
    {"JUNO PAD", {1, 60, 40, 0, 50, 30, 30, 2}, {80, 90, 100, 90}, 0, 0, FX(0, 0, 10, 30), XP(P_LD_SHP + 1, 60, P_LRATE + 1, 30)},
    {"JUNO BASS", {1, 30, 90, 0, 22, 40, 60, 0}, {0, 40, 50, 10}, 0, 1, FX(0, 0, 0, 0)},
    {"SUB 106", {0, 0, 127, 0, 18, 10, 40, 0}, {0, 50, 90, 10}, 0, 1, FX(0, 0, 0, 0)},
    {"JUNO KEYS", {1, 20, 30, 0, 30, 15, 80, 1}, {0, 70, 30, 40}, 0, 0, FX(0, 0, 8, 16)},
    {"JUNO STAB", {0, 0, 50, 0, 50, 60, 75, 1}, {0, 55, 20, 25}, 0, 0, FX(0, 0, 14, 12)},
    {"JUNO LEAD", {2, 50, 0, 0, 55, 50, 40, 1}, {0, 60, 100, 20}, 0, 1, FX(0, 0, 12, 10), XP(P_GLIDE + 1, 20)},
    {"JUNO SWEEP", {0, 0, 40, 10, 10, 100, 110, 2}, {70, 110, 60, 80}, 0, 0, FX(0, 0, 10, 24)},
    {"JUNO ORGAN", {1, 0, 60, 0, 90, 0, 0, 3}, {0, 0, 127, 10}, 0, 0, FX(0, 0, 0, 14)},
    {"JUNO HOOVER", {1, 90, 60, 0, 60, 20, 30, 3}, {10, 60, 100, 40}, 0, 1, FX(0, 0, 0, 16), XP(P_LD_SHP + 1, 90, P_LRATE + 1, 70)},
    /* MAIN MENU: the 106's chorus I+II drenching a held chord -- CHORD on at 9TH so one key plays the real
     * voicing (the reference: "chords drenched in this sound define a lot of deep house"). */
    {"JUNO 9TH", {1, 55, 45, 0, 42, 18, 30, 3}, {70, 85, 95, 85}, 0, 0, FX(0, 0, 8, 28), XP(P_CHORD + 1, 3)},
    /* MAIN MENU: dub techno chord stab -- a simple triad (CHORD at 1), filtered down and snapped short so the
     * delay/reverb tail (not the patch) carries the sound, per the reference's "build the chord, then spend
     * most of your time on the delay and reverb chain". */
    {"DUB STAB", {0, 0, 30, 0, 32, 15, 55, 2}, {0, 30, 10, 14}, 0, 0, FX(0, 10, 70, 58), XP(P_CHORD + 1, 1)},
    /* MAIN MENU: deep house organ chord -- CHOR I+II for the Leslie-ish spin, a slow LFO>RATE wobble on top, held
     * in 7th chords (CHORD at 2). No filter envelope: an organ's cutoff doesn't move. */
    {"DEEP ORGAN", {1, 0, 55, 0, 85, 0, 0, 3}, {0, 0, 120, 14}, 0, 0, FX(0, 0, 4, 18), XP(P_CHORD + 1, 2, P_LRATE + 1, 14)},
    /* MAIN MENU: Jupiter-8-style strings/brass -- saw+pulse, slow attack, drenched in chorus, 9th chords for the
     * "expensive" wide/glassy sound on slower, emotional progressions. */
    {"JUP STRINGS", {2, 45, 0, 0, 65, 15, 20, 3}, {55, 70, 110, 70}, 0, 0, FX(0, 0, 10, 34), XP(P_CHORD + 1, 3)},
    /* MAIN MENU: chord-capable brass stab -- short, loud, stabbing, a plain triad (CHORD at 1) for a big-room
     * house/garage hit. */
    {"BRASS STAB", {0, 0, 0, 0, 40, 25, 75, 1}, {0, 45, 20, 20}, 0, 0, FX(0, 0, 8, 10), XP(P_CHORD + 1, 1)},
    /* MAIN MENU: basses, appended at the end (see the note on eng_prism.c's appended block). */
    /* Moog-style sub bass -- pure sine-ish sub (big SUB, no PW/NOISE), a slow filter, the foundation for
     * jazzy deep house. */
    {"MOOG SUB", {0, 0, 110, 0, 15, 5, 20, 0}, {0, 70, 100, 20}, 0, 1, FX(0, 0, 0, 0)},
    /* Roland SH-101 bass -- square + sub, a snappy envelope, the Detroit techno/electro staple. */
    {"SH101 BASS", {1, 0, 80, 0, 30, 10, 70, 0}, {0, 25, 30, 10}, 0, 1, FX(0, 0, 0, 0)},
    /* Moog Taurus-style pedal bass -- slow, heavy, cinematic; suits darker dubby tracks. */
    {"TAURUS PDL", {0, 0, 120, 0, 12, 8, 30, 0}, {30, 90, 110, 60}, 0, 1, FX(0, 0, 0, 0)},
    /* Oberheim Matrix-1000 bass -- fat analog saw with a little grit (DCO's own drive, no SUB). */
    {"MATRIX BASS", {0, 0, 60, 10, 28, 30, 55, 0}, {0, 30, 60, 15}, 0, 1, FX(0, 0, 0, 0)},
    /* 808-style sub bass -- a long sine sub tuned to the key, essential for electro/Miami bass. */
    {"808 SUB", {0, 0, 127, 0, 10, 0, 10, 0}, {0, 0, 127, 40}, 0, 1, FX(0, 0, 0, 0)},
    /* electro bass stab (Drexciya/Aux 88) -- short, dry square, a fast filter envelope snap. */
    {"ELECTRO BASS", {1, 0, 0, 0, 35, 20, 90, 0}, {0, 15, 0, 10}, 0, 1, FX(0, 0, 0, 0)},
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
