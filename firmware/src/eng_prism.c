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
    {"FOLD LEAD", {0, 64, 46, 64, 60, 0, 0, 0}, {0, 70, 90, 34}, 0, 1, FX(0, 8, 18, 14), XP(P_GLIDE + 1, 24)},
    {"FOLD BASS", {0, 64, 30, 40, 84, 60, 0, 0}, {0, 58, 40, 20}, 0, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)},
    {"WEST PLUCK", {0, 20, 20, 64, 110, 0, 0, 0}, {0, 52, 0, 40}, 0, 0, FX(0, 0, 24, 22)},
    {"GLASS FOLD", {0, 110, 58, 90, 30, 0, 0, 0}, {40, 90, 110, 80}, 0, 0, FX(0, 40, 18, 46)},
    {"BUCHLA BELL", {0, 127, 84, 20, 70, 0, 0, 0}, {0, 84, 0, 70}, 0, 0, FX(0, 10, 26, 40)},
    {"FOLD DRONE", {0, 0, 70, 110, 0, 40, 0, 0}, {70, 90, 127, 90}, 0, 0, FX(0, 30, 20, 50), XP(P_LRATE + 1, 20, P_LD_FLT + 1, 30)},
    /* SWARM (ENV at 0: the grains' rate is the knob's) */
    {"SUPER SWARM", {1, 36, 56, 100, 0, 0, 0, 0}, {4, 80, 115, 50}, 0, 0, FX(0, 20, 14, 30)},
    {"SWARM PAD", {1, 52, 44, 90, 0, 0, 0, 0}, {70, 90, 120, 90}, 0, 0, FX(0, 30, 20, 55)},
    {"GLISS CLOUD", {1, 84, 60, 70, 0, 0, 0, 0}, {40, 90, 120, 80}, 0, 0, FX(0, 0, 30, 50)},
    {"GRAIN RAIN", {1, 70, 96, 20, 0, 0, 0, 0}, {10, 90, 120, 70}, 0, 0, FX(0, 0, 34, 46)},
    {"SWARM BASS", {1, 24, 50, 110, 0, 50, 0, 0}, {0, 70, 100, 30}, 0, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -24)},
    /* WAVE */
    {"TABLE SWEEP", {2, 20, 40, 64, 30, 0, 0, 0}, {50, 90, 118, 80}, 0, 0, FX(0, 30, 18, 46), XP(P_LRATE + 1, 24, P_LD_FLT + 1, 40)},
    {"TABLE BASS", {2, 0, 30, 20, 70, 40, 0, 0}, {0, 62, 60, 22}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"DIGI KEYS", {2, 50, 60, 30, 60, 0, 0, 0}, {0, 78, 30, 50}, 0, 0, FX(0, 20, 16, 30)},
    {"VOX TABLE", {2, 90, 50, 80, 20, 0, 0, 0}, {60, 90, 118, 84}, 0, 0, FX(0, 40, 16, 52)},
    {"TABLE LEAD", {2, 64, 80, 50, 40, 0, 0, 0}, {0, 70, 100, 30}, 0, 1, FX(0, 10, 22, 20), XP(P_GLIDE + 1, 28)},
    /* HARM */
    {"GLASS ORGAN", {3, 30, 30, 70, 20, 0, 0, 0}, {4, 80, 120, 40}, 0, 0, FX(0, 30, 10, 36)},
    {"OVERTONES", {3, 0, 20, 96, 100, 0, 0, 0}, {30, 96, 60, 80}, 0, 0, FX(0, 0, 26, 50)},
    {"SPECTRAL PD", {3, 70, 50, 60, 0, 0, 0, 0}, {70, 90, 120, 90}, 0, 0, FX(0, 36, 20, 56), XP(P_LRATE + 1, 18, P_LD_FLT + 1, 36)},
    {"ADD BELL", {3, 96, 70, 80, 60, 0, 0, 0}, {0, 88, 0, 76}, 0, 0, FX(0, 0, 24, 44)},
    /* CHORD (HARM: 6 OCT, 17 5TH, 28 SUS4, 40 M, 51 M7, 62 M9, 74 M11, 85 6/9, 96 MAJ9, 108 MAJ7, 119 MAJ; ENV at 0:
     * TIMB is the inversion) */
    {"STRING MCH", {4, 108, 30, 30, 0, 0, 0, 0}, {40, 90, 118, 70}, 0, 0, FX(0, 50, 10, 40)},
    {"SOLINA MIN", {4, 51, 40, 20, 0, 0, 0, 0}, {50, 90, 118, 76}, 0, 0, FX(0, 60, 14, 46)},
    {"ORGAN CHRD", {4, 119, 20, 90, 0, 0, 0, 0}, {2, 80, 120, 30}, 0, 0, FX(0, 20, 0, 24)},
    {"HOUSE STAB", {4, 62, 45, 10, 0, 0, 0, 0}, {0, 62, 0, 40}, 0, 0, FX(0, 10, 26, 26)},
    {"RAVE 5TH", {4, 17, 30, 0, 0, 0, 0, 0}, {0, 66, 30, 30}, 0, 0, FX(20, 0, 22, 20)},
    {"DREAM 69", {4, 85, 60, 110, 0, 0, 0, 0}, {70, 90, 120, 90}, 0, 0, FX(0, 30, 24, 56), XP(P_LRATE + 1, 14, P_LD_FLT + 1, 24)},
    /* (2026-10-20) Plaits' own models: 5 VA, 6 VCF, 7 PHDIS, 8 FM, 9..11 6OP A B C, 12 GRAIN, 13 TERRN, 14 STRMC,
     * 15 CHIP, 16 STRNG, 17 MODAL, 18 NOISE, 19 SPECH, 20 PARTC, 21 KICK, 22 SNARE, 23 HIHAT. The ones with their
     * own decay (6OP, STRNG, MODAL, the drums) hold SUS at 127 and ENV at 0 */
    {"VA SAW LEAD", {5, 40, 20, 100, 30, 0, 0, 0}, {0, 70, 100, 30}, 0, 1, FX(0, 10, 22, 16), XP(P_GLIDE + 1, 26)},
    {"VA PULSE PAD", {5, 70, 64, 30, 0, 0, 0, 0}, {70, 90, 118, 84}, 0, 0, FX(0, 40, 18, 50), XP(P_LRATE + 1, 20, P_LD_SHP + 1, 30)},
    {"VCF BASS", {6, 40, 40, 30, 80, 40, 0, 0}, {0, 60, 50, 20}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"VCF SWEEP", {6, 90, 30, 70, 50, 0, 0, 0}, {40, 100, 100, 80}, 0, 0, FX(0, 20, 20, 44), XP(P_LRATE + 1, 16, P_LD_FLT + 1, 40)},
    {"PD KEYS", {7, 30, 60, 40, 70, 0, 0, 0}, {0, 76, 40, 50}, 0, 0, FX(0, 20, 16, 30)},
    {"PD RESO", {7, 90, 80, 90, 60, 0, 0, 0}, {0, 70, 80, 40}, 0, 1, FX(0, 0, 20, 20), XP(P_GLIDE + 1, 20)},
    {"FM BELL", {8, 100, 70, 20, 80, 0, 0, 0}, {0, 90, 0, 80}, 0, 0, FX(0, 10, 24, 46)},
    {"FM BASS", {8, 40, 50, 60, 90, 30, 0, 0}, {0, 60, 40, 20}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -12)},
    {"DX BANK A", {9, 0, 64, 64, 0, 0, 0, 0}, {0, 0, 127, 50}, 0, 0, FX(0, 10, 14, 30)},
    {"DX BANK B", {10, 0, 64, 64, 0, 0, 0, 0}, {0, 0, 127, 50}, 0, 0, FX(0, 10, 14, 30)},
    {"DX BANK C", {11, 0, 64, 64, 0, 0, 0, 0}, {0, 0, 127, 50}, 0, 0, FX(0, 10, 14, 30)},
    {"FORMANT VOX", {12, 50, 60, 40, 30, 0, 0, 0}, {20, 80, 110, 60}, 0, 0, FX(0, 30, 16, 40), XP(P_LRATE + 1, 18, P_LD_FLT + 1, 30)},
    {"GRAIN BUZZ", {12, 100, 40, 90, 60, 0, 0, 0}, {0, 70, 90, 30}, 0, 1, FX(10, 0, 20, 16)},
    {"TERRAIN", {13, 30, 50, 50, 40, 0, 0, 0}, {0, 70, 100, 40}, 0, 1, FX(0, 10, 20, 20), XP(P_LRATE + 1, 24, P_LD_SHP + 1, 40)},
    {"TERRAIN PAD", {13, 90, 60, 80, 0, 0, 0, 0}, {70, 90, 118, 90}, 0, 0, FX(0, 40, 20, 56), XP(P_LRATE + 1, 12, P_LD_FLT + 1, 30)},
    {"STR MACHINE", {14, 50, 60, 40, 0, 0, 0, 0}, {50, 90, 118, 80}, 0, 1, FX(0, 30, 10, 46)},
    {"POLY STRING", {14, 100, 80, 80, 0, 0, 0, 0}, {40, 90, 118, 84}, 0, 1, FX(0, 40, 14, 50)},
    {"CHIP LEAD", {15, 0, 30, 40, 0, 0, 0, 0}, {0, 70, 100, 20}, 0, 1, FX(0, 0, 24, 10), XP(P_GLIDE + 1, 10)},
    {"CHIP ARP", {15, 70, 60, 50, 0, 0, 0, 0}, {0, 70, 100, 30}, 0, 1, FX(0, 0, 26, 14)},
    {"KS GUITAR", {16, 20, 60, 70, 0, 0, 0, 0}, {0, 0, 127, 60}, 0, 1, FX(0, 0, 18, 30)},
    {"KS HARP", {16, 60, 80, 100, 0, 0, 0, 0}, {0, 0, 127, 80}, 0, 1, FX(0, 20, 20, 50)},
    {"MODAL BELL", {17, 90, 70, 90, 0, 0, 0, 0}, {0, 0, 127, 80}, 0, 0, FX(0, 0, 22, 50)},
    {"MODAL WOOD", {17, 10, 40, 40, 0, 0, 0, 0}, {0, 0, 127, 40}, 0, 0, FX(0, 0, 16, 26)},
    {"NOISE SWEEP", {18, 50, 70, 90, 60, 0, 0, 0}, {30, 90, 100, 70}, 0, 0, FX(0, 20, 24, 44), XP(P_LRATE + 1, 14, P_LD_FLT + 1, 50)},
    {"NOISE TONE", {18, 110, 50, 110, 30, 0, 0, 0}, {0, 70, 90, 40}, 0, 1, FX(0, 0, 20, 30)},
    {"SPEECH", {19, 20, 50, 50, 0, 0, 0, 0}, {0, 70, 110, 30}, 0, 1, FX(0, 0, 14, 20)},
    {"ROBOT WORDS", {19, 110, 40, 30, 0, 0, 0, 0}, {0, 70, 110, 30}, 0, 1, FX(0, 0, 18, 16)},
    {"PARTICLES", {20, 60, 70, 60, 0, 0, 0, 0}, {0, 0, 127, 70}, 0, 1, FX(0, 0, 20, 40)},
    {"DROPLETS", {20, 20, 40, 100, 0, 0, 0, 0}, {0, 0, 127, 80}, 0, 1, FX(0, 0, 24, 50)},
    {"PL KICK", {21, 50, 50, 60, 0, 0, 0, 0}, {0, 0, 127, 30}, 0, 0, FX(0, 0, 0, 6), XP(P_TRANS + 1, -24)},
    {"PL SNARE", {22, 60, 60, 50, 0, 0, 0, 0}, {0, 0, 127, 30}, 0, 0, FX(0, 0, 0, 10)},
    {"PL HIHAT", {23, 50, 70, 40, 0, 0, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 8)},
    /* MAIN MENU: added after the factory bank so every existing preset keeps its index (preset_trim.h,
     * golden.txt and cpu_baseline.txt are all indexed by position) -- new sounds append here, never inline. */
    /* Korg M1 "house piano" stab -- bright, slightly plastic, PCM-wavetable character; voiced as a 7th-chord
     * stab so it reads jazzy, not just a single bell hit. */
    {"M1 PIANO", {2, 30, 45, 40, 55, 0, 0, 0}, {0, 45, 0, 24}, 0, 0, FX(0, 20, 12, 22), XP(P_CHORD + 1, 2)},
    /* Roland D-50 "Fantasia"/"Staccato Heart" digital shimmer -- bright wavetable sweep, cold and airy, a 9th
     * chord for slower pads/progressions. */
    {"D50 FANTASIA", {2, 70, 55, 75, 25, 0, 0, 0}, {45, 85, 115, 75}, 0, 0, FX(0, 30, 16, 42), XP(P_CHORD + 1, 3)},
    /* Detroit string stab (Strings of Life) -- bright digital ensemble, short release, rhythmic and punchy,
     * a 7th chord. */
    {"DETROIT STAB", {3, 20, 35, 65, 85, 0, 0, 0}, {0, 35, 0, 20}, 0, 0, FX(0, 30, 14, 24), XP(P_CHORD + 1, 2)},
    /* chopped soul/jazz sample-chord feel -- narrow, slightly noisy spectral harmonics standing in for an old
     * record chop, a 7th chord (no sampler: memory limits ruled that out). */
    {"CHOP CHORD", {3, 10, 60, 30, 50, 0, 0, 0}, {0, 50, 20, 30}, 0, 0, FX(0, 10, 30, 36), XP(P_CHORD + 1, 2)},
    /* Robin S "Show Me Love" organ -- root-and-fifth (HARM 17), held at the organ end of MRPH, close inversion
     * (low TIMB) so it stays hollow and punchy, almost no filter movement. Tagged BK_BASS: it's played as the
     * bass/ostinato, not a stacked chord -- the fifth is baked into the patch, not CHORD mode. */
    {"SHOWMELOVE", {4, 17, 20, 88, 0, 0, 0, 0}, {0, 50, 85, 20}, 0, 0, FX(0, 8, 8, 20)},
    /* a DX-style FM electric piano (2-op: HARM the partial ratio, TIMB the mod index -- the classic EP "bark"
     * on attack that settles as ENV closes TIMB back down). CHORD on at 9TH: one key plays the dusty minor-9th
     * voicing the reference calls for, the module stacking the real scale intervals (not baked in). */
    {"DX EP 9TH", {8, 68, 42, 50, 68, 0, 0, 0}, {0, 42, 68, 32}, 0, 0, FX(0, 18, 14, 26), XP(P_CHORD + 1, 3)},
    /* a staccato FM EP stab into a dotted delay -- same voice family as DX EP 9TH, shorter and drier so the
     * delay (not the patch) does the staccato bounce; a 7th chord, not the 9th. */
    {"RHODES STAB", {8, 65, 38, 45, 60, 0, 0, 0}, {0, 25, 20, 20}, 0, 0, FX(0, 15, 55, 30), XP(P_CHORD + 1, 2)},
    /* a warm, soft jazz-house electric piano (Wurli/Rhodes 7th & 9th chords, lots of room reverb) -- lower FM
     * ratio and mod index than DX EP 9TH so it stays round, not clangy. */
    {"JAZZY EP", {8, 45, 30, 55, 50, 0, 0, 0}, {10, 55, 70, 60}, 0, 0, FX(0, 10, 10, 55), XP(P_CHORD + 1, 3)},
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
