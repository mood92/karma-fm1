/* SPDX-License-Identifier: GPL-3.0-only */
/* PRISM: models of Emilie Gillet's Plaits (github.com/pichenettes/eurorack, MIT), ported to fixed point in
 * prism_dsp.c (there: what each model is, and how close to hers: tests/prism_test.c). As on the module, three
 * controls shape every model: HARM, TIMB, MRPH. The envelope and the LFO are this firmware's: ENV is how far the
 * ADSR opens TIMB; ENV > FLT and LFO > FLT move TIMB, LFO > SHP moves MRPH. SUB adds a sine an octave down.
 *   FOLD   a triangle whose peak MRPH moves, through a waveshaper (HARM) and a wavefolder (TIMB)
 *   SWARM  eight sawtooths on grains of their own: HARM how far they spread, TIMB the grains' rate, MRPH their
 *          size (short: a cloud; long: glissandi)
 *   WAVE   192 waves in banks of 8 x 8: TIMB and MRPH move through a bank, HARM through the banks
 *   HARM   24 harmonics: TIMB the centre of the spectrum, MRPH how narrow it is, HARM how many bumps it has
 *   CHORD  a chord from one key: HARM picks it (octave, fifth, sus4, m, m7, m9, m11, 6/9, M9, M7, M), TIMB its
 *          inversion, MRPH the sound (a string machine, then organ and string waves)
 * The state of all but FOLD is in the part's engine arena (engines.c eng_arena_of), a slot a voice; they play four
 * voices a part at most, CHORD (five oscillators a key) two (voice.c: prism_poly).
 * 2026-10-20: the rest of Plaits' models, her own code (C++, unchanged: firmware/mi, through plx.cc): VA, VCF,
 * PHDIS(tortion), FM, 6OP A / B / C (the six-op FM banks), GRAIN (formant), TERRN (wave terrain), STRMC (string
 * machine), CHIP(tune), STRNG (Karplus strings), MODAL, NOISE, SPECH (speech), PARTC (particles), KICK, SNARE,
 * HIHAT. HARM, TIMB, MRPH are hers; a note starts her trigger. Each voice an instance in the arena: the small
 * models up to four voices, MODAL and the drums two, the big ones (6OP, STRMC, STRNG, SPECH, PARTC) one. */
#include "prism_tables.h"
#include "prism_dsp.c"
#include "../mi/plx.h"

static const char *const N_PRISM_MODEL[] = {"FOLD", "SWARM", "WAVE", "HARM", "CHORD",
                                            "VA", "VCF", "PHDIS", "FM", "6OP A", "6OP B", "6OP C", "GRAIN", "TERRN",
                                            "STRMC", "CHIP", "STRNG", "MODAL", "NOISE", "SPECH", "PARTC", "KICK",
                                            "SNARE", "HIHAT"};
enum { PRM_FOLD, PRM_SWARM, PRM_WAVE, PRM_HARM, PRM_CHORD, PRM_PLX, PRM_COUNT = PRM_PLX + PLX_COUNT };
_Static_assert(sizeof N_PRISM_MODEL / sizeof N_PRISM_MODEL[0] == PRM_COUNT, "a name a model");
/* each Plaits model's voices (its instances): the CPU and the arena decide */
static const uint8_t PLX_VOICES[PLX_COUNT] = {4, 4, 4, 4, 1, 1, 1, 4, 4, 1, 4, 1, 2, 4, 1, 1, 2, 2, 2};
/* their output, x 2^n: the plucked and struck ones and the particles are quiet (at the factory LEVEL trims' end) */
static const uint8_t PLX_BOOST[PLX_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0};
typedef union {
    pr_swarm_t swarm;
    pr_wave_t wave;
    pr_harm_t harm;
    pr_chord_t chord;
} prism_slot_t;
static void *eng_arena_of(const track_t *t, uint32_t eng);   /* (engines.c) */
static uint32_t prism_model(const track_t *t) { return (uint32_t)clamp(t->p[P_E0], 0, PRM_COUNT - 1); }
static uint32_t plx_stride(uint32_t k) { return (plx_size((int)k) + 7u) & ~7u; }
static uint32_t prism_poly(const track_t *t)             /* (0: NVOICE) */
{
    uint32_t m = prism_model(t);
    if (m >= PRM_PLX) {                                   /* (as many as the part's arena holds, at most its own) */
        uint32_t fit = eng_arena_size(t) / plx_stride(m - PRM_PLX), k = PLX_VOICES[m - PRM_PLX];
        return fit < k ? (fit ? fit : 1u) : k;
    }
    return m == PRM_FOLD ? 0u : m == PRM_CHORD ? 2u : 4u;
}
/* Plaits' model k for voice v: its instance in the part's arena (0: it does not fit), set up when it is not the
 * model's yet (a voice's slot keeps its model + 1 in plx_ready; the arena taken by another engine clears them) */
static uint8_t plx_ready[NPART][NVOICE];
static uint32_t plx_gen_seen[NPART];
static void *plx_slot(track_t *t, voice_t *v, uint32_t k)
{
    uint32_t p = (uint32_t)(t - trk), st = plx_stride(k), n = prism_poly(t), i = (uint32_t)(v - t->v) % NVOICE;
    uint8_t *a;
    if (p >= NPART)
        return 0;
    a = (uint8_t *)eng_arena_need(t, ENGI_PRISM, st);
    if (!a)
        return 0;
    if (plx_gen_seen[p] != eng_arena_gen[p]) {           /* (the arena was cleared: every instance starts over) */
        memset(plx_ready[p], 0, sizeof plx_ready[p]);
        plx_gen_seen[p] = eng_arena_gen[p];
    }
    i %= n ? n : 1u;                                     /* (one instance: every note plays it) */
    if (plx_ready[p][i] != k + 1u) {
        uint32_t j;
        for (j = 0; j < NVOICE; j++)                     /* (another model's instances overlap this one) */
            if (plx_ready[p][j] != k + 1u)
                plx_ready[p][j] = 0;
        if (plx_init((int)k, a + i * st, st))
            return 0;
        plx_ready[p][i] = (uint8_t)(k + 1u);
        for (j = 0; j < NVOICE; j++)                     /* (2026-10-31) PRISM's own slots share this arena: written */
            t->v[j].s[5] = 0;                            /* over, each sets itself up again (prism_slot_init) */
    }
    return a + i * st;
}
static prism_slot_t *prism_slot(track_t *t, voice_t *v)   /* the voice's slot (0 on the drum track, or past the
                                                            * part's arena: none) */
{
    prism_slot_t *a = (prism_slot_t *)eng_arena_of(t, ENGI_PRISM);
    uint32_t i = (uint32_t)(v - t->v) % NVOICE;
    return a && (i + 1u) * sizeof(prism_slot_t) <= eng_arena_size(t) ? &a[i] : 0;
}
static void prism_slot_init(track_t *t, voice_t *v)      /* the slot set for the model the track plays now */
{
    prism_slot_t *sl = prism_slot(t, v);
    uint32_t model = prism_model(t), p = (uint32_t)(t - trk);
    v->s[5] = (int32_t)model + 1;
    if (sl && model != PRM_FOLD && p < NPART)            /* (2026-10-31, Ariel's song: CHORD here, Plaits' MODAL in the next
                                                          * section ran the instance this slot had written over: a crash at
                                                          * the second pass) Plaits' instances in this arena start over */
        memset(plx_ready[p], 0, sizeof plx_ready[p]);
    if (sl)
        memset(sl, 0, sizeof *sl);                       /* (as on a fresh FM-1: what another model left is not this one's) */
    if (sl && model == PRM_SWARM)
        pr_swarm_init(&sl->swarm);
    if (sl && model == PRM_WAVE)
        pr_wave_init(&sl->wave);
    if (sl && model == PRM_HARM)
        pr_harm_init(&sl->harm);
    if (sl && model == PRM_CHORD)
        pr_chord_init(&sl->chord);
}

static void prism_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0x80000000u;      /* pr_fold_init */
    v->ph[2] = 0;                /* SUB */
    v->s[0] = 0;                 /* next */
    v->s[3] = 1;                 /* high */
    v->s[4] = 0;                 /* on: the shape and the gain start where they are asked */
    v->s[6] = 1;                 /* (Plaits' models: a trigger at the next render) */
    if (prism_model(t) < PRM_PLX)
        prism_slot_init(t, v);
}

static void prism_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t timb = (p[P_E2] << 8) + m->cutoff + mulq15(m->envq15, p[P_E4] * 256);
    int32_t mrph = (p[P_E3] << 8) + m->shape - (64 << 8), sub = p[P_E5] * 200;
    uint32_t ph2 = v->ph[2], done = 0, model = prism_model(t);
    prism_slot_t *sl = model == PRM_FOLD ? 0 : prism_slot(t, v);
    pr_fold_t s;
    if (model >= PRM_PLX) {                              /* Plaits' own models (plx.cc) */
        void *in = plx_slot(t, v, model - PRM_PLX);
        plx_params_t q;
        if (!in)
            return;
        q.note = (float)m->pitch16 * (1.0f / 16.0f);
        q.harmonics = (float)p[P_E1] * (1.0f / 127.0f);
        q.timbre = (float)clamp(timb, 0, 127 << 8) * (1.0f / (127 << 8));
        q.morph = (float)clamp(mrph, 0, 127 << 8) * (1.0f / (127 << 8));
        q.accent = (float)v->vel * (1.0f / 127.0f);
        while (done < n) {
            float b[16], aux[16];
            uint32_t k = n - done < 16u ? n - done : 16u, i;
            q.trigger = v->s[6] ? PLX_TRIG_RISE | PLX_TRIG_HIGH : v->gate ? PLX_TRIG_HIGH : PLX_TRIG_LOW;
            v->s[6] = 0;
            plx_render(in, &q, b, aux, k);
            for (i = 0; i < k; i++) {
                float f = b[i] * 32767.0f;
                int32_t x = f > 32767.0f ? 32767 : f < -32767.0f ? -32767 : (int32_t)f;
                if (sub) {
                    ph2 += m->inc >> 1;
                    x += mulq15(osc_sine(ph2), sub);
                }
                out[done + i] += mulq15(mulq15(x, amp_at(m, done + i)), VOICE_FS) << (1u + PLX_BOOST[model - PRM_PLX]);
            }
            done += k;
        }
        v->ph[2] = ph2;
        return;
    }
    if (v->s[5] != (int32_t)model + 1)                   /* (MODEL turned under a held note: its slot is not set) */
        prism_slot_init(t, v);
    timb = clamp(timb, 0, 127 << 8) * 32767 / (127 << 8);
    mrph = clamp(mrph, 0, 127 << 8) * 32767 / (127 << 8);
    s.phase = v->ph[0];
    s.next = v->s[0];
    s.shape = v->s[1];
    s.gain = v->s[2];
    s.high = (uint8_t)v->s[3];
    s.on = (uint8_t)v->s[4];
    while (done < n) {                                   /* (the mix runs in blocks of CTL) */
        int16_t b[CTL];
        uint32_t k = n - done < CTL ? n - done : CTL, i;
        if (sl && model == PRM_SWARM)
            pr_swarm_render(&sl->swarm, m->inc, p[P_E1] * 258, timb, mrph, b, k);
        else if (sl && model == PRM_WAVE)
            pr_wave_render(&sl->wave, m->inc, p[P_E1] * 258, timb, mrph, b, k);
        else if (sl && model == PRM_HARM)
            pr_harm_render(&sl->harm, m->inc, p[P_E1] * 258, timb, mrph, b, k);
        else if (sl && model == PRM_CHORD)           /* the knob's eleventh: the middle of that chord's range of hers */
            pr_chord_render(&sl->chord, m->inc, (2 * (p[P_E1] * 11 / 128) + 1) * 1460, timb, mrph, b, k, 0);
        else
            pr_fold_render(&s, m->inc, p[P_E1] * 258, timb, mrph, b, k);
        for (i = 0; i < k; i++) {
            int32_t x = b[i];
            if (sub) {
                ph2 += m->inc >> 1;
                x += mulq15(osc_sine(ph2), sub);
            }
            out[done + i] += mulq15(mulq15(x, amp_at(m, done + i)), VOICE_FS) << (model == PRM_SWARM ? 2 : 1);   /* (the swarm: quieter) */
        }
        done += k;
    }
    v->ph[0] = s.phase;
    v->ph[2] = ph2;
    v->s[0] = s.next;
    v->s[1] = s.shape;
    v->s[2] = s.gain;
    v->s[3] = s.high;
    v->s[4] = s.on;
}

static const preset_t PRISM_PRESETS[] = {
    /* name, {MODEL, HARM, TIMB, MRPH, ENV, SUB, -, -}, {A D S R}, fenv, mono */
    /* Rhodes Juno Sketch reference, KEYS bank: tine/reed/cp/fm/m1piano -> FM(8)/VA(5)/WAVE(2); mallet -> MODAL(17) */
    {"MK I STAGE", {8, 60, 35, 45, 65, 0, 0, 0}, {0, 38, 55, 28}, 0, 0, FX(0, 15, 10, 22)},
    {"SUITCASE", {8, 45, 28, 50, 55, 0, 0, 0}, {0, 45, 65, 35}, 0, 0, FX(0, 12, 12, 28)},
    {"MK II BRIGHT", {8, 80, 48, 40, 75, 0, 0, 0}, {0, 30, 45, 22}, 0, 0, FX(0, 18, 8, 18)},
    {"PHASER TINE", {8, 60, 40, 45, 65, 0, 0, 0}, {0, 35, 55, 28}, 0, 0, FX(0, 45, 14, 24)},
    {"DYNO GLASS", {8, 95, 55, 30, 80, 0, 0, 0}, {0, 25, 30, 20}, 0, 0, FX(0, 10, 16, 20)},
    {"BARK DRIVE", {8, 110, 60, 35, 85, 0, 0, 0}, {0, 20, 30, 16}, 0, 0, FX(30, 10, 10, 18)},
    {"LOFI TINE", {8, 35, 25, 55, 45, 0, 0, 0}, {0, 50, 60, 30}, 0, 0, FX(0, 8, 20, 30)},
    {"WURLI 200A", {8, 50, 65, 40, 50, 0, 0, 0}, {0, 40, 60, 30}, 0, 0, FX(0, 10, 10, 24)},
    {"WURLI GROWL", {8, 65, 85, 35, 55, 0, 0, 0}, {0, 35, 55, 26}, 0, 0, FX(10, 10, 10, 20)},
    {"MUTED WURLI", {8, 30, 45, 50, 40, 0, 0, 0}, {0, 45, 65, 35}, 0, 0, FX(0, 8, 14, 30)},
    {"PIANET", {8, 55, 50, 25, 60, 0, 0, 0}, {0, 18, 15, 16}, 0, 0, FX(0, 8, 8, 18)},
    {"CP-70 GRAND", {5, 55, 45, 60, 70, 0, 0, 0}, {0, 30, 80, 22}, 0, 0, FX(0, 15, 12, 30)},
    {"CP CHORUS", {5, 60, 50, 60, 70, 0, 0, 0}, {0, 30, 80, 22}, 0, 0, FX(0, 55, 14, 32)},
    {"M1 PIANO", {2, 40, 50, 35, 55, 0, 0, 0}, {0, 40, 40, 22}, 0, 0, FX(0, 20, 12, 22)},
    {"DARK PIANO", {2, 20, 35, 30, 40, 0, 0, 0}, {0, 55, 50, 30}, 0, 0, FX(0, 15, 16, 30)},
    {"DX E.PIANO", {8, 70, 45, 45, 60, 0, 0, 0}, {0, 35, 50, 26}, 0, 0, FX(0, 16, 12, 24)},
    {"GLASS BELL", {17, 100, 65, 80, 0, 0, 0, 0}, {0, 0, 127, 55}, 0, 0, FX(0, 10, 20, 40)},
    {"VIBES", {17, 60, 45, 60, 0, 0, 0, 0}, {0, 0, 127, 70}, 0, 0, FX(0, 15, 18, 45)},
    /* CHORDS bank: non-juno (tine/m1piano) chord stabs */
    {"HOUSE STAB", {4, 62, 45, 10, 0, 0, 0, 0}, {0, 62, 0, 40}, 0, 0, FX(0, 10, 26, 26)},
    {"RHODES STAB", {8, 65, 38, 45, 60, 0, 0, 0}, {0, 25, 20, 20}, 0, 0, FX(0, 15, 55, 30), XP(P_CHORD + 1, 2)},
    {"PIANO STAB", {2, 35, 40, 30, 60, 0, 0, 0}, {0, 30, 10, 20}, 0, 0, FX(0, 18, 16, 24), XP(P_CHORD + 1, 2)},
    /* BASS bank: non-juno (fmbass -> FM(8) mono low register; organ bass -> CHORD(4) low register) */
    {"LATELY BASS", {8, 55, 60, 30, 70, 30, 0, 0}, {0, 30, 40, 15}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -12)},
    {"SOLID BASS", {8, 40, 45, 35, 65, 40, 0, 0}, {0, 25, 45, 14}, 0, 1, FX(0, 0, 0, 3), XP(P_TRANS + 1, -12)},
    {"ROUND BASS", {8, 30, 35, 40, 55, 45, 0, 0}, {0, 35, 55, 18}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -12)},
    {"SLAP BASS", {8, 60, 55, 25, 75, 35, 0, 0}, {0, 15, 20, 10}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -12)},
    {"FRETLESS", {8, 25, 30, 45, 50, 50, 0, 0}, {0, 40, 60, 20}, 0, 1, FX(0, 0, 0, 6), XP(P_GLIDE + 1, 25, P_TRANS + 1, -12)},
    {"DEEP SUB", {8, 15, 20, 50, 40, 70, 0, 0}, {0, 50, 90, 25}, 0, 1, FX(0, 0, 0, 3), XP(P_TRANS + 1, -24)},
    {"DUB SUB", {8, 20, 25, 48, 42, 65, 0, 0}, {0, 55, 95, 30}, 0, 1, FX(0, 0, 5, 10), XP(P_TRANS + 1, -24)},
    {"ORGAN BASS", {4, 6, 10, 70, 0, 0, 0, 0}, {0, 20, 110, 15}, 0, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12)},
    /* ORGAN bank: drawbar-style organs, all Plaits CHORD model (HARM = chord quality, MRPH = organ-end timbre) */
    {"M1 ORGAN 2", {4, 119, 30, 75, 10, 0, 0, 0}, {0, 10, 120, 14}, 0, 0, FX(0, 15, 6, 20)},
    {"JAZZ ORGAN", {4, 51, 40, 65, 8, 0, 0, 0}, {0, 12, 118, 18}, 0, 0, FX(0, 18, 8, 24)},
    {"GOSPEL ORGAN", {4, 62, 35, 70, 12, 0, 0, 0}, {0, 10, 120, 20}, 0, 0, FX(0, 20, 10, 40)},
    {"CLICK ORGAN", {4, 40, 25, 60, 45, 0, 0, 0}, {0, 6, 108, 10}, 0, 0, FX(0, 10, 4, 14)},
    {"PERC ORGAN", {4, 28, 20, 55, 35, 0, 0, 0}, {0, 8, 70, 12}, 0, 0, FX(0, 8, 6, 16)},
    {"DEEP ORGAN", {4, 6, 15, 65, 6, 0, 0, 0}, {0, 14, 120, 22}, 0, 0, FX(0, 10, 8, 28)},
    /* FX bank: non-juno sfx-tagged entries, NOISE model (18) */
    {"RISER", {18, 80, 30, 100, 110, 0, 0, 0}, {127, 60, 0, 10}, 0, 1, FX(0, 0, 10, 30)},
    {"DOWNLIFTER", {18, 70, 60, 90, 90, 0, 0, 0}, {0, 110, 0, 30}, 0, 1, FX(0, 0, 14, 30)},
    {"IMPACT", {18, 40, 80, 40, 20, 0, 0, 0}, {0, 8, 0, 25}, 0, 0, FX(10, 0, 20, 45)},
    {"LASER ZAP", {18, 100, 30, 100, 15, 0, 0, 0}, {0, 10, 0, 8}, 0, 1, FX(0, 0, 8, 10)},
    {"BLEEP", {18, 20, 20, 30, 80, 0, 0, 0}, {0, 8, 0, 6}, 0, 1, FX(0, 0, 16, 10)},
    {"DUB SIREN", {18, 60, 50, 70, 60, 0, 0, 0}, {20, 80, 100, 50}, 0, 0, FX(0, 10, 40, 50), XP(P_LRATE + 1, 30, P_LD_FLT + 1, 40)},
    {"VINYL CRKL", {18, 10, 20, 60, 0, 0, 0, 0}, {30, 100, 127, 60}, 0, 0, FX(0, 0, 0, 20)},
    {"WIND", {18, 15, 45, 25, 20, 0, 0, 0}, {60, 100, 127, 80}, 0, 0, FX(0, 0, 0, 30)},
};

/* CHORD: its three controls by what they do there */
static const param_desc_t *prism_desc(const track_t *t, uint32_t k)
{
    static const char *const CH[] = {"OCT", "5TH", "SUS4", "MIN", "MIN7", "MIN9", "MIN11", "6/9", "MAJ9", "MAJ7", "MAJ", 0};
    static const param_desc_t D[3] = {{"CHORD", F_INT, 0, 127, 64, CH, 0}, {"INVRT", F_PCT, 0, 127, 40, 0, 0},
                                      {"SOUND", F_PCT, 0, 127, 64, 0, 0}};
    return prism_model(t) == PRM_CHORD && k >= 1u && k <= 3u ? &D[k - 1u] : 0;
}

static const engine_t ENG_PRISM = {
    "PLAITS", {"TONE", "MOD"},      /* (the name Ariel wants on his build; the files and the code say PRISM) */
    {
        {"MODEL", F_ENUM, 0, PRM_COUNT - 1, 0, N_PRISM_MODEL, 0},
        {"HARM", F_PCT, 0, 127, 64, 0, 0},
        {"TIMB", F_PCT, 0, 127, 40, 0, 0},
        {"MRPH", F_PCT, 0, 127, 64, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    PRISM_PRESETS, sizeof(PRISM_PRESETS) / sizeof(PRISM_PRESETS[0]), 0, prism_note_on, prism_render,
    0xA2FF, {P_E1, P_E2, P_E3, P_REL},
    .desc = prism_desc,
};
