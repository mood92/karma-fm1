/* SPDX-License-Identifier: GPL-3.0-only */
/* The TB-303 of X0X (github.com/charlesvestal/fm1-x0x, GPL-3.0, Charles Vestal: firmware/src/dsp/bass303.c, .h,
 * fastmath.h, x0x_param.h), copied as they are into one file for SLOOP's single translation unit: its static
 * helpers carry a b3_ prefix and its functions are static. X0X's port is of Open303 (Robin Schmidt, MIT) with the
 * Devilfish ranges and the Soft / RAT drive of schwung-303 (GPL-3.0; the RAT model from davemollen/dm-Rat,
 * GPL-3.0). Float: the build turns on the AC79's single-precision FPU (tools/build.py). Used by eng_acid.c. */
/* X0X parameter descriptors, shared by every sound engine.
 *
 * Every user-facing sound parameter is a "pot": an integer 0..max (max is 127 for a
 * continuous control, n-1 for an n-way switch), exactly like 9W9's panel. The UI
 * draws and edits them from these descriptors alone, and a project stores the
 * integers, so a saved sound is engine-version independent as long as a pot keeps
 * its meaning. */
#include <stdint.h>

/* X0X's pan: a balance law, pot 0..127, 64 = centre. Centre is 1 on both sides exactly (a centred
 * mix is the mono one, bit for bit); toward a side the other fades along a quarter cosine and the
 * near side stays at 1. cosine: a 5-term even polynomial, |error| < 2e-4 over the quarter turn. */
static inline void x0x_pan_gains(int pot, float *l, float *r)
{
    float q = pot >= 64 ? (float)(pot - 64) / 63.0f : (float)(pot - 64) / 64.0f, x, c;
    x = q * 1.5707963f;
    x *= x;
    c = 1.0f + x * (-0.5f + x * (0.041666667f + x * (-0.0013888889f + x * 0.0000248016f)));
    if (pot <= 0 || pot >= 127)
        c = 0.0f;                                 /* hard over: nothing on the far side, exactly */
    *l = q > 0.0f ? c : 1.0f;
    *r = q < 0.0f ? c : 1.0f;
}

/* a pan gain gliding toward its target over a block of n samples (one pole, ~10 ms): returns the
 * per-sample step for a linear ramp from *cur, and moves *cur to where the block ends. Settled, the
 * step is 0 and *cur is the target exactly */
static inline float x0x_glide_block(float *cur, float tgt, int n)
{
    float c = *cur, e;
    if (c == tgt)
        return 0.0f;
    e = (tgt - c) * (n >= 441 ? 1.0f : (float)n * (1.0f / 441.0f));   /* ~1 - exp(-n / 441) for small n */
    if (tgt - (c + e) < 1e-5f && (c + e) - tgt < 1e-5f)
        e = tgt - c;
    *cur = c + e;
    return e / (float)n;
}

typedef struct {
    const char *name;                 /* <= 6 characters: drawn above a knob */
    uint8_t max;                      /* 127 = continuous; else switch with max+1 positions */
    uint8_t def;                      /* power-on value */
    const char *const *names;         /* switch: max+1 labels (<= 5 chars), else 0 */
} x0x_param_t;

/* X0X single-precision maths, with no libm.
 *
 * The FM-1's FPU is float-only (-mfprev1) and the firmware links no libm, so this
 * is the whole maths library of the DSP. The same code runs in the host build,
 * which is what lets a host render stand in for the device: build the host with
 * -ffp-contract=off (the device has no fused multiply-add) and the two compute
 * the same samples.
 *
 * Accuracy (tests/host/fastmath_test.c checks every bound against libm):
 *   fm_exp2f  rel 3e-7 on [-126, 127]      fm_expf  rel 6e-7 on [-87, 88]
 *   fm_log2f  abs 3e-7 on [0.25, 4], 4e-6 on [1e-30, 1e30]
 *   fm_powf   rel 1e-5 (x > 0)             fm_tanhf abs 3e-7
 *   fm_sinf / fm_cosf abs 1e-6 on [-100, 100] (three-part 2pi reduction)
 *   fm_tanf   rel 1e-6 on (-pi/2, pi/2)   fm_sqrtf rel 2e-7
 *
 * Every function is static inline and branch-light. None may be called with
 * NaN; none returns a denormal for an in-range argument. */
#include <stdint.h>

#define FM_PI 3.14159265358979f
#define FM_TWO_PI 6.28318530717959f
#define FM_LN2 0.693147180559945f
#define FM_LOG2E 1.44269504088896f

typedef union { float f; uint32_t u; int32_t i; } fm_bits_t;

static inline float fm_fabsf(float x) { fm_bits_t b; b.f = x; b.u &= 0x7FFFFFFFu; return b.f; }
static inline float fm_minf(float a, float b) { return a < b ? a : b; }
static inline float fm_maxf(float a, float b) { return a > b ? a : b; }
static inline float fm_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* floor for |x| < 2^31 */
static inline float fm_floorf(float x)
{
    int32_t i = (int32_t)x;
    float f = (float)i;
    return f > x ? f - 1.0f : f;
}

/* 2^n * 2^f for an integer n and |f| <= 0.5 (or a little more) */
static inline float fm_exp2_nf(int32_t n, float f)
{
    fm_bits_t b;
    float p;
    if (n < -126)
        return 0.0f;                 /* flush instead of a denormal */
    p = 1.534580891e-04f;            /* 2^f on [-0.5, 0.5]: degree 6, fitted (tools/fit_fastmath.py) */
    p = p * f + 1.339993158e-03f;
    p = p * f + 9.618488972e-03f;
    p = p * f + 5.550328776e-02f;
    p = p * f + 2.402264689e-01f;
    p = p * f + 6.931472057e-01f;
    p = p * f + 1.0f;
    b.f = p;
    b.i += n << 23;
    return b.f;
}

/* 2^x. Split x = n + f, f in [-0.5, 0.5]; fitted polynomial for 2^f. */
static inline float fm_exp2f(float x)
{
    float fl;
    if (x < -126.0f)
        return 0.0f;
    if (x > 127.0f)
        x = 127.0f;
    fl = fm_floorf(x + 0.5f);
    return fm_exp2_nf((int32_t)fl, x - fl);
}

/* e^x with Cody-Waite reduction: x = n ln2 + r, so |x| * eps never reaches the result */
static inline float fm_expf(float x)
{
    float n;
    if (x < -87.3f)
        return 0.0f;
    if (x > 88.7f)
        x = 88.7f;
    n = fm_floorf(x * FM_LOG2E + 0.5f);
    x = (x - n * 0.693145752f) - n * 1.42860677e-6f;      /* ln2 split: hi has 16 bits */
    return fm_exp2_nf((int32_t)n, x * FM_LOG2E);            /* |x log2e| <= 0.5 */
}

/* log2(x), x > 0 (denormals and 0 return -126 / -127-ish, never NaN) */
static inline float fm_log2f(float x)
{
    fm_bits_t b;
    float m, t, t2, p;
    int32_t e;
    b.f = x;
    if (b.i <= 0)
        return -127.0f;
    e = ((b.i >> 23) & 0xFF) - 127;
    b.i = (b.i & 0x007FFFFF) | 0x3F800000;     /* mantissa in [1, 2) */
    m = b.f;
    if (m > 1.41421356f) {                     /* centre on 1: m in [0.707, 1.414) */
        m *= 0.5f;
        e += 1;
    }
    /* log2(m) = 2/ln2 * atanh(t), t = (m-1)/(m+1), |t| <= 0.1716 */
    t = (m - 1.0f) / (m + 1.0f);
    t2 = t * t;
    p = 3.428278367e-01f;                       /* odd polynomial in t, fitted */
    p = p * t2 + 4.115337810e-01f;
    p = p * t2 + 5.770866525e-01f;
    p = p * t2 + 9.617966483e-01f;
    p = p * t2 + 2.885390082e+00f;
    return (float)e + p * t;
}

static inline float fm_logf(float x) { return fm_log2f(x) * FM_LN2; }

/* x^y for x > 0 (x <= 0 returns 0) */
static inline float fm_powf(float x, float y)
{
    if (x <= 0.0f)
        return 0.0f;
    return fm_exp2f(y * fm_log2f(x));
}

/* 10^(db/20) */
static inline float fm_db2lin(float db) { return fm_exp2f(db * 0.166096405f); }

static inline float fm_tanhf(float x)
{
    float a = fm_fabsf(x), e, r;
    if (a < 0.0004f)
        return x;                                /* tanh x = x - x^3/3: below float resolution here */
    if (a < 0.5f) {                              /* Taylor to x^13: the next term is 4e-8 at 0.5. No exp2, */
        float z = x * x;                         /* no divide: most clippers spend most samples here */
        return x * (1.0f + z * (-0.333333333f + z * (0.133333333f + z * (-0.0539682540f + z * (0.0218694885f +
                    z * (-0.00886323552f + z * 0.00359212803f))))));
    }
    if (a > 9.0f)
        return x > 0.0f ? 1.0f : -1.0f;
    e = fm_exp2f(2.0f * FM_LOG2E * a);
    r = 1.0f - 2.0f / (e + 1.0f);
    return x > 0.0f ? r : -r;
}

/* sin on [-pi, pi]: odd polynomial, degree 13 */
static inline float fm_sin_pi(float x)
{
    float x2 = x * x, p;
    p = 1.345176048e-10f;                      /* degree 13, fitted */
    p = p * x2 - 2.467706523e-08f;
    p = p * x2 + 2.752946668e-06f;
    p = p * x2 - 1.984015614e-04f;
    p = p * x2 + 8.333310417e-03f;
    p = p * x2 - 1.666666459e-01f;
    p = p * x2 + 9.999999946e-01f;
    return p * x;
}

static inline float fm_wrap_pi(float x)                /* to [-pi, pi), 2pi in three parts */
{
    float k = fm_floorf(x * (1.0f / FM_TWO_PI) + 0.5f);
    return ((x - k * 6.28125f) - k * 1.9353071693331003e-3f) - k * 1.0253131561401567e-11f;
}

static inline float fm_sinf(float x) { return fm_sin_pi(fm_wrap_pi(x)); }
static inline float fm_cosf(float x)
{
    float r = fm_wrap_pi(x) + 0.5f * FM_PI;          /* reduce first: adding pi/2 to a big x loses bits */
    if (r >= FM_PI)
        r -= FM_TWO_PI;
    return fm_sin_pi(r);
}
/* tan on (-pi/2, pi/2), the filter-coefficient range: cos(x) = sin(pi/2 - |x|), which keeps
 * its relative accuracy next to the pole where cos is small */
static inline float fm_tanf(float x)
{
    float a = fm_fabsf(x), c = (1.57079637f - a) - 4.37113883e-8f;
    return fm_sin_pi(x) / fm_sin_pi(c);
}

/* sine of a phase in turns (0..1 = one cycle): the cheap oscillator form */
static inline float fm_sin_turns(float ph)
{
    float x = ph - fm_floorf(ph + 0.5f);          /* [-0.5, 0.5) */
    return fm_sin_pi(x * FM_TWO_PI);
}

static inline float fm_sqrtf(float x)
{
    fm_bits_t b;
    float y;
    if (x <= 0.0f)
        return 0.0f;
    b.f = x;
    b.u = 0x5F375A86u - (b.u >> 1);              /* 1/sqrt seed, then Newton x3 */
    y = b.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

/* 1 / sqrt(x), x > 0: the seed and two Newton steps (rel 5e-6). For gains, not for values that are
 * shown or compared: fm_sqrtf keeps three. */
static inline float fm_rsqrtf(float x)
{
    fm_bits_t b;
    float y;
    b.f = x;
    b.u = 0x5F375A86u - (b.u >> 1);
    y = b.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return y * (1.5f - 0.5f * x * y * y);
}

/* denormal / tiny-value flush for filter states */
static inline float fm_flush(float x) { return fm_fabsf(x) < 1e-20f ? 0.0f : x; }

/* X0X TB-303 bass engine: a C99 / float / no-libm port of Open303 (Robin Schmidt, MIT) with
 * the Devilfish ranges and the Soft / RAT drive of schwung-303 (GPL-3.0; the RAT model is
 * from davemollen/dm-Rat, GPL-3.0).
 *
 * What is the same as schwung-303's engine: the signal path and every constant of Open303's
 * getSample() - pitch slew, the decay envelope with its two RC followers, the measured
 * env-mod mapping, the TB_303-mode TeeBeeFilter (mystran & kunn coefficients) with its
 * feedback highpass, the amp envelope, the 200 Hz de-clicker, and the allpass / highpass /
 * notch after the filter - and the pot-to-physical ranges of plugin.cpp.
 *
 * What is different, and why:
 *  - Oscillator: Open303's MipMappedWaveTable (2 x 12 x 2052 doubles, FFT-built) does not fit.
 *    Saw and the "303 square" are computed directly: the saw is polyBLEP; the square is the
 *    same transistor-shaper function Open303 tabulates, -tanh(F*(2p-1) + O), evaluated per
 *    sample (its falling edge is a smooth tanh ramp), with a polyBLEP on its one hard edge
 *    and its DC removed (the mip-map drops DC above 43 Hz).
 *  - Oversampling: BASS303_OS (1, 2 or 4; Open303 uses 4). The decimator is a polyphase IIR
 *    halfband (one stage per factor of 2) instead of Open303's 12th-order elliptic run at
 *    every oversample: 6 (OS 2) or 12 (OS 4) multiplies per output sample instead of ~100.
 *  - Control rate: envelopes, slew, cutoff and the filter's coefficients every BASS303_CTRL
 *    samples (the recursions advanced exactly, the values interpolated linearly between);
 *    Open303 computes them every sample. A trigger still jumps pitch and amp.
 *  - Idle: when the gate is off and the amp envelope has died (< -120 dB) for
 *    BASS303_IDLE_HOLD samples, render writes zeros and only advances the oscillator phase
 *    and the envelopes (closed form, once per block). Filter states are cleared on the way
 *    in, as Open303's own triggerNote() does after idle (schwung-303 never idles).
 *  - Only the four Devilfish controls that fit the panel are exposed (slide time, accent
 *    decay); the others stay at the stock values plugin.cpp uses with Devilfish off.
 *
 * Every instance is self-contained (no mutable static state): two can run side by side. */
#include <stdint.h>

#ifndef BASS303_OS
#define BASS303_OS 2                    /* oversampling factor: 1, 2 or 4 */
#endif
#if BASS303_OS != 1 && BASS303_OS != 2 && BASS303_OS != 4
#error "BASS303_OS must be 1, 2 or 4"
#endif

/* Idle hold-off, in samples of silence before the engine stops computing. Open303 never
 * stops: its oscillator and filters keep running under the closed amp, so a note after a rest
 * starts into warm filter states. Starting into cleared ones makes the first 5 ms of the note
 * 4-5 dB quieter than the reference (measured; every filter in the chain contributes, so no
 * cheap state initialisation or short pre-roll fixes it). Holding off keeps notes within a phrase exactly as
 * Open303 plays them; only a note after a longer silence starts cold. It costs nothing in
 * the worst case, which is a note sounding continuously. 0 = idle as soon as it is silent. */
#ifndef BASS303_IDLE_HOLD
#define BASS303_IDLE_HOLD 22050         /* 500 ms */
#endif
/* Control rate: the envelopes, slew, cutoff and filter coefficients are computed every
 * BASS303_CTRL samples (the envelope recursions advanced in exact closed form) and the
 * coefficients, increment and amp interpolated linearly in between. 1 = every sample, as
 * Open303. tests/host/run_bass303.sh measures 4 against 1. */
#ifndef BASS303_CTRL
#define BASS303_CTRL 4
#endif
#if BASS303_CTRL < 1 || BASS303_CTRL > 4
#error "BASS303_CTRL must be 1..4"
#endif
#define BASS303_SR 44100.0f
#define BASS303_MAX_BLOCK 256

/* parameter indices (pages of four: the UI shows KNOB 1-4) */
enum {
    BASS303_CUTOFF, BASS303_RESO, BASS303_ENVMOD, BASS303_DECAY,     /* page 1 */
    BASS303_ACCENT, BASS303_WAVE, BASS303_TUNE, BASS303_VOLUME,      /* page 2 */
    BASS303_DRIVE, BASS303_DRVTYPE, BASS303_SLIDE, BASS303_ACCDEC,   /* page 3 */
    BASS303_POS, BASS303_MOTION,   /* ACID MAX: the shape's POSITION and the slow MOTION that sweeps it (WAVE >= 2) */
    BASS303_NPARAMS
};
enum { BASS303_DRV_OFF, BASS303_DRV_SOFT, BASS303_DRV_RAT };

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } bass303_bq_t;     /* TDF-II biquad */
typedef struct { float x1, x2, y1, y2; } bass303_df1_t;                /* DF-I state */
typedef struct { float x1, y1; } bass303_ap_t;                         /* halfband allpass */

typedef struct bass303 {
    uint8_t pot[BASS303_NPARAMS];

    /* derived from the pots (bass303_set) */
    float cutoff;            /* nominal cutoff, Hz */
    float reso;              /* resonanceSkewed */
    float env_scaler, env_offset;
    float accent;            /* 0..1 */
    float tuning;            /* A4, Hz */
    float amp_scaler;        /* volume, linear */
    float decay_c, accdec_c; /* main-envelope multipliers for normal / accented notes */
    float slew_c;            /* pitch slew-limiter coefficient */
    int wave;                /* 0 saw, 1 square; ACID MAX: 2.. the B3W_* shapes (b3_osc_ext) */
    float pos, motion;       /* 0..1: the shape's position, the depth of its slow sweep */
    float mot_ph;            /* the sweep's phase, turns */
    int drv_type;
    float drv_amt;

    /* fixed coefficients (sample-rate dependent; set in bass303_init) */
    float rc1_c, rc2_c;                 /* filter-envelope RC followers (3 ms, 15 ms) */
    float dc_b0, dc_b1, dc_a1, dc_a2;   /* de-clicker: 200 Hz Butterworth lowpass, b2 = b0 */
    float hp1_b0, hp1_a1;               /* pre-filter highpass at the oversampled rate */
    float fbhp_b0, fbhp_a1;             /* feedback highpass at the oversampled rate */
    float hp1l_b0, hp1l_a1, fbhpl_b0, fbhpl_a1;   /* the same two at the base rate (lite) */
    int lite, lite_cur;      /* X0X overload guard: render without oversampling (bass303_set_lite) */
    float ap_b0, ap_a1;                 /* 14 Hz allpass (b1 = 1) */
    float hp2_b0, hp2_a1;               /* 24 Hz highpass */
    float nt_b0, nt_b1, nt_a1, nt_a2;   /* 7.5 Hz notch (b2 = b0) */
    float sq_dc, sq_h;                  /* 303 square: mean, hard-edge height */

    /* voice */
    int gate;                /* a note is held */
    int idle;                /* nothing sounding: render writes zeros */
    int started;             /* a note has been played since init (oscillator runs) */
    int quiet;               /* samples of silence so far (idle hold-off) */
    int snap;                /* next control step starts without interpolation */
    int trig;                /* next control step follows a trigger: pitch and amp jump */

    /* control rate: c^m for m = 0..BASS303_CTRL of each recursion, the RC followers'
     * closed-form gains, and the interpolated values the samples use */
    float rc1_pw[BASS303_CTRL + 1], rc2_pw[BASS303_CTRL + 1], slew_pw[BASS303_CTRL + 1];
    float ampd_pw[BASS303_CTRL + 1], ampn_pw[BASS303_CTRL + 1], ampa_pw[BASS303_CTRL + 1];
    float env_pw[BASS303_CTRL + 1], g1[BASS303_CTRL + 1], g2[BASS303_CTRL + 1];
    float c_inc, c_b0, c_k, c_g2, c_a;
    float osc_freq, slew_y;
    float phase;             /* oscillator phase in turns, 0..1 */
    float env_c, env_y;      /* main (filter) decay envelope */
    float rc1_y, rc2_y;
    float accent_gain;
    float amp_y;             /* amp envelope */
    int amp_acc;             /* the release is the accented one */
    int amp_trig;            /* next sample is the attack */
    bass303_df1_t dc;
    float hp1_x1, hp1_y1;
    float f_y1, f_y2, f_y3, f_y4, fb_x1, fb_y1;   /* TeeBeeFilter */
#if BASS303_OS >= 2
    bass303_ap_t hb1[6];                 /* 2x -> 1x halfband */
#endif
#if BASS303_OS == 4
    bass303_ap_t hb2[3];                 /* 4x -> 2x halfband */
#endif
    float ap_x1, ap_y1, hp2_x1, hp2_y1;
    bass303_df1_t nt;

    /* drive (schwung-303 drive.h) */
    bass303_bq_t s_pre, s_post, up_lp, down_lp;
    float s_gain, s_inv;
    float r_b[4], r_a[4], r_z[3];        /* RAT op-amp, 3rd-order TDF-II */
    float r_corr_b1, r_corr_z, r_tone_b1, r_tone_z;
    float dcb_x1, dcb_y1;
} bass303_t;

static void bass303_init(bass303_t *b);
static void bass303_note_on(bass303_t *b, int note, int accent, int slide);
static void bass303_note_off(bass303_t *b);
static void bass303_all_off(bass303_t *b);
static void bass303_render(bass303_t *b, float *out, int n);
/* the engine's overload guard: 1 = no oversampling from the next block (~12 % of the worst case
 * on the FM-1; some aliasing on bright, resonant notes), 0 = back to BASS303_OS */
static void bass303_set_lite(bass303_t *b, int on);
static int bass303_nparams(void);
const x0x_param_t *bass303_param(int i);
static void bass303_set(bass303_t *b, int i, int value);
static int bass303_get(const bass303_t *b, int i);

/* X0X TB-303 bass engine - see bass303.h for what is ported and what is not.
 *
 * Reference: schwung-303 src/dsp/open303/rosic_Open303.{h,cpp} (getSample, triggerNote,
 * slideToNote, calculateEnvModScalerAndOffset), rosic_TeeBeeFilter.h (TB_303 mode),
 * rosic_AnalogEnvelope / DecayEnvelope / LeakyIntegrator / OnePoleFilter / BiquadFilter,
 * rosic_MipMappedWaveTable.cpp (fillWithSaw303 / fillWithSquare303), src/dsp/plugin.cpp
 * (pot ranges) and src/dsp/drive.h (Soft / RAT). */

#define FSO (BASS303_SR * (float)BASS303_OS)    /* oversampled rate */

/* ---- parameters ------------------------------------------------------------------------ */

static const char *const b3_wave_names[] = { "Saw", "Sqr", "Morph", "Fold", "Formant", "WT Classic", "WT Sync", "WT Vocal", "WT FM",
                                             "WT Digital" };
static const char *const b3_drv_names[] = { "Off", "Soft", "RAT" };

/* defaults are schwung-303's (every knob at 0.5, Devilfish values stock), except Volume,
 * lowered from 0.8 so the nominal peak is ~0.5 */
static const x0x_param_t b3_params[BASS303_NPARAMS] = {
    { "Cutoff", 127, 64, 0 },        /* 314 .. 2394 Hz, exponential */
    { "Reso", 127, 64, 0 },          /* 0 .. 100 % */
    { "EnvMod", 127, 64, 0 },        /* 0 .. 100 % */
    { "Decay", 127, 64, 0 },         /* 200 .. 2000 ms, exponential (stock 303 range) */
    { "Accent", 127, 64, 0 },        /* 0 .. 100 % */
    { "Wave", 9, 0, b3_wave_names },     /* 0, 1: the 303 oscillators; 2..9: ACID MAX shapes */
    { "Tune", 127, 64, 0 },          /* A4 = 440 + (pot - 64) * 0.625 Hz: 400 .. 479.4 */
    { "Volume", 127, 96, 0 },        /* -60 .. 0 dB; 96 = -14.6 dB: peaks ~0.5 (schwung-303: 102) */
    { "Drive", 127, 0, 0 },          /* 0 = bypass */
    { "DrvTyp", 2, 1, b3_drv_names },
    { "Slide", 127, 21, 0 },         /* 2 .. 360 ms (Devilfish); 21 = 61 ms, stock is 60 */
    { "AccDec", 127, 7, 0 },         /* 30 .. 3000 ms (Devilfish); 7 = 194 ms, stock is 200 */
    { "Pos", 127, 0, 0 },            /* ACID MAX: where the shape is (WAVE >= 2) */
    { "Motion", 127, 0, 0 },         /* ACID MAX: how far a slow sweep moves it */
};

static int bass303_nparams(void) { return BASS303_NPARAMS; }

const x0x_param_t *bass303_param(int i)
{
    return (i >= 0 && i < BASS303_NPARAMS) ? &b3_params[i] : 0;
}

static int bass303_get(const bass303_t *b, int i)
{
    return (i >= 0 && i < BASS303_NPARAMS) ? b->pot[i] : 0;
}

/* exp(-1/(tau_ms * fs / 1000)): one-pole / RC coefficient */
static float b3_rc_coeff(float tau_ms, float fs)
{
    return tau_ms > 0.0f ? fm_expf(-1000.0f / (tau_ms * fs)) : 0.0f;
}

/* pw[m] = c^m, m = 0..BASS303_CTRL */
static void b3_powers(float *pw, float c)
{
    int m;
    pw[0] = 1.0f;
    for (m = 1; m <= BASS303_CTRL; m++)
        pw[m] = pw[m - 1] * c;
}

/* the main envelope's multiplier changed: its powers, and the closed-form gains of the two RC
 * followers it drives, g[m] = (1-a) sum_{n=1..m} a^(m-n) c^n (so r_m = a^m r_0 + g[m] e_0) */
static void b3_env_tables(bass303_t *b)
{
    int m;
    b3_powers(b->env_pw, b->env_c);
    b->g1[0] = b->g2[0] = 0.0f;
    for (m = 1; m <= BASS303_CTRL; m++) {
        b->g1[m] = b->rc1_c * b->g1[m - 1] + (1.0f - b->rc1_c) * b->env_pw[m];
        b->g2[m] = b->rc2_c * b->g2[m - 1] + (1.0f - b->rc2_c) * b->env_pw[m];
    }
}

/* Open303::calculateEnvModScalerAndOffset, the measured mapping */
static void b3_calc_envmod(bass303_t *b)
{
    float e = (float)b->pot[BASS303_ENVMOD] * (1.0f / 127.0f);
    float c = fm_log2f(b->cutoff * (1.0f / 313.8152786f)) * (1.0f / 2.931683907f); /* log2(c1/c0) */
    float slo = 3.773996325f * e + 0.736965594f;
    float shi = 4.194548788f * e + 0.864344901f;
    b->env_scaler = (1.0f - c) * slo + c * shi;
    b->env_offset = 0.048292931f * c + 0.294391201f;
}

/* drive.h set_lowpass / set_low_shelf (TDF-II coefficients, a0-normalised) */
static void b3_bq_lowpass(bass303_bq_t *q, float fc, float fs)
{
    float w = FM_TWO_PI * fc / fs, cs = fm_cosf(w), sn = fm_sinf(w);
    float alpha = sn * 0.707106781f, a0 = 1.0f + alpha;     /* sn / (2 q), q = 1/sqrt2 */
    q->b0 = 0.5f * (1.0f - cs) / a0;
    q->b1 = (1.0f - cs) / a0;
    q->b2 = q->b0;
    q->a1 = -2.0f * cs / a0;
    q->a2 = (1.0f - alpha) / a0;
}

static void b3_bq_low_shelf(bass303_bq_t *q, float fc, float gain_db, float fs)
{
    float A = fm_db2lin(0.5f * gain_db);                       /* 10^(g/40) */
    float w = FM_TWO_PI * fc / fs, cs = fm_cosf(w), sn = fm_sinf(w);
    float alpha = sn * 0.707106781f;                           /* S = 1: sn/2 * sqrt(2) */
    float s2 = 2.0f * fm_sqrtf(A) * alpha;
    float a0 = (A + 1.0f) + (A - 1.0f) * cs + s2;
    q->b0 = A * ((A + 1.0f) - (A - 1.0f) * cs + s2) / a0;
    q->b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs) / a0;
    q->b2 = A * ((A + 1.0f) - (A - 1.0f) * cs - s2) / a0;
    q->a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs) / a0;
    q->a2 = ((A + 1.0f) + (A - 1.0f) * cs - s2) / a0;
}

static inline float b3_bq_run(bass303_bq_t *q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

/* RAT op-amp: drive.h rat::State::update_op_amp_coeffs + the correction one-pole cutoff */
static void b3_rat_coeffs(bass303_t *b)
{
    const float R1 = 100000.0f, C1 = 1e-10f, Z1_B0 = 2.72149e-7f, Z1_B1 = 0.0027354f;
    const float Z1_A0 = 6.27638e-9f, Z1_A1 = 0.0000069f;
    const float t = 1.0f / BASS303_SR, s0 = t * 0.5f, s1 = t * t * 0.25f, s2 = t * t * t * 0.125f;
    float dist = fm_maxf(b->drv_amt, 0.001f);
    float z2_b0 = fm_maxf(dist * R1, 1.0f), z2_a0 = z2_b0 * C1;
    float a0 = Z1_B0 * z2_a0, a1 = Z1_B0 + Z1_B1 * z2_a0, a2 = Z1_B1 + z2_a0;
    float num[4], den[4], nz[4], dz[4], inv;
    int i;
    num[0] = a0; num[1] = a1 + Z1_A0 * z2_b0; num[2] = Z1_A1 * z2_b0 + Z1_B1 + z2_a0; num[3] = 1.0f;
    den[0] = a0; den[1] = a1; den[2] = a2; den[3] = 1.0f;
    for (i = 0; i < 2; i++) {                                  /* bilinear transform */
        const float *x = i ? den : num;
        float *o = i ? dz : nz;
        float x0 = x[0], x1 = x[1] * s0, x2 = x[2] * s1, x3 = x[3] * s2;
        o[0] = x0 + x1 + x2 + x3;
        o[1] = -3.0f * x0 - x1 + x2 + 3.0f * x3;
        o[2] = 3.0f * x0 - x1 - x2 + 3.0f * x3;
        o[3] = -x0 + x1 - x2 + x3;
    }
    inv = 1.0f / dz[0];
    for (i = 0; i < 4; i++) {
        b->r_b[i] = nz[i] * inv;
        b->r_a[i] = dz[i] * inv;
    }
    /* correction cutoff = MAX_GAIN_AT_1HZ / (dist * (MAX_DIST_GAIN - 1) + 1) */
    b->r_corr_b1 = fm_expf(-FM_TWO_PI / BASS303_SR * 1119360.558f / (dist * 2306.231003f + 1.0f));
}

static void bass303_set(bass303_t *b, int i, int v)
{
    float n;
    if (i < 0 || i >= BASS303_NPARAMS)
        return;
    if (v < 0)
        v = 0;
    if (v > b3_params[i].max)
        v = b3_params[i].max;
    b->pot[i] = (uint8_t)v;
    n = (float)v * (1.0f / 127.0f);
    switch (i) {
    case BASS303_CUTOFF:
        b->cutoff = 314.0f * fm_exp2f(n * 2.930586688f);       /* log2(2394/314) */
        b3_calc_envmod(b);
        break;
    case BASS303_ENVMOD:
        b3_calc_envmod(b);
        break;
    case BASS303_RESO:                                         /* TeeBeeFilter::setResonance */
        b->reso = (1.0f - fm_expf(-3.0f * n)) * (1.0f / 0.950212932f);
        break;
    case BASS303_DECAY:                                        /* applied at the next note */
        b->decay_c = b3_rc_coeff(200.0f * fm_exp2f(n * 3.321928095f), BASS303_SR);
        break;
    case BASS303_ACCDEC:
        b->accdec_c = b3_rc_coeff(30.0f + 2970.0f * n, BASS303_SR);
        break;
    case BASS303_ACCENT:
        b->accent = n;
        break;
    case BASS303_WAVE:
        b->wave = v;
        break;
    case BASS303_POS:
        b->pos = n;
        break;
    case BASS303_MOTION:
        b->motion = n;
        break;
    case BASS303_TUNE:
        b->tuning = 440.0f + (float)(v - 64) * 0.625f;
        break;
    case BASS303_VOLUME:
        b->amp_scaler = fm_db2lin(-60.0f + 60.0f * n);
        break;
    case BASS303_SLIDE:                                        /* slew tau = 0.2 * slide time */
        b->slew_c = b3_rc_coeff(0.2f * (2.0f + 358.0f * n), BASS303_SR);
        b3_powers(b->slew_pw, b->slew_c);
        break;
    case BASS303_DRIVE:
        b->drv_amt = n;
        b->s_gain = fm_db2lin(24.0f * n);
        b->s_inv = 1.0f / b->s_gain;
        b3_rat_coeffs(b);
        break;
    case BASS303_DRVTYPE:
        if (v != b->drv_type) {                                /* drive.h set_model: fresh state */
            b->s_pre.z1 = b->s_pre.z2 = b->s_post.z1 = b->s_post.z2 = 0.0f;
            b->up_lp.z1 = b->up_lp.z2 = b->down_lp.z1 = b->down_lp.z2 = 0.0f;
            b->r_z[0] = b->r_z[1] = b->r_z[2] = b->r_corr_z = b->r_tone_z = 0.0f;
            b->dcb_x1 = b->dcb_y1 = 0.0f;
        }
        b->drv_type = v;
        break;
    }
}

/* clear everything that only matters while sound is running (Open303::triggerNote after idle) */
static void b3_clear_audio_state(bass303_t *b)
{
    int i;
    b->dc.x1 = b->dc.x2 = b->dc.y1 = b->dc.y2 = 0.0f;
    b->hp1_x1 = b->hp1_y1 = 0.0f;
    b->f_y1 = b->f_y2 = b->f_y3 = b->f_y4 = b->fb_x1 = b->fb_y1 = 0.0f;
#if BASS303_OS >= 2
    for (i = 0; i < 6; i++)
        b->hb1[i].x1 = b->hb1[i].y1 = 0.0f;
#endif
#if BASS303_OS == 4
    for (i = 0; i < 3; i++)
        b->hb2[i].x1 = b->hb2[i].y1 = 0.0f;
#endif
    b->ap_x1 = b->ap_y1 = b->hp2_x1 = b->hp2_y1 = 0.0f;
    b->nt.x1 = b->nt.x2 = b->nt.y1 = b->nt.y2 = 0.0f;
    b->s_pre.z1 = b->s_pre.z2 = b->s_post.z1 = b->s_post.z2 = 0.0f;
    b->up_lp.z1 = b->up_lp.z2 = b->down_lp.z1 = b->down_lp.z2 = 0.0f;
    for (i = 0; i < 3; i++)
        b->r_z[i] = 0.0f;
    b->r_corr_z = b->r_tone_z = 0.0f;
    b->dcb_x1 = b->dcb_y1 = 0.0f;
    b->amp_y = 0.0f;
}

static void bass303_init(bass303_t *b)
{
    uint8_t *p = (uint8_t *)b;
    unsigned k;
    int i;
    float w, sn, cs, alpha, scale, x, F, O, ln_cosh_p, ln_cosh_m;

    for (k = 0; k < sizeof(*b); k++)
        p[k] = 0;

    /* fixed (Open303 constructor + setSampleRate) */
    b->rc1_c = b3_rc_coeff(3.0f, BASS303_SR);           /* normalAttack */
    b->rc2_c = b3_rc_coeff(15.0f, BASS303_SR);
    b3_powers(b->rc1_pw, b->rc1_c);
    b3_powers(b->rc2_pw, b->rc2_c);
    b3_powers(b->ampd_pw, b3_rc_coeff(1230.0f, BASS303_SR));         /* amp: y += (1 - c)(0 - y) */
    b3_powers(b->ampn_pw, b3_rc_coeff(1.0f, BASS303_SR));            /* release, normal: 1 ms */
    b3_powers(b->ampa_pw, b3_rc_coeff(50.0f, BASS303_SR));           /* release, accented: 50 ms */

    w = FM_TWO_PI * 200.0f / BASS303_SR;             /* de-clicker: LOWPASS12, q = sqrt(.5) */
    sn = fm_sinf(w);
    cs = fm_cosf(w);
    alpha = sn * 0.707106781f;
    scale = 1.0f / (1.0f + alpha);
    b->dc_a1 = 2.0f * cs * scale;
    b->dc_a2 = (alpha - 1.0f) * scale;
    b->dc_b1 = (1.0f - cs) * scale;
    b->dc_b0 = 0.5f * b->dc_b1;

    x = fm_expf(-FM_TWO_PI * 44.486f / FSO);         /* OnePoleFilter HIGHPASS (dspguide) */
    b->hp1_b0 = 0.5f * (1.0f + x);
    b->hp1_a1 = x;
    x = fm_expf(-FM_TWO_PI * 150.0f / FSO);          /* feedback highpass (stock 150 Hz) */
    b->fbhp_b0 = 0.5f * (1.0f + x);
    b->fbhp_a1 = x;
    x = fm_expf(-FM_TWO_PI * 44.486f / BASS303_SR);  /* the same two at the base rate (lite) */
    b->hp1l_b0 = 0.5f * (1.0f + x);
    b->hp1l_a1 = x;
    x = fm_expf(-FM_TWO_PI * 150.0f / BASS303_SR);
    b->fbhpl_b0 = 0.5f * (1.0f + x);
    b->fbhpl_a1 = x;
    x = fm_expf(-FM_TWO_PI * 24.167f / BASS303_SR);
    b->hp2_b0 = 0.5f * (1.0f + x);
    b->hp2_a1 = x;
    x = fm_tanf(FM_PI * 14.008f / BASS303_SR);       /* OnePoleFilter ALLPASS (DAFX) */
    b->ap_b0 = (x - 1.0f) / (x + 1.0f);
    b->ap_a1 = -b->ap_b0;

    w = FM_TWO_PI * 7.5164f / BASS303_SR;            /* BANDREJECT, 4.7 octaves */
    sn = fm_sinf(w);
    x = fm_sinf(0.5f * w);
    cs = 1.0f - 2.0f * x * x;                        /* cos w without the cancellation */
    x = 0.5f * FM_LN2 * 4.7f * w / sn;               /* alpha = sn * sinh(x) */
    alpha = sn * 0.5f * (fm_expf(x) - fm_expf(-x));
    scale = 1.0f / (1.0f + alpha);
    b->nt_a1 = 2.0f * cs * scale;
    b->nt_a2 = (alpha - 1.0f) * scale;
    b->nt_b0 = scale;
    b->nt_b1 = -2.0f * cs * scale;

    /* 303 square = -tanh(F * (2p - 1) + O), F = dB2amp(36.9), O = 4.37 (fillWithSquare303,
     * after its 180 degree circular shift). Mean over a cycle, for the DC the mip-map drops:
     * -(ln cosh(F + O) - ln cosh(F - O)) / 2F; ln cosh(y) = y - ln 2 + ln(1 + e^-2y), y > 0. */
    F = fm_db2lin(36.9f);
    O = 4.37f;
    ln_cosh_p = (F + O) + fm_logf(1.0f + fm_expf(-2.0f * (F + O)));
    ln_cosh_m = (F - O) + fm_logf(1.0f + fm_expf(-2.0f * (F - O)));
    b->sq_dc = -(ln_cosh_p - ln_cosh_m) / (2.0f * F);
    b->sq_h = fm_tanhf(F + O) + fm_tanhf(F - O);    /* height of the hard edge at p = 0 */

    /* drive.h prepare() */
    b3_bq_low_shelf(&b->s_pre, 400.0f, 6.0f, BASS303_SR);
    b3_bq_low_shelf(&b->s_post, 400.0f, -6.0f, BASS303_SR);
    b3_bq_lowpass(&b->up_lp, 19000.0f, 2.0f * BASS303_SR);
    b3_bq_lowpass(&b->down_lp, 19000.0f, 2.0f * BASS303_SR);
    /* tone fixed at 0.5: 1 / (2 pi (0.5 * 100k + 1.5k) 3.3n) */
    b->r_tone_b1 = fm_expf(-FM_TWO_PI / BASS303_SR / (FM_TWO_PI * 51500.0f * 3.3e-9f));

    b->drv_type = -1;
    for (i = 0; i < BASS303_NPARAMS; i++)
        bass303_set(b, i, b3_params[i].def);

    b->osc_freq = b->slew_y = 440.0f;
    b->env_c = b->decay_c;
    b3_env_tables(b);
    b->env_y = 0.0f;
    b->idle = 1;
    b->snap = 1;
}

/* ---- notes ----------------------------------------------------------------------------- */

static void b3_set_accent_state(bass303_t *b, int accent)
{
    if (accent) {
        b->accent_gain = b->accent;
        b->env_c = b->accdec_c;          /* setMainEnvDecay(accentDecay) */
    } else {
        b->accent_gain = 0.0f;
        b->env_c = b->decay_c;
    }
    b->amp_acc = accent != 0;            /* release: accentAmpRelease 50 ms, normal 1 ms */
    b3_env_tables(b);
}

static void bass303_note_on(bass303_t *b, int note, int accent, int slide)
{
    float f;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    f = b->tuning * fm_exp2f((float)(note - 69) * (1.0f / 12.0f));
    b3_set_accent_state(b, accent);
    b->osc_freq = f;
    if (!(slide && b->gate)) {           /* triggerNote; else slideToNote (no retrigger) */
        b->slew_y = f;                   /* pitchSlewLimiter.setState */
        b->env_y = 1.0f / b->env_c;      /* mainEnv.trigger: the next sample is 1 */
        b->amp_trig = 1;                 /* ampEnv.noteOn: attack time 0 */
        b->trig = 1;
    }
    if (b->idle)
        b->snap = 1;                     /* no interpolation from the stale control values */
    b->gate = 1;
    b->idle = 0;
    b->quiet = 0;
    b->started = 1;
}

static void bass303_note_off(bass303_t *b) { b->gate = 0; }
static void bass303_set_lite(bass303_t *b, int on) { b->lite = on != 0; }
static void bass303_all_off(bass303_t *b) { b->gate = 0; }

/* ---- render ---------------------------------------------------------------------------- */

/* polyBLEP residual for a unit (2-high) step at phase 0, t in [0, 1), dt = increment. The
 * divides happen only on the two samples per period next to the step. */
static inline float b3_blep(float t, float dt)
{
    float x;
    if (t < dt) {
        x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

#if BASS303_OS >= 2
static inline float b3_hb(bass303_ap_t *s, float a, float x)
{
    float y = a * (x - s->y1) + s->x1;
    s->x1 = x;
    s->y1 = y;
    return y;
}
#endif

/* Halfband decimators: polyphase allpass pairs (de Soras' hiir structure; coefficients from
 * its closed-form elliptic design, response checked numerically). 2x -> 1x: 6 coefficients,
 * transition 0.04 (passband flat to 18.5 kHz, -74.5 dB from 25.6 kHz at the 2x rate). 4x -> 2x:
 * 3 coefficients, transition 0.137 (flat to 19.9 kHz, -62 dB over the band that folds below
 * 20 kHz). 8 / 4 coefficients (-99 / -81 dB) measured the same alias floor in
 * tests/host/run_bass303.sh: what reaches the decimator is already low-passed by the ladder.
 * Paths: even coefficients take the newer sample, odd ones the older. */
#if BASS303_OS >= 2
static inline float b3_decim_hb1(bass303_ap_t *s, float x_old, float x_new)
{
    float a = x_new, c = x_old;
    a = b3_hb(&s[0], 0.068204076f, a);
    c = b3_hb(&s[1], 0.240270358f, c);
    a = b3_hb(&s[2], 0.448676236f, a);
    c = b3_hb(&s[3], 0.641122367f, c);
    a = b3_hb(&s[4], 0.799997564f, a);
    c = b3_hb(&s[5], 0.934482236f, c);
    return 0.5f * (a + c);
}
#endif
#if BASS303_OS == 4
static inline float b3_decim_hb2(bass303_ap_t *s, float x_old, float x_new)
{
    float a = x_new, c = x_old;
    a = b3_hb(&s[0], 0.104996100f, a);
    c = b3_hb(&s[1], 0.375357314f, c);
    a = b3_hb(&s[2], 0.754467339f, a);
    return 0.5f * (a + c);
}
#endif

/* While idle, advance what Open303 keeps running so the next note starts where the
 * reference would: oscillator phase, slew, the main envelope and its RC followers
 * (closed forms of N steps of each recursion). */
static void b3_idle_advance(bass303_t *b, int n)
{
    float N = (float)n, cn, an, ph;
    if (!b->started)
        return;                                       /* Open303 does not run before note 1 */
    ph = b->phase + N * b->slew_y * (1.0f / BASS303_SR);    /* N base samples */
    b->phase = ph - fm_floorf(ph);
    an = fm_powf(b->slew_c, N);
    b->slew_y = b->osc_freq + an * (b->slew_y - b->osc_freq);
    cn = fm_powf(b->env_c, N);
    an = fm_powf(b->rc1_c, N);
    /* r[n] = a r[n-1] + (1-a) u[n], u[n] = E c^n:  r_N = a^N r0 + (1-a) E c (c^N - a^N)/(c - a) */
    b->rc1_y = an * b->rc1_y + (1.0f - b->rc1_c) * b->env_y * b->env_c * (cn - an) / (b->env_c - b->rc1_c);
    an = fm_powf(b->rc2_c, N);
    if (b->accent_gain > 0.0f)
        b->rc2_y = an * b->rc2_y + (1.0f - b->rc2_c) * b->env_y * b->env_c * (cn - an) / (b->env_c - b->rc2_c);
    else
        b->rc2_y *= an;
    b->env_y = fm_flush(b->env_y * cn);
    b->rc1_y = fm_flush(b->rc1_y);
    b->rc2_y = fm_flush(b->rc2_y);
}

static void b3_drive_block(bass303_t *b, float *out, int n)
{
    int i;
    float y, v, u0, u1, yh;
    if (b->drv_type == BASS303_DRV_SOFT) {
        const float g = b->s_gain, ig = b->s_inv;
        for (i = 0; i < n; i++) {
            v = b3_bq_run(&b->s_pre, out[i]) * g;
            u0 = 2.0f * b3_bq_run(&b->up_lp, v);
            u1 = 2.0f * b3_bq_run(&b->up_lp, 0.0f);
            b3_bq_run(&b->down_lp, fm_tanhf(u0 + 0.15f) - 0.14888503f);
            y = b3_bq_run(&b->down_lp, fm_tanhf(u1 + 0.15f) - 0.14888503f);
            y = b3_bq_run(&b->s_post, y * ig);
            yh = y - b->dcb_x1 + 0.9996f * b->dcb_y1;
            b->dcb_x1 = y;
            b->dcb_y1 = yh;
            out[i] = yh;
        }
    } else {                                           /* RAT */
        const float *rb = b->r_b, *ra = b->r_a;
        const float cb1 = b->r_corr_b1, tb1 = b->r_tone_b1;
        float x, x2;
        for (i = 0; i < n; i++) {
            x = out[i];
            y = x * rb[0] + b->r_z[0];                 /* op-amp, 3rd-order TDF-II */
            b->r_z[0] = x * rb[1] - y * ra[1] + b->r_z[1];
            b->r_z[1] = x * rb[2] - y * ra[2] + b->r_z[2];
            b->r_z[2] = x * rb[3] - y * ra[3];
            b->r_corr_z = y * (1.0f - cb1) + b->r_corr_z * cb1;
            v = b->r_corr_z * 1.877f;
            u0 = 2.0f * b3_bq_run(&b->up_lp, v);
            u1 = 2.0f * b3_bq_run(&b->up_lp, 0.0f);
            {   /* x / (1 + x^4)^(1/4) = x s t, s = y^(-1/2), t = s^(-1/2) = y^(1/4): no divide */
                float s0, s1;
                x2 = u0 * u0;
                s0 = fm_rsqrtf(1.0f + x2 * x2);
                u0 *= s0 * fm_rsqrtf(s0);
                x2 = u1 * u1;
                s1 = fm_rsqrtf(1.0f + x2 * x2);
                u1 *= s1 * fm_rsqrtf(s1);
            }
            b3_bq_run(&b->down_lp, u0);
            y = b3_bq_run(&b->down_lp, u1) * 0.3204805f;
            b->r_tone_z = y * (1.0f - tb1) + b->r_tone_z * tb1;
            y = b->r_tone_z;
            yh = y - b->dcb_x1 + 0.9996f * b->dcb_y1;
            b->dcb_x1 = y;
            b->dcb_y1 = yh;
            out[i] = yh;
        }
    }
}


/* ---- ACID MAX oscillator shapes (WAVE >= 2) ----------------------------------------------------------------------
 * Six shapes and, in WAVETABLE mode, five tables, each swept by POSITION (and moved by MOTION). Computed from the
 * phase (turns) with polyBLEP on the hard edges, so no table is stored: the filter after it (the 303's ladder) does
 * the rest. WAVE: 2 MORPH, 3 FOLD, 4 FORMANT, 5 WT CLASSIC, 6 WT SYNC, 7 WT VOCAL, 8 WT FM, 9 WT DIGITAL. */
enum { B3W_SAW, B3W_SQR, B3W_MORPH, B3W_FOLD, B3W_FORMANT, B3W_WT_CLASSIC, B3W_WT_SYNC, B3W_WT_VOCAL, B3W_WT_FM,
       B3W_WT_DIGITAL, B3W_N };

static inline float b3_fract(float x) { return x - fm_floorf(x); }
static inline float b3_sint(float t) { return fm_sinf(t * FM_TWO_PI); }
static inline float b3_ramp(float t, float inc) { return t + t - 1.0f - b3_blep(t, inc); }   /* the rising saw, -1 .. 1 */
/* a pulse of width w (0.05 .. 0.5), the two edges band-limited, peak 1 */
static inline float b3_pulse(float t, float inc, float w)
{
    return (b3_ramp(t, inc) - b3_ramp(b3_fract(t - w + 1.0f), inc)) * (0.5f / (1.0f - w));
}
/* a window that is 0 at the end of the cycle and a sine that is 0 at its start: any ratio k is click-free */
static inline float b3_res(float t, float k)
{
    float w = 1.0f - t;
    return w * w * b3_sint(t * k);
}
/* the two formants of a vowel as multiples of the pitch (A2 as the reference), "ooh" at 0 to "eee" at 1 */
static inline float b3_vowel(float t, float v)
{
    float r1 = 2.7f - 0.25f * v, r2 = 7.9f + 8.1f * v;
    return b3_res(t, r1) + 0.6f * b3_res(t, r2);
}
/* each shape's level against the 303's saw, averaged over POSITION (tests/acidmax_osc_test.c, CAL=1) */
static const float B3W_GAIN[B3W_N] = {1.0f, 1.0f, 1.39f, 0.92f, 1.50f, 1.20f, 1.21f, 1.18f, 0.95f, 1.01f};

static inline float b3_osc_ext(int wv, float t, float inc, float pos)
{
    float s, a, k, f;
    switch (wv) {
    case B3W_MORPH:                                        /* sine to pulse on one knob (the pulse narrows) */
        s = (1.0f - pos) * b3_sint(t) + pos * b3_pulse(t, inc, 0.5f - 0.4f * pos);
        break;
    case B3W_FOLD:                                         /* a sine folded back on itself, more as POSITION rises */
        s = fm_sinf(1.5707963f * (1.0f + 7.0f * pos) * b3_sint(t));
        break;
    case B3W_FORMANT:                                      /* vowel resonances on a saw: POSITION is the resonance's ratio */
        s = 0.5f * b3_ramp(t, inc) + 1.4f * b3_res(t, 1.0f + 7.0f * pos);
        break;
    case B3W_WT_CLASSIC:                                   /* sine to saw to formant */
        if (pos < 0.5f) {
            a = pos + pos;
            s = (1.0f - a) * b3_sint(t) + a * b3_ramp(t, inc);
        } else {
            a = (pos - 0.5f) * 2.0f;
            s = (1.0f - a) * b3_ramp(t, inc) + a * (0.5f * b3_ramp(t, inc) + 1.4f * b3_res(t, 4.0f));
        }
        break;
    case B3W_WT_SYNC: {                                    /* hard-sync scream: a saw at 1 .. 8 times the pitch, reset each cycle */
        float r = 1.0f + 7.0f * pos;
        s = 0.9f * b3_ramp(b3_fract(t * r), inc * r);
        break;
    }
    case B3W_WT_VOCAL:                                     /* talking acid: "ooh" to "eee" */
        s = 0.35f * b3_ramp(t, inc) + 1.5f * b3_vowel(t, pos);
        break;
    case B3W_WT_FM: {                                      /* soft bass to metallic bells: two whole ratios blended */
        float x = pos * 3.0f, idx = 0.5f + 4.0f * pos;
        int i = (int)x;
        f = x - (float)i;
        idx *= 1.0f / FM_TWO_PI;                           /* (the index in turns: b3_sint takes turns) */
        a = b3_sint(t + idx * b3_sint(t * (float)(i + 1)));
        k = b3_sint(t + idx * b3_sint(t * (float)(i + 2)));
        s = (1.0f - f) * a + f * k;
        break;
    }
    case B3W_WT_DIGITAL: {                                 /* stepped, bit-crushed grit: fewer steps and levels as POSITION rises */
        float n = 32.0f - 28.0f * pos, lv = 64.0f - 58.0f * pos;
        s = 2.0f * fm_floorf(t * n) / n - 1.0f;
        s = fm_floorf(s * lv + 0.5f) / lv;
        break;
    }
    default:
        s = b3_ramp(t, inc);
        break;
    }
    return s * B3W_GAIN[wv < B3W_N ? wv : 0];
}

static const float recip[5] = { 0.0f, 1.0f, 0.5f, 0.333333343f, 0.25f };

static void b3_run(bass303_t *b, float *out, int n)
{
    int i, j, m;
    /* hot state in locals: out[] may alias the struct's floats, which would force reloads */
    float phase = b->phase, inc = b->c_inc, f_b0 = b->c_b0, k = b->c_k, g2 = b->c_g2, a = b->c_a;
    float hp1x = b->hp1_x1, hp1y = b->hp1_y1;
    float y1 = b->f_y1, y2 = b->f_y2, y3 = b->f_y3, y4 = b->f_y4, fbx = b->fb_x1, fby = b->fb_y1;
    const float osc_freq = b->osc_freq, acc = b->accent_gain;
    const float scaler = b->env_scaler, offset = b->env_offset, cutoff = b->cutoff, r = b->reso;
    const float *amp_pw = b->gate ? b->ampd_pw : b->amp_acc ? b->ampa_pw : b->ampn_pw;
    const float amp_boost = b->gate ? 0.45f + 4.0f * acc : 0.0f;
    /* the overload guard's lite mode: no oversampling; a change of rate jumps to the new rate's
     * coefficients (snap) rather than gliding between the two rates' values */
    const int lite = b->lite;
    const int nos = lite ? 1 : BASS303_OS;
    const float fso = lite ? BASS303_SR : FSO;
    const float hp1_b0 = lite ? b->hp1l_b0 : b->hp1_b0, hp1_a1 = lite ? b->hp1l_a1 : b->hp1_a1;
    const float fb_b0 = lite ? b->fbhpl_b0 : b->fbhp_b0, fb_a1 = lite ? b->fbhpl_a1 : b->fbhp_a1;
    const float sq_dc = b->sq_dc, sq_h2 = 0.5f * b->sq_h;
    const float out_gain = b->amp_scaler;
    const int wv = b->wave;
    float pe = b->pos;                                  /* the shape's position, swept by MOTION (WAVE >= 2) */
    float os[BASS303_OS];

    for (i = 0; i < n; i += m) {
        float e0, fc, fx, n_b0, n_k, n_g2, n_inc, n_a, rm, d_inc, d_b0, d_k, d_g2, d_a;
        m = n - i < BASS303_CTRL ? n - i : BASS303_CTRL;

        /* ---- control step: Open303's per-sample recursions advanced m samples in closed
         * form (exact), then the cutoff and amp they produce at the chunk's last sample */
        e0 = b->env_y;                                         /* main envelope: y *= c */
        b->env_y = e0 * b->env_pw[m];
        b->rc1_y = b->rc1_pw[m] * b->rc1_y + b->g1[m] * e0;    /* RC followers of it */
        b->rc2_y = b->rc2_pw[m] * b->rc2_y + (acc > 0.0f ? b->g2[m] * e0 : 0.0f);
        b->slew_y = osc_freq + b->slew_pw[m] * (b->slew_y - osc_freq);
        if (wv >= 2) {                                         /* MOTION: a slow triangle moves POSITION either way */
            b->mot_ph += (float)m * (1.5f / BASS303_SR);
            if (b->mot_ph >= 1.0f)
                b->mot_ph -= 1.0f;
            pe = fm_clampf(b->pos + 0.5f * b->motion * (1.0f - 4.0f * fm_fabsf(b->mot_ph - 0.5f)), 0.0f, 1.0f);
        }
        if (b->amp_trig) {                                     /* attack 0: 1, then decay */
            b->amp_y = amp_pw[m - 1];
            b->amp_trig = 0;
        } else {
            b->amp_y *= amp_pw[m];
        }
        if (lite != b->lite_cur) {
            b->lite_cur = lite;
            b->snap = 1;
        }
        n_inc = b->slew_y * (1.0f / fso);
        n_a = b->amp_y + amp_boost * b->env_y;
        fc = cutoff * fm_exp2f(scaler * (b->rc1_y - offset) + acc * b->rc2_y);   /* n1 = n2 = 1 */
        fc = fm_clampf(fc, 200.0f, 20000.0f);
        /* TeeBeeFilter::calculateCoefficientsApprox4, TB_303 branch only (the a1 polynomial
         * it computes first is overwritten there, so it is skipped) */
        fx = fc * (0.707106781f / fso);
        n_b0 = (0.00045522346f + 6.1922189f * fx) / (1.0f + 12.358354f * fx + 4.4156345f * (fx * fx));
        n_k = fx * (fx * (fx * (fx * (fx * (fx + 7198.6997f) - 5837.7917f) - 476.47308f) + 614.95611f) + 213.87126f) + 16.998792f;
        n_g2 = 2.0f * ((n_k * (1.0f / 17.0f) - 1.0f) * r + 1.0f) * (1.0f + r);
        n_k *= r;
        if (b->snap) {                                         /* first chunk after init / idle */
            inc = n_inc; f_b0 = n_b0; k = n_k; g2 = n_g2; a = n_a;
            d_inc = d_b0 = d_k = d_g2 = d_a = 0.0f;
            b->snap = 0;
        } else {                                               /* per-sample linear interpolation */
            rm = recip[m];
            d_inc = (n_inc - inc) * rm;
            d_b0 = (n_b0 - f_b0) * rm;
            d_k = (n_k - k) * rm;
            d_g2 = (n_g2 - g2) * rm;
            d_a = (n_a - a) * rm;
            if (b->trig) {               /* a trigger jumps pitch and amp in Open303: no ramp */
                inc = n_inc;
                a = n_a;
                d_inc = d_a = 0.0f;
            }
        }
        b->trig = 0;

        for (j = 0; j < m; j++) {
            float s, t, ym, amp;
            inc += d_inc;
            f_b0 += d_b0;
            k += d_k;
            g2 += d_g2;
            a += d_a;

            /* amp: de-clicked (200 Hz Butterworth), per sample */
            ym = b->dc_b0 * (a + b->dc.x2) + b->dc_b1 * b->dc.x1 + b->dc_a1 * b->dc.y1 + b->dc_a2 * b->dc.y2;
            b->dc.x2 = b->dc.x1;
            b->dc.x1 = a;
            b->dc.y2 = b->dc.y1;
            b->dc.y1 = ym;
            amp = ym * out_gain;

            /* oversampled: oscillator -> highpass -> TeeBeeFilter */
            {
                int q;
                for (q = 0; q < nos; q++) {
                    if (wv >= 2) {                                  /* ACID MAX shapes */
                        s = b3_osc_ext(wv, phase, inc, pe);
                    } else if (wv == 0) {
                        t = phase + 0.5f;                       /* Saw303: rises -1..1, jump at p = .5 */
                        if (t >= 1.0f)
                            t -= 1.0f;
                        s = -(t + t - 1.0f - b3_blep(t, inc));     /* Open303 inverts the osc */
                    } else {
                        /* Square303 (shaped saw, minus its mean, polyBLEP on the hard rising edge
                         * at p = 0), times 0.5 as BlendOscillator scales it, inverted */
                        s = 0.5f * (fm_tanhf(69.98419960f * (phase + phase - 1.0f) + 4.37f)
                                    + sq_dc - sq_h2 * b3_blep(phase, inc));
                    }
                    phase += inc;
                    if (phase >= 1.0f)
                        phase -= 1.0f;

                    ym = hp1_b0 * (s - hp1x) + hp1_a1 * hp1y;
                    hp1x = s;
                    hp1y = ym;

                    t = k * y4;                                 /* feedback through the highpass */
                    fby = fb_b0 * (t - fbx) + fb_a1 * fby;
                    fbx = t;
                    t = ym - fby;
                    y1 += 2.0f * f_b0 * (t - y1 + y2);
                    y2 += f_b0 * (y1 - 2.0f * y2 + y3);
                    y3 += f_b0 * (y2 - 2.0f * y3 + y4);
                    y4 += f_b0 * (y3 - 2.0f * y4);
                    os[q] = g2 * y4;
                }
            }
#if BASS303_OS == 1
            s = os[0];
#elif BASS303_OS == 2
            s = lite ? os[0] : b3_decim_hb1(b->hb1, os[0], os[1]);
#else
            s = lite ? os[0] : b3_decim_hb1(b->hb1, b3_decim_hb2(b->hb2, os[0], os[1]), b3_decim_hb2(b->hb2, os[2], os[3]));
#endif

            /* allpass 14 Hz, highpass 24 Hz, notch 7.5 Hz at the base rate */
            t = b->ap_b0 * (s - b->ap_y1) + b->ap_x1;
            b->ap_x1 = s;
            b->ap_y1 = t;
            s = b->hp2_b0 * (t - b->hp2_x1) + b->hp2_a1 * b->hp2_y1;
            b->hp2_x1 = t;
            b->hp2_y1 = s;
            t = b->nt_b0 * (s + b->nt.x2) + b->nt_b1 * b->nt.x1 + b->nt_a1 * b->nt.y1 + b->nt_a2 * b->nt.y2;
            b->nt.x2 = b->nt.x1;
            b->nt.x1 = s;
            b->nt.y2 = b->nt.y1;
            b->nt.y1 = t;
            out[i + j] = t * amp;
        }
    }

    b->phase = phase;
    b->c_inc = inc;
    b->c_b0 = f_b0;
    b->c_k = k;
    b->c_g2 = g2;
    b->c_a = a;
    b->env_y = fm_flush(b->env_y);
    b->rc1_y = fm_flush(b->rc1_y);
    b->rc2_y = fm_flush(b->rc2_y);
    b->amp_y = fm_flush(b->amp_y);
    b->hp1_x1 = hp1x;
    b->hp1_y1 = hp1y;
    b->f_y1 = y1;
    b->f_y2 = y2;
    b->f_y3 = y3;
    b->f_y4 = y4;
    b->fb_x1 = fbx;
    b->fb_y1 = fby;
}

static void bass303_render(bass303_t *b, float *out, int n)
{
    int i;
    if (b->idle) {
        for (i = 0; i < n; i++)
            out[i] = 0.0f;
        b3_idle_advance(b, n);
        return;
    }
    b3_run(b, out, n);

    if (b->drv_type != BASS303_DRV_OFF && b->drv_amt > 0.0f)
        b3_drive_block(b, out, n);

    /* states that decay toward zero while the engine still runs (the idle hold-off, where the
     * amp is exactly 0): flushed once per block, before they can reach denormals */
    b->dc.x1 = fm_flush(b->dc.x1);
    b->dc.x2 = fm_flush(b->dc.x2);
    b->dc.y1 = fm_flush(b->dc.y1);
    b->dc.y2 = fm_flush(b->dc.y2);
    b->s_pre.z1 = fm_flush(b->s_pre.z1);
    b->s_pre.z2 = fm_flush(b->s_pre.z2);
    b->s_post.z1 = fm_flush(b->s_post.z1);
    b->s_post.z2 = fm_flush(b->s_post.z2);
    b->up_lp.z1 = fm_flush(b->up_lp.z1);
    b->up_lp.z2 = fm_flush(b->up_lp.z2);
    b->down_lp.z1 = fm_flush(b->down_lp.z1);
    b->down_lp.z2 = fm_flush(b->down_lp.z2);
    b->r_z[0] = fm_flush(b->r_z[0]);
    b->r_z[1] = fm_flush(b->r_z[1]);
    b->r_z[2] = fm_flush(b->r_z[2]);
    b->r_corr_z = fm_flush(b->r_corr_z);
    b->r_tone_z = fm_flush(b->r_tone_z);
    b->dcb_x1 = fm_flush(b->dcb_x1);
    b->dcb_y1 = fm_flush(b->dcb_y1);

    /* idle: gate off, amp envelope and de-clicker below -120 dB and the drive's DC blocker
     * settled, for BASS303_IDLE_HOLD samples in a row (see bass303.h) */
    if (!b->gate && b->amp_y < 1e-6f && fm_fabsf(b->dc.y1) < 1e-6f && fm_fabsf(b->dc.y2) < 1e-6f
        && fm_fabsf(b->dcb_y1) < 1e-6f) {
        b->quiet += n;
        if (b->quiet >= BASS303_IDLE_HOLD) {
            b3_clear_audio_state(b);
            b->idle = 1;
        }
    } else {
        b->quiet = 0;
    }
}
