/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID MAX: the 303 voice of eng_acid.c (acid303.c: Open303's ladder, accent and slide) with six oscillator shapes, five
 * wavetables, a slow MOTION that sweeps them, and a tone stage after the filter: BUZZ, DRIVE (six kinds), CHARACTER (six
 * modes, BASS / BRIGHT, DRIFT), NOTCH MOTION, HI-END and COMPRESSION. One note at a time, as the 303: a step's ACCENT (or a
 * key played hard) is the accent, a step's SLIDE the slide.
 *
 * Its knobs: EDIT 1 / 2 hold the eight that matter most; the rest are the engine's own bytes (track_t.ax, AX_*), in pages
 * of four (engine_t.ax). The chorus, delay and reverb are the tracks' sends into the shared buses (FX page).
 *   EDIT 1   WAVE (SAW SQR MORPH FOLD FORM, WT:CL .. WT:DG = the five wavetables), POS, MOTION, CUT
 *   EDIT 2   RES, ENV (how far the envelope opens the filter), DEC, ACC
 *   GLIDE    SLIDE time, MODE (CLASSIC / ENHANCED), ATK (the amplitude's attack), FINE (cents)
 *   DRIVE    DRV, TYPE (SOFT TUBE HARD FOLD FUZZ RAT), TONE, MIX
 *   HI-END   AMT, CLAR, PLUCK, AIR
 *   BUZZ     AMT, TYPE (SINE NOISE WIDE), FREQ, GATE
 *   NOTCH    AMT, FREQ, RATE, SYNC (a sweep, with the tempo or free)
 *   CHARACT  MODE (CLEAN CLASSIC DIRTY ACID WIDE DIGITAL), BASS, BRIGHT, DRIFT
 *   COMP     AMT, TONE, MIX, OUT
 * Float, on the AC79's FPU (tools/build.py). eng_acid.c (acid303.c) comes first in engines.c. */

enum {
    AX_SLIDE, AX_GMODE, AX_ATK, AX_FINE,
    AX_DRV, AX_DTYPE, AX_DTONE, AX_DMIX,
    AX_HAMT, AX_CLAR, AX_PLUCK, AX_AIR,
    AX_BZAMT, AX_BZTYPE, AX_BZFREQ, AX_BZGATE,
    AX_NAMT, AX_NFREQ, AX_NRATE, AX_NSYNC,
    AX_CMODE, AX_CBASS, AX_CBRIGHT, AX_CDRIFT,
    AX_PAMT, AX_PTONE, AX_PMIX, AX_POUT,
    AX_N
};
enum { DT_SOFT, DT_TUBE, DT_HARD, DT_FOLD, DT_FUZZ, DT_RAT, DT_N };
enum { CM_CLEAN, CM_CLASSIC, CM_DIRTY, CM_ACID, CM_WIDE, CM_DIGITAL, CM_N };
enum { BZ_SINE, BZ_NOISE, BZ_WIDE, BZ_N };

typedef struct { float z; } am_lp_t;                  /* a one-pole low-pass */
typedef struct {
    bass303_t b;
    uint8_t init, trig, acc, slide, had_note;
    int8_t pot[8];                                    /* the values last given to the 303 (-1: not yet) */
    int8_t ax[AX_N];                                  /* .. and the engine's own (-1: not yet) */
    int32_t last_p16;                                 /* the pitch of the last note (ENHANCED slides take longer for a leap) */
    float atk_pos, atk_len;                           /* the amplitude's attack: samples in, samples long */
    float zgate;                                      /* the gate as a smooth 0..1 (BUZZ GATE) */
    float bz_ph, bz_ph2;                              /* BUZZ: the sine's, the second saw's phase */
    uint32_t rnd;                                     /* noise (BUZZ NOISE, DRIFT) */
    float bz_lp;
    float drift, drift_t;                             /* DRIFT: a slow random walk, cents / 100 */
    am_lp_t dlp;                                      /* DRIVE: the tone's low-pass */
    float dc_x, dc_y;                                 /* DRIVE: a DC blocker after the asymmetric kinds */
    am_lp_t clo, chi, cmid;                           /* CHARACTER: the bass shelf, the bright shelf, the ACID mid band */
    float dly[32];                                    /* CHARACTER WIDE: a 0.6 ms comb */
    uint32_t dw;
    float crush_h;                                    /* CHARACTER DIGITAL: sample and hold */
    uint32_t crush_c;
    float nl, nb;                                     /* NOTCH: the state-variable filter's two integrators */
    float nph;                                        /* .. and its sweep's phase, turns */
    am_lp_t hhp, hair;                                /* HI-END: the 3 kHz and 9 kHz splits */
    float hf_fast, hf_slow;                           /* HI-END: the band's two envelope followers */
    float cenv, cgain;                                /* COMP: the detector, the gain */
    am_lp_t csc;                                      /* COMP: the sidechain's high-pass (low-pass subtracted) */
} acidmax_slot_t;

static const char *const N_AM_WAVE[] = {"SAW", "SQR", "MORPH", "FOLD", "FORM", "WT:CL", "WT:SY", "WT:VO", "WT:FM", "WT:DG"};
static const char *const N_AM_GMODE[] = {"CLASS", "ENHNC"};
static const char *const N_AM_DTYPE[] = {"SOFT", "TUBE", "HARD", "FOLD", "FUZZ", "RAT"};
static const char *const N_AM_BZTYPE[] = {"SINE", "NOISE", "WIDE"};
static const char *const N_AM_CMODE[] = {"CLEAN", "CLASS", "DIRTY", "ACID", "WIDE", "DIGI"};
static const char *const N_AM_SYNC[] = {"FREE", "SYNC"};
static const char *const N_AM_GATE[] = {"HOLD", "NOTE"};

static float am_lp(am_lp_t *f, float x, float a)       /* y += a (x - y) */
{
    f->z += a * (x - f->z);
    return f->z;
}
static float am_a(float hz) { return 1.0f - fm_expf(-FM_TWO_PI * hz / BASS303_SR); }   /* the one-pole's coefficient for a corner */
static float am_rnd(acidmax_slot_t *A)                 /* -1 .. 1 */
{
    uint32_t x = A->rnd ? A->rnd : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    A->rnd = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static acidmax_slot_t *acidmax_slot(track_t *t)
{
    acidmax_slot_t *A;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    A = (acidmax_slot_t *)eng_arena_of(t, ENGI_ACIDMAX);
    if (!A)
        return 0;
    if (!A->init) {                                   /* (a fresh arena is all 0) */
        uint32_t k;
        bass303_init(&A->b);
        for (k = 0; k < 8u; k++)
            A->pot[k] = -1;
        for (k = 0; k < AX_N; k++)
            A->ax[k] = -1;
        A->rnd = 0x9E3779B9u + (uint32_t)(t - trk) * 0x85EBCA6Bu;
        A->cgain = 1.0f;
        A->init = 1;
    }
    return A;
}

static void acidmax_note_on(track_t *t, voice_t *v)
{
    acidmax_slot_t *A = acidmax_slot(t);
    if (!A || v != &t->v[0])
        return;
    A->trig = 1;                                      /* (the 303 is told in the next render: it needs the pitch) */
    A->acc = v->vel >= 120u;
    A->slide = t->slide_glide && A->b.gate;           /* a step's SLIDE into this one, the last note still held */
}

/* what a block of the 303's output goes through after the filter. a: 0 .. 1 amounts from the track's bytes */
static float am_sat(int type, float x)
{
    switch (type) {
    case DT_TUBE:                                     /* even harmonics: a bent tanh */
        return fm_tanhf(x + 0.25f * x * fm_fabsf(x)) - 0.12f;
    case DT_HARD:
        return fm_clampf(x, -1.0f, 1.0f);
    case DT_FOLD:                                     /* a sine fold */
        return fm_sinf(1.5707963f * x);
    case DT_FUZZ:                                     /* asymmetric, gated by the squaring */
        return fm_tanhf(3.0f * (x > 0.0f ? x : 0.55f * x)) * 0.8f;
    case DT_RAT:                                      /* a diode pair: soft knee, uneven */
        return x > 0.0f ? x / (1.0f + 1.6f * x) * 1.6f : x / (1.0f - 0.9f * x) * 1.1f;
    default:
        return fm_tanhf(x);
    }
}

static void acidmax_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint8_t POT[8] = {BASS303_WAVE, BASS303_POS, BASS303_MOTION, BASS303_CUTOFF,
                                   BASS303_RESO, BASS303_ENVMOD, BASS303_DECAY, BASS303_ACCENT};
    acidmax_slot_t *A = acidmax_slot(t);
    const int8_t *ax;
    float y[CTL], f;
    uint32_t i, k;
    float drv, dmix, dtone_a, dtone, hamt, clar, pluck, air, bz, bzf, nam, nrate, nf0, cbass, cbright, pamt, pmix, pout, ptone;
    int dtype, cmode, bztype, bzgate, nsync;
    float bars, hz;
    if (!A || v != &t->v[0])
        return;
    if (n > CTL)
        n = CTL;
    ax = t->ax;
    for (k = 0; k < 8u; k++) {                        /* the knobs, when they move */
        int32_t p = clamp(t->p[P_E0 + k], 0, k ? 127 : 9);
        if (p != A->pot[k]) {
            bass303_set(&A->b, POT[k], p);
            A->pot[k] = (int8_t)p;
        }
    }
    if (ax[AX_SLIDE] != A->ax[AX_SLIDE]) {
        bass303_set(&A->b, BASS303_SLIDE, clamp(ax[AX_SLIDE], 0, 127));
        A->ax[AX_SLIDE] = ax[AX_SLIDE];
    }
    if (A->b.drv_type != BASS303_DRV_OFF)             /* (the drive is the engine's own, after the tone stages) */
        bass303_set(&A->b, BASS303_DRVTYPE, BASS303_DRV_OFF);
    if (A->trig) {
        int slide = A->slide;
        if (slide && clamp(ax[AX_GMODE], 0, 1)) {     /* ENHANCED: a leap takes longer than a step (time scales with the interval) */
            int32_t d = (m->pitch16 - A->last_p16) >> 4;
            float sc = 0.6f + 0.12f * (float)(d < 0 ? -d : d);
            bass303_set(&A->b, BASS303_SLIDE, clamp((int32_t)((float)clamp(ax[AX_SLIDE], 0, 127) * sc), 0, 127));
            A->ax[AX_SLIDE] = -1;                     /* (back to the knob's value at the next plain note) */
        }
        bass303_note_on(&A->b, (m->pitch16 + 8) >> 4, A->acc, slide);
        A->last_p16 = m->pitch16;
        A->atk_pos = 0.0f;
        A->atk_len = (float)clamp(ax[AX_ATK], 0, 127) * (0.05f * BASS303_SR / 127.0f);   /* up to 50 ms */
        A->trig = 0;
        A->had_note = 1;
    } else if (!v->gate && A->b.gate) {
        bass303_note_off(&A->b);
    }
    /* DRIFT: a slow random walk, +- (DRIFT * 25) cents */
    A->drift_t += 0.0004f * (am_rnd(A) - A->drift_t);
    A->drift += 0.02f * (A->drift_t - A->drift);
    /* the track's pitch, finer than a note: transpose, glide, bend, LFO; FINE (cents), DRIFT */
    f = A->b.tuning * fm_exp2f(((float)m->pitch16 * (1.0f / 16.0f) - 69.0f) * (1.0f / 12.0f)
                               + ((float)(clamp(ax[AX_FINE], -64, 63)) + A->drift * (float)clamp(ax[AX_CDRIFT], 0, 127) * (0.25f / 12.0f * 3.0f))
                                     * (1.0f / 1200.0f));
    A->b.osc_freq = f;
    bass303_render(&A->b, y, (int)n);

    drv = (float)clamp(ax[AX_DRV], 0, 127) * (1.0f / 127.0f);
    dtype = clamp(ax[AX_DTYPE], 0, DT_N - 1);
    dtone = ((float)clamp(ax[AX_DTONE], 0, 127) - 64.0f) * (1.0f / 64.0f);
    dtone_a = am_a(1200.0f);
    dmix = (float)clamp(ax[AX_DMIX], 0, 127) * (1.0f / 127.0f);
    hamt = (float)clamp(ax[AX_HAMT], 0, 127) * (1.0f / 127.0f);
    clar = (float)clamp(ax[AX_CLAR], 0, 127) * (1.0f / 127.0f);
    pluck = (float)clamp(ax[AX_PLUCK], 0, 127) * (1.0f / 127.0f);
    air = (float)clamp(ax[AX_AIR], 0, 127) * (1.0f / 127.0f);
    bz = (float)clamp(ax[AX_BZAMT], 0, 127) * (1.0f / 127.0f);
    bztype = clamp(ax[AX_BZTYPE], 0, BZ_N - 1);
    bzgate = clamp(ax[AX_BZGATE], 0, 1);
    bzf = 30.0f * fm_exp2f((float)clamp(ax[AX_BZFREQ], 0, 127) * (3.7f / 127.0f));   /* 30 .. 390 Hz */
    nam = (float)clamp(ax[AX_NAMT], 0, 127) * (1.0f / 127.0f);
    nsync = clamp(ax[AX_NSYNC], 0, 1);
    nf0 = 300.0f * fm_exp2f((float)clamp(ax[AX_NFREQ], 0, 127) * (4.0f / 127.0f));   /* the sweep starts at 300 .. 4.8 kHz */
    if (nsync) {                                      /* a sweep of 8, 4, 2, 1, 1/2, 1/4 bars: with the tempo */
        static const float BARS[6] = {8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f};
        bars = BARS[clamp(ax[AX_NRATE], 0, 127) * 6 / 128];
        hz = (float)clamp(song.g[G_BPM], 20, 300) * (1.0f / 240.0f) / bars;
    } else {
        hz = 0.05f * fm_exp2f((float)clamp(ax[AX_NRATE], 0, 127) * (8.0f / 127.0f));   /* 0.05 .. 12.8 Hz */
    }
    nrate = hz * (1.0f / BASS303_SR);
    cmode = clamp(ax[AX_CMODE], 0, CM_N - 1);
    cbass = ((float)clamp(ax[AX_CBASS], 0, 127) - 64.0f) * (1.0f / 64.0f);
    cbright = ((float)clamp(ax[AX_CBRIGHT], 0, 127) - 64.0f) * (1.0f / 64.0f);
    pamt = (float)clamp(ax[AX_PAMT], 0, 127) * (1.0f / 127.0f);
    ptone = (float)clamp(ax[AX_PTONE], 0, 127) * (1.0f / 127.0f);
    pmix = (float)clamp(ax[AX_PMIX], 0, 127) * (1.0f / 127.0f);
    pout = fm_db2lin(((float)clamp(ax[AX_POUT], 0, 127) - 64.0f) * (12.0f / 64.0f));   /* -12 .. +12 dB */

    {
        const float a_hp3 = am_a(3000.0f), a_air = am_a(9000.0f), a_bz = am_a(bzf * 4.0f), a_lo = am_a(160.0f),
                    a_hi = am_a(3500.0f), a_mid = am_a(1100.0f), a_csc = am_a(180.0f + 900.0f * ptone);
        const float g_t = fm_tanf(FM_PI * fm_minf(nf0, 12000.0f) / BASS303_SR);
        const float gate_a = A->b.gate ? 0.004f : 0.0007f;
        for (i = 0; i < n; i++) {
            float x = y[i], w, s, e;
            /* the attack of the amplitude */
            if (A->atk_pos < A->atk_len) {
                x *= A->atk_pos / A->atk_len;
                A->atk_pos += 1.0f;
            }
            A->zgate += gate_a * ((A->b.gate ? 1.0f : 0.0f) - A->zgate);
            /* BUZZ: a layer under the line (the sine, a low-passed noise, or two saws a hair apart), gated by the note */
            if (bz > 0.0f && A->had_note) {
                float g = bzgate ? A->zgate : 1.0f;
                if (bztype == BZ_SINE) {
                    A->bz_ph += bzf * (1.0f / BASS303_SR);
                    A->bz_ph -= fm_floorf(A->bz_ph);
                    w = fm_sinf(A->bz_ph * FM_TWO_PI);
                } else if (bztype == BZ_NOISE) {
                    A->bz_lp += a_bz * (am_rnd(A) - A->bz_lp);
                    w = A->bz_lp * 3.0f;
                } else {
                    A->bz_ph += bzf * (1.0f / BASS303_SR);
                    A->bz_ph -= fm_floorf(A->bz_ph);
                    A->bz_ph2 += bzf * 1.013f * (1.0f / BASS303_SR);
                    A->bz_ph2 -= fm_floorf(A->bz_ph2);
                    A->bz_lp += a_bz * 2.0f * (A->bz_ph + A->bz_ph2 - 1.0f - A->bz_lp);
                    w = A->bz_lp * 1.6f;
                }
                x += bz * 0.18f * g * w;
            }
            /* DRIVE: the clipper, a DC blocker, TONE (above 64 brighter, below darker), MIX with what went in */
            if (drv > 0.0f) {
                float gain = 1.0f + 14.0f * drv * drv, cmp = 1.0f / (0.55f + 0.45f * fm_tanhf(gain));
                s = am_sat(dtype, x * gain) * cmp;
                e = s - A->dc_x + 0.995f * A->dc_y;   /* (a one-pole high-pass, 35 Hz) */
                A->dc_x = s;
                A->dc_y = e;
                s = e;
                w = am_lp(&A->dlp, s, dtone_a);
                s += dtone > 0.0f ? dtone * 0.8f * (s - w) : -dtone * 0.9f * (w - s);
                x += dmix * (s - x);
            }
            /* CHARACTER */
            switch (cmode) {
            case CM_CLASSIC:
                x = fm_tanhf(1.25f * x) * 0.9f;
                break;
            case CM_DIRTY:
                x = am_sat(DT_TUBE, 2.2f * x) * 0.7f + 0.04f * am_rnd(A) * fm_fabsf(x);
                break;
            case CM_ACID: {                           /* a mid band pushed into a soft clip */
                float mid = am_lp(&A->cmid, x, a_mid);
                x = fm_tanhf(1.1f * (x + 0.8f * mid)) * 0.85f;
                break;
            }
            case CM_WIDE:                             /* a 0.6 ms comb: the mono sum of a small spread */
                A->dly[A->dw & 31u] = x;
                x = 0.74f * x + 0.34f * A->dly[(A->dw - 26u) & 31u];
                A->dw++;
                break;
            case CM_DIGITAL:                          /* half the rate, eleven bits */
                if (!(A->crush_c++ & 1u))
                    A->crush_h = fm_floorf(x * 1024.0f + 0.5f) * (1.0f / 1024.0f);
                x = A->crush_h;
                break;
            default:
                break;
            }
            if (cbass != 0.0f || cbright != 0.0f) {   /* BASS / BRIGHT: shelves, +- 9 dB around 160 Hz and 3.5 kHz */
                float lo = am_lp(&A->clo, x, a_lo), hi = x - am_lp(&A->chi, x, a_hi);
                x += cbass * 1.8f * lo + cbright * 1.4f * hi;
            }
            /* NOTCH MOTION: a state-variable notch sweeping up from NF to 3 octaves above, once a cycle (resonant) */
            if (nam > 0.0f) {
                float fr = g_t * (1.0f + 7.0f * A->nph * A->nph), q = 0.28f, lpv, bpv, hpv, notch;
                float g1 = fr / (1.0f + fr * (fr + q));
                A->nph += nrate;
                if (A->nph >= 1.0f)
                    A->nph -= 1.0f;
                hpv = (x - (q + fr) * A->nb - A->nl) * (1.0f / (1.0f + fr * (q + fr)));
                bpv = hpv * fr + A->nb;
                lpv = bpv * fr + A->nl;
                A->nb = bpv * fr + bpv;
                A->nl = lpv * fr + lpv;
                notch = x - q * bpv * 0.0f - bpv * 2.0f * (q * 0.5f) / q * 0.0f;
                (void)g1;
                notch = hpv + lpv;
                x += nam * ((notch + 0.6f * bpv) - x);
            }
            /* HI-END: the band above 3 kHz, its attack lifted (PLUCK), the band lifted (CLAR), its top (AIR) */
            if (hamt > 0.0f) {
                float hp = x - am_lp(&A->hhp, x, a_hp3), top = x - am_lp(&A->hair, x, a_air), ab = fm_fabsf(hp), tr, g;
                A->hf_fast += (ab > A->hf_fast ? 0.30f : 0.004f) * (ab - A->hf_fast);
                A->hf_slow += 0.0008f * (ab - A->hf_slow);
                tr = fm_maxf(A->hf_fast - A->hf_slow, 0.0f) / (A->hf_slow + 0.01f);
                g = 1.0f + clar * 1.6f + pluck * fm_minf(tr, 3.0f) * 0.9f;
                g /= 1.0f + 3.0f * A->hf_slow * (1.0f + clar);      /* (the band compressed as it is lifted) */
                x += hamt * (hp * (g - 1.0f) + air * 0.9f * top);
            }
            /* COMPRESSION */
            if (pamt > 0.0f) {
                float sc = x - am_lp(&A->csc, x, a_csc), ab = fm_fabsf(sc), thr = 0.45f - 0.38f * pamt, gt, c;
                A->cenv += (ab > A->cenv ? 0.02f : 0.0004f) * (ab - A->cenv);
                gt = A->cenv > thr ? (thr + (A->cenv - thr) * 0.25f) / A->cenv : 1.0f;
                A->cgain += (gt < A->cgain ? 0.05f : 0.0008f) * (gt - A->cgain);
                c = x * A->cgain * (1.0f + 1.4f * pamt * (1.0f - thr));
                x = x + pmix * (c - x);
            }
            y[i] = x * pout;
        }
        A->bz_lp = fm_flush(A->bz_lp);
        A->nl = fm_flush(A->nl);
        A->nb = fm_flush(A->nb);
    }
    for (i = 0; i < n; i++) {
        float s = y[i] * 96000.0f;                    /* (the 303 peaks near 0.5 on an accent) */
        int32_t q = s > 60000.0f ? 60000 : s < -60000.0f ? -60000 : (int32_t)s;
        out[i] += voice_amp(q, m, i) << 1;
    }
}

/* the engine's own parameters, four to a page (core.h engine_t.ax) */
#define AXP(l, a, b, c) {l, F_PCT, a, b, c, 0, 0}
#define AXE(l, names, def) {l, F_ENUM, 0, (int16_t)(NELEM(names) - 1u), def, names, 0}
static const param_desc_t AM_AX[AX_N] = {
    AXP("SLIDE", 0, 127, 21), AXE("MODE", N_AM_GMODE, 0), AXP("ATK", 0, 127, 0), {"FINE", F_INT, -64, 63, 0, 0, 0},
    AXP("DRV", 0, 127, 0), AXE("TYPE", N_AM_DTYPE, DT_SOFT), AXP("TONE", 0, 127, 64), AXP("MIX", 0, 127, 127),
    AXP("AMT", 0, 127, 0), AXP("CLAR", 0, 127, 90), AXP("PLUCK", 0, 127, 90), AXP("AIR", 0, 127, 100),
    AXP("AMT", 0, 127, 0), AXE("TYPE", N_AM_BZTYPE, BZ_SINE), AXP("FREQ", 0, 127, 40), AXE("GATE", N_AM_GATE, 1),
    AXP("AMT", 0, 127, 0), AXP("FREQ", 0, 127, 70), AXP("RATE", 0, 127, 30), AXE("SYNC", N_AM_SYNC, 1),
    AXE("MODE", N_AM_CMODE, CM_CLASSIC), AXP("BASS", 0, 127, 64), AXP("BRIGHT", 0, 127, 64), AXP("DRIFT", 0, 127, 0),
    AXP("AMT", 0, 127, 0), AXP("TONE", 0, 127, 64), AXP("MIX", 0, 127, 127), AXP("OUT", 0, 127, 64),
};
static const char *const AM_AX_TITLE[] = {"GLIDE", "DRIVE", "HI-END", "BUZZ", "NOTCH", "CHARACT", "COMP"};

/* a preset's engine bytes, in AX_ order: SLIDE GMODE ATK FINE | DRV DTYPE DTONE DMIX | HAMT CLAR PLUCK AIR |
 * BZAMT BZTYPE BZFREQ BZGATE | NAMT NFREQ NRATE NSYNC | CMODE CBASS CBRIGHT CDRIFT | PAMT PTONE PMIX POUT */
#define AXV(...) (const int8_t[AX_N]){__VA_ARGS__}
#define AXD AXV(21, 0, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, 1, 64, 64, 0, 0, 64, 127, 64)

static const preset_t ACIDMAX_PRESETS[] = {
    /* name, {WAVE, POS, MOTION, CUT, RES, ENV, DEC, ACC}, {A D S R}, fenv, mono, FX, ... ax */
    {"303 FOREVER", {0, 0, 0, 40, 100, 80, 50, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8),
     .ax = AXV(21, 0, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, 1, 64, 64, 0, 0, 64, 127, 64)},
    {"SQUARE BITE", {1, 0, 0, 50, 90, 70, 60, 80}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8), .ax = AXD},
    {"CHROME SQUELCH", {0, 0, 0, 30, 120, 110, 30, 110}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 14, 10),
     .ax = AXV(15, 0, 0, 0, 30, DT_SOFT, 64, 90, 40, 100, 100, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 70, 70, 0, 30, 64, 100, 64)},
    {"DEEP DUB ACID", {0, 0, 0, 20, 60, 40, 80, 60}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12),
     .ax = AXV(30, 0, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 35, BZ_SINE, 5, 1, 0, 70, 30, 1, CM_CLASSIC, 100, 50, 0, 40, 40, 127, 64)},
    {"RUBBER LINE", {1, 0, 0, 30, 70, 60, 90, 70}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 8, 8), .ax = AXD},
    {"MORPH PULSE", {2, 40, 0, 45, 85, 70, 55, 85}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8), .ax = AXD},
    {"MORPH SWEEP", {2, 20, 70, 40, 90, 80, 55, 85}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 12, 10), .ax = AXD},
    {"FOLD BITE", {3, 50, 0, 45, 70, 60, 45, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8),
     .ax = AXV(21, 0, 0, 0, 20, DT_FOLD, 64, 80, 30, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 64, 70, 0, 20, 64, 127, 64)},
    {"FOLD DRIVE", {3, 90, 40, 50, 60, 55, 40, 95}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 8, 8),
     .ax = AXV(21, 0, 0, 0, 50, DT_FOLD, 70, 100, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_DIRTY, 70, 60, 20, 0, 64, 127, 64)},
    {"VOWEL ACID", {4, 40, 0, 55, 70, 70, 50, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 12, 12), .ax = AXD},
    {"VOWEL TALK", {4, 25, 80, 50, 80, 75, 45, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 14, 12),
     .ax = AXV(25, 1, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 40, 60, 40, 1, CM_ACID, 64, 80, 0, 25, 64, 100, 64)},
    {"WT CLASSIC", {5, 30, 40, 45, 80, 70, 55, 85}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 10), .ax = AXD},
    {"WT SWEEP", {5, 10, 100, 40, 90, 85, 60, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 12, 12),
     .ax = AXV(21, 1, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 64, 64, 0, 0, 64, 127, 64)},
    {"SYNC SCREAM", {6, 60, 0, 55, 70, 80, 45, 100}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 8, 8),
     .ax = AXV(21, 0, 0, 0, 40, DT_HARD, 64, 90, 40, 110, 100, 110, 0, 0, 40, 1, 0, 70, 30, 1, CM_DIRTY, 64, 70, 0, 45, 64, 100, 64)},
    {"SYNC RIDE", {6, 20, 90, 45, 80, 70, 55, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 10), .ax = AXD},
    {"VOCAL OOH EEE", {7, 0, 120, 50, 70, 60, 55, 85}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 12, 14),
     .ax = AXV(30, 1, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 64, 64, 0, 0, 64, 127, 64)},
    {"VOCAL ACID", {7, 70, 0, 55, 75, 80, 45, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 12, 12), .ax = AXD},
    {"FM BELL BASS", {8, 80, 0, 60, 40, 40, 70, 70}, {0, 0, 127, 10}, 0, 1, FX(0, 0, 14, 16),
     .ax = AXV(21, 0, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLEAN, 64, 70, 0, 0, 64, 127, 64)},
    {"FM SOFT SUB", {8, 5, 0, 20, 30, 20, 90, 50}, {0, 0, 127, 10}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -12), .ax = AXD},
    {"DIGI GRIT", {9, 40, 0, 50, 70, 70, 50, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8),
     .ax = AXV(21, 0, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_DIGITAL, 64, 70, 0, 0, 64, 127, 64)},
    {"DIGI STEPS", {9, 80, 60, 55, 60, 60, 45, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 8, 8), .ax = AXD},
    {"BUZZ BOX", {0, 0, 0, 35, 80, 60, 50, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 8, 8),
     .ax = AXV(21, 0, 0, 0, 20, DT_TUBE, 64, 90, 0, 90, 90, 100, 70, BZ_WIDE, 30, 1, 0, 70, 30, 1, CM_CLASSIC, 80, 60, 0, 25, 64, 100, 64)},
    {"NOISE ACID", {0, 0, 0, 45, 90, 80, 45, 100}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 8, 8),
     .ax = AXV(21, 0, 0, 0, 30, DT_FUZZ, 64, 80, 0, 90, 90, 100, 45, BZ_NOISE, 80, 1, 0, 70, 30, 1, CM_DIRTY, 64, 80, 20, 20, 64, 100, 64)},
    {"NOTCH TECHNO", {0, 0, 0, 40, 100, 90, 45, 100}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8),
     .ax = AXV(21, 0, 0, 0, 25, DT_SOFT, 64, 90, 40, 90, 90, 100, 0, 0, 40, 1, 85, 60, 40, 1, CM_CLASSIC, 64, 64, 0, 30, 64, 100, 64)},
    {"NOTCH WASH", {5, 40, 40, 50, 80, 70, 55, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 14, 16),
     .ax = AXV(21, 0, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 100, 40, 70, 1, CM_CLASSIC, 64, 64, 0, 0, 64, 127, 64)},
    {"WIDE ACID", {0, 0, 0, 40, 90, 80, 50, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 10, 10, 10),
     .ax = AXV(21, 0, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_WIDE, 64, 64, 0, 0, 64, 127, 64)},
    {"HI-END SHINE", {0, 0, 0, 55, 80, 70, 40, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8),
     .ax = AXV(21, 0, 0, 0, 0, 0, 64, 127, 100, 110, 100, 110, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 64, 80, 0, 0, 64, 127, 64)},
    {"PUMP COMP", {0, 0, 0, 40, 90, 80, 60, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 8, 8),
     .ax = AXV(21, 0, 0, 0, 15, DT_SOFT, 64, 100, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 70, 64, 0, 90, 64, 127, 70)},
    {"RAT FUZZ", {1, 0, 0, 45, 80, 60, 50, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 8, 8),
     .ax = AXV(21, 0, 0, 0, 80, DT_RAT, 70, 110, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_DIRTY, 70, 60, 0, 30, 64, 100, 64)},
    {"TUBE WARM", {0, 0, 0, 35, 70, 50, 60, 70}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 8, 8),
     .ax = AXV(21, 0, 0, 0, 45, DT_TUBE, 60, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 90, 50, 0, 20, 64, 100, 64)},
    {"SLIDE MACHINE", {0, 0, 0, 40, 100, 80, 50, 90}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 10, 8),
     .ax = AXV(70, 1, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 64, 64, 0, 0, 64, 127, 64)},
    {"SOFT ATTACK", {2, 10, 0, 30, 60, 40, 70, 50}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 10, 12),
     .ax = AXV(40, 0, 60, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 64, 64, 0, 0, 64, 127, 64)},
    {"DRIFT LEAD", {5, 50, 30, 60, 60, 60, 60, 80}, {0, 0, 127, 8}, 0, 1, FX(0, 12, 14, 16),
     .ax = AXV(30, 0, 0, 0, 0, 0, 64, 127, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 64, 70, 70, 0, 64, 127, 64)},
    {"MC202", {1, 0, 0, 55, 75, 60, 55, 75}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 8, 8),
     .ax = AXV(25, 0, 0, 0, 30, DT_SOFT, 64, 100, 0, 90, 90, 100, 0, 0, 40, 1, 0, 70, 30, 1, CM_CLASSIC, 70, 64, 0, 0, 64, 127, 64)},
};

static const engine_t ENG_ACIDMAX = {
    .name = "ACIDMAX",
    .page_title = {"OSC", "FILTER"},
    .edit = {
        {"WAVE", F_ENUM, 0, 9, 0, N_AM_WAVE, 0},
        {"POS", F_PCT, 0, 127, 0, 0, 0},
        {"MOTN", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_PCT, 0, 127, 64, 0, 0},
        {"RES", F_PCT, 0, 127, 64, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"DEC", F_PCT, 0, 127, 64, 0, 0},
        {"ACC", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = ACIDMAX_PRESETS,
    .npresets = NELEM(ACIDMAX_PRESETS),
    .note_on = acidmax_note_on,
    .render = acidmax_render,
    .fil_page = 1,
    .color = 0xFFE0,
    .macro = {P_E3, P_E4, P_E5, P_E1},
    .poly = 1,
    .nax = AX_N,
    .ax = AM_AX,
    .ax_title = AM_AX_TITLE,
};
