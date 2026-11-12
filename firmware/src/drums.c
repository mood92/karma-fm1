/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* GM drum part (track 4): the SAMPLE engine's KIT set (General MIDI percussion map,
 * tools/gen_samples.py GM_KIT), played by its step pattern, the keys when track 4
 * is selected, and its own MIDI channel (GLO -> DRUMS, default 10). Its own voices
 * (outside the parts' voice budget). One-shots: note-offs are ignored; a closed or
 * pedal hi-hat chokes the open one. LEVEL / REV / DLY: GLO > DRUMS (G_DRLVL, G_DRREV, G_DRDLY);
 * PAN and MUTE: the drum track's P_PAN / P_MUTE. Rendered from the audio ISR. */
#define NDRUM 6
#include "drum_synth.c"       /* synthesised kits (DS_KITS) */
/* P_E0 was unused on the drum track: it holds the kit. 0..4: the HEX kits (hex_drums.c, the LXR-02's drum voices
 * ported in lxr_dsp.c; they took the places of the
 * GM sample kit and its four treatments, gone with the samples), 5..: the synthesised kits, then (2.4) USR1..USR4:
 * a user sample slot as a kit (the web editor's DRUM KIT: a sound per lane, zone lo..hi = the lane's note),
 * and USR3+4: one kit over the slots USR3 and USR4 (a lane's sound in either) */
#define DRUM_SAMPLED 5u
#define DRUM_USR (DRUM_SAMPLED + DS_NKITS)
#define DRUM_PAIR (DRUM_USR + SMP_USER_SLOTS)   /* USR3+4: one kit in two slots (the editor splits it), ~15 s */
#define DRUM_SYN (DRUM_PAIR + 1u)                /* SLOOP 2.5: SYN1..SYN4, your synthesised kits (drum_synth.c dsu) */
#define DRUM_KITS (DRUM_SYN + DSU_N)
static const char *const DRUM_KIT_NAMES[] = {"HEX BOOM", "HEX ACID", "HEX METAL", "HEX GLITCH", "HEX ROOM", DS_KIT_NAME_LIST,
                                             "USR1", "USR2", "USR3", "USR4", "USR3+4",
                                             "MY HEX 1", "MY HEX 2", "MY HEX 3", "MY HEX 4"};
static const char *const DRUM_KIT_STYLES[] = {"ELECTRO", "TECHNO", "INDUSTRIAL", "GLITCH", "SOFT", DS_KIT_STYLE_LIST,
                                              "YOUR KIT", "YOUR KIT", "YOUR KIT", "YOUR KIT", "BIG KIT",
                                              "YOUR HEX", "YOUR HEX", "YOUR HEX", "YOUR HEX"};
_Static_assert(sizeof(DRUM_KIT_NAMES) / sizeof(DRUM_KIT_NAMES[0]) == DRUM_KITS, "a name per kit");
static uint32_t drum_kit(void) { return (uint32_t)clamp(TDRUM->p[P_E0], 0, DRUM_KITS - 1); }
#define DRUM_DEFAULT_KIT DRUM_SAMPLED   /* power-on: 808 */

/* each sound's own sends, on top of the kit's (GLO > DRUMS REV, DLY): [lane] {reverb 0..15, delay 0..7}; the
 * kit page's KNOB 3 and 4 (ui_studio.c), kept with the project (project.c proj_ds_put) */
static uint8_t drum_send[DRUM_LANES][2];
/* the CLOUD (cloud.c, a send as the reverb and the delay): what the tracks send it this block, each synth track's
 * send and each drum sound's own (0..127; the FX > CLOUD page's SEND) */
static int32_t cloud_bus[CTL];
static uint8_t cloud_send[NPART], drum_cloud[DRUM_LANES];
static uint8_t cloud_resync;              /* they were set from elsewhere (a project): the page's SEND takes its value again */
#define DRUM_SEND_REV 15
#define DRUM_SEND_DLY 7

static struct {
    voice_t v[NDRUM];
    uint8_t lane[NDRUM];         /* the sound each voice plays (its sends) */
    uint32_t age;
    int16_t set;                 /* SMP_SETS index of "PERC" (GM map), -1 = none */
    int32_t tail;                /* declick: the last output of cut voices, decaying */
    int32_t peak;                /* largest |output| since the UI last looked (TRACKS meter) */
    uint8_t kit[NDRUM];
    int32_t filter[NDRUM], env[NDRUM];
    uint8_t synth[NDRUM];        /* the voice plays a synthesised kit (ds[]) */
    dsv_t ds[NDRUM];
    hex_v_t hx[NDRUM];           /* .. or a HEX kit (synth[] 2) */
    volatile uint16_t hits;      /* bit per lane hit since the UI last looked (pads, key LEDs) */
    volatile uint8_t kick;       /* a kick was hit (fx.c DUCK) */
    int32_t a0, a1;              /* the drum track's mute / solo attenuation over this block (fx.c), Q15, 0 = heard */
} drums = {.set = -2};

static int32_t ds_buf[CTL];

static int32_t drum_set(void)
{
    uint32_t i;
    if (drums.set == -2) {
        drums.set = -1;
        for (i = 0; i < SMP_NSETS; i++)
            if (str_eq(SMP_SETS[i].name, "PERC"))
                drums.set = (int16_t)i;
    }
    return drums.set;
}

/* ------------------------------------------------------------ lanes --- */
/* The drum track's 16 sounds, one per white key, F3 (kick) .. G5 (cowbell): kicks, snare and clap,
 * the hi-hats, rim and a second snare, the toms, the cymbals, the percussion. Each plays a GM note
 * (the sampled kits: their samples; the synthesised kits: drum_synth.c DS_MAP). A black key plays
 * the lane of the white key left of it (two fingers on one sound). */
static const uint8_t LANE_NOTE[DRUM_LANES] = {36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56};
static const char *const LANE_NAME[DRUM_LANES] = {
    "KICK", "KICK 2", "SNARE", "CLAP", "HAT", "OPEN HAT", "PEDAL", "RIM",
    "SNARE 2", "LOW TOM", "HI TOM", "CRASH", "RIDE", "SHAKER", "CONGA", "COWBELL"};
static const char *const LANE_SHORT[DRUM_LANES] = {           /* 5 characters: tiles, dials */
    "kick", "kick2", "snare", "clap", "hat", "open", "pedal", "rim",
    "snr 2", "tom l", "tom h", "crash", "ride", "shake", "conga", "bell"};
/* a GM note (MIDI in, old projects) -> its lane: the nearest sound of the 16 (35..81; below: kick, above: shaker) */
static const uint8_t LANE_OF_GM[81 - 35 + 1] = {
    /* 35 */ 1, 0, 7, 2, 3, 8, 9, 4, 9, 6,
    /* 45 */ 9, 5, 10, 10, 11, 10, 12, 11, 12, 13,
    /* 55 */ 11, 15, 11, 13, 12, 14, 14, 14, 14, 14,
    /* 65 */ 14, 14, 15, 15, 13, 13, 15, 15, 13, 13,
    /* 75 */ 7, 7, 7, 14, 14, 15, 15};
static uint32_t lane_of_note(uint32_t note)
{
    return note < 35u ? 0u : note > 81u ? 13u : LANE_OF_GM[note - 35u];
}
/* the lane of key k (0 = F3 .. 26 = G5): white keys in order, a black key the white key left of it */
static uint32_t lane_of_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* from F */
    int32_t i = W[k % 12u];
    if (i < 0)
        i = W[(k - 1u) % 12u];
    return (uint32_t)((int32_t)(k / 12u) * 7 + i) & 15u;
}
/* the white key of lane l (key index), for the key LEDs */
static uint32_t key_of_lane(uint32_t l)
{
    static const uint8_t K[7] = {0, 2, 4, 6, 7, 9, 11};      /* F G A B C D E */
    return (l / 7u) * 12u + K[l % 7u];
}

/* drum steps: a lane's bit, level, ratchet */
static int dstep_has(const dstep_t *s, uint32_t l) { return (s->on[(l >> 3) & 1u] >> (l & 7u)) & 1u; }
static uint32_t dstep_lvl(const dstep_t *s, uint32_t l) { return (s->lvl[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_rat(const dstep_t *s, uint32_t l) { return (s->rat[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_mask(const dstep_t *s) { return (uint32_t)s->on[0] | (uint32_t)s->on[1] << 8; }
static void dstep_set(dstep_t *s, uint32_t l, uint32_t lvl, uint32_t rat)   /* lane on, with its level and ratchet */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] |= (uint8_t)(1u << (l & 7u));
    s->lvl[b] = (uint8_t)((s->lvl[b] & ~(3u << sh)) | (lvl & 3u) << sh);
    s->rat[b] = (uint8_t)((s->rat[b] & ~(3u << sh)) | (rat & 3u) << sh);
}
static void dstep_clr(dstep_t *s, uint32_t l)                                /* lane off */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] &= (uint8_t)~(1u << (l & 7u));
    s->lvl[b] &= (uint8_t)~(3u << sh);
    s->rat[b] &= (uint8_t)~(3u << sh);
}

/* the zone a sampled drum voice plays (drum_on: v->s[4]): SMP_ZONES[i], or a user kit's, DZ_USR + slot * 16 + zone */
#define DZ_USR (1 << 16)                 /* (above every SMP_ZONES index) */
static const smp_zone_t *drum_zone_of(const voice_t *v)
{
    int32_t z = v->s[4];
    return z >= DZ_USR ? &usr_zone[((uint32_t)(z - DZ_USR) >> 4) % SMP_USER_SLOTS][(uint32_t)z & 15u] : &SMP_ZONES[(uint32_t)z];
}
/* a user kit (KIT USR1..3): the zone of slot k for the lane note ln (the lane's GM note), -1 = none */
static int32_t drum_usr_zone(uint32_t k, uint32_t ln)
{
    uint32_t j;
    for (j = 0; j < usr_nz[k] && j < 16u; j++)
        if (ln >= usr_zone[k][j].lo && ln <= usr_zone[k][j].hi)
            return DZ_USR + (int32_t)(k * 16u + j);
    return -1;
}

/* the velocity of a level: as played (LV_NORM: vel), ghost, soft, hard */
static uint32_t lvl_vel(uint32_t lvl, uint32_t vel)
{
    static const uint8_t V[4] = {0, 42, 72, 127};
    return lvl & 3u ? V[lvl & 3u] : vel;
}
/* a MIDI velocity -> the level it records as (keys play 100: LV_NORM) */
static uint32_t vel_lvl(uint32_t vel)
{
    return vel < 56u ? LV_GHOST : vel < 88u ? LV_SOFT : vel < 116u ? LV_NORM : LV_HARD;
}

/* the metronome (GLO > GLOBAL > CLICK, and the REC count-in), 2.4.1: a voice of its own, mixed after the drum
 * track (fx.c mix_block), so it is heard with every kit (a user kit has no wood block: it was silent), whatever
 * the drum track's mute, a solo, its filter or slicer. A short sine tick: 1568 Hz on a bar's first beat, 1047 Hz
 * on the others, about 12 ms to fade by 1/e; its level follows GLO > DRUMS > LVL (100: as loud as the old
 * wood block). It costs a few hundred cycles a block while it sounds, nothing after */
static struct { uint32_t ph, inc; int32_t env, gain; } click;   /* env: Q30, 0 = silent */
static uint32_t click_n;                         /* clicks so far, and the last one's accent (the host tests count them) */
static uint8_t click_acc;
static void click_on(int accent)
{
    click.ph = 0;
    click.inc = accent ? (uint32_t)(((uint64_t)1568u << 32) / FS) : (uint32_t)(((uint64_t)1047u << 32) / FS);
    click.gain = accent ? 20000 : 12000;
    click.env = 1 << 30;
    click_acc = (uint8_t)(accent != 0);
    click_n++;
}
static void click_render(int32_t *ml, int32_t *mr, uint32_t n)
{
    uint32_t i;
    int32_t g;
    if (!click.env)
        return;
    g = (int32_t)((int64_t)click.gain * song.g[G_DRLVL] / 100);
    for (i = 0; i < n; i++) {
        int32_t v = (int32_t)(((int64_t)sine_i(click.ph) * (click.env >> 15) >> 15) * g >> 15);
        ml[i] += v;
        mr[i] += v;
        click.ph += click.inc;
        click.env -= click.env >> 9;                 /* about 12 ms to 1/e */
    }
    if (click.env < (1 << 18))
        click.env = 0;                                /* (-72 dB: done, about 100 ms) */
}

/* a synthesised voice: note on the 16 sounds snd (a factory kit, SYN1..4), the oldest voice stolen */
static void __attribute__((noinline)) drum_synth_on(uint32_t note, uint32_t vel, uint32_t kit, const dsnd_t *snd, uint32_t crush,
                                                    const hex_snd_t *hex)   /* (a HEX kit: hex, snd unused) */
{
    voice_t *v = &drums.v[0];
    uint32_t i;
    if (note == 42u || note == 44u)                 /* hi-hat choke */
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active && drums.v[i].note == 46u) {
                drums.v[i].active = 0;
                drums.tail += drums.v[i].s[7];
            }
    for (i = 0; i < NDRUM; i++) {
        if (!drums.v[i].active) {
            v = &drums.v[i];
            break;
        }
        if (drums.v[i].age < v->age)
            v = &drums.v[i];
    }
    if (v->active)
        drums.tail += v->s[7];
    i = (uint32_t)(v - drums.v);
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->active = 1;
    v->s[7] = 0;
    v->age = ++drums.age;
    drums.synth[i] = hex ? 2 : 1;
    drums.lane[i] = (uint8_t)(note < 128u ? lane_of_note(note) : 0u);
    drums.kit[i] = (uint8_t)kit;
    if (hex)
        hex_on(&drums.hx[i], hex, note, vel);
    else
        ds_on(&drums.ds[i], snd, crush, note, vel);
}
/* the editor's audition (editor_dsyn.c DSYN_PLAY): queued, played by the audio ISR (drum_audition_poll) */
static volatile uint8_t dsu_aud_k, dsu_aud_lane, dsu_aud_vel;
static void drum_audition_poll(void)
{
    uint32_t vel = dsu_aud_vel;
#if MAIN_MENU_NO_DRUMS
    (void)vel;
    return;
#endif
    if (!vel)
        return;
    dsu_aud_vel = 0;
    {
        const dsu_kit_t *u = dsu_kit(dsu_aud_k);
        drum_synth_on(DS_LANE_NOTE[dsu_aud_lane % DS_LANES], vel, DRUM_SYN + dsu_aud_k % DSU_N, 0, 0, u->s);
    }
}

static void drum_on(uint32_t note, uint32_t vel)
{
    int32_t si = drum_set();
    const smp_set_t *set;
    voice_t *v = &drums.v[0];
    uint32_t i, zi = 0xFFFFu, kit = drum_kit();
    int32_t zid = -1;
#if MAIN_MENU_NO_DRUMS
    (void)note; (void)vel; (void)si; (void)set; (void)v; (void)i; (void)zi; (void)kit; (void)zid;
    return;            /* the drum track never sounds: nothing below is reachable, the linker drops its kits */
#endif
    if (note != 76u && note != 77u)                 /* the pads and key LEDs (not the click's wood block) */
        drums.hits |= (uint16_t)(1u << lane_of_note(note));
    if (note == 35u || note == 36u)
        drums.kick = 1;                             /* (DUCK) */
    if (kit >= DRUM_SYN) {                          /* SYN1..SYN4: your synthesised kit */
        const dsu_kit_t *u = dsu_kit(kit - DRUM_SYN);
        drum_synth_on(note, vel, kit, 0, 0, u->s);
        return;
    }
    if (kit >= DRUM_USR) {                          /* a user kit: the lane's sound, as its note (the hat choke) */
        if (note == 76u || note == 77u)
            return;                                 /* (the click's wood block: not a lane) */
        note = LANE_NOTE[lane_of_note(note)];
        if (kit == DRUM_PAIR) {                     /* USR3+4: the lane's sound in USR3, else in USR4 */
            zid = drum_usr_zone(2, note);
            if (zid < 0)
                zid = drum_usr_zone(3, note);
        } else {
            zid = drum_usr_zone(kit - DRUM_USR, note);
        }
        if (zid < 0)
            return;                                 /* (an empty slot, or no sound on this lane) */
    } else if (kit < DRUM_SAMPLED) {                /* a HEX kit */
        drum_synth_on(note, vel, kit, 0, 0, HEX_KITS[kit].s);
        return;
    } else {                                        /* synthesised kit */
        drum_synth_on(note, vel, kit, DS_KITS[kit - DRUM_SAMPLED].s, DS_KITS[kit - DRUM_SAMPLED].crush, 0);
        return;
    }
    if (zid < 0) {                                  /* the GM sample kit */
        if (si < 0)
            return;
        set = &SMP_SETS[si];
        for (i = 0; i < set->nz; i++)
            if (note >= SMP_ZONES[set->z0 + i].lo && note <= SMP_ZONES[set->z0 + i].hi)
                zi = set->z0 + i;
        if (zi == 0xFFFFu)
            return;
        zid = (int32_t)zi;
    }
    if (note == 42u || note == 44u)                 /* hi-hat choke */
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active && drums.v[i].note == 46u) {
                drums.v[i].active = 0;
                drums.tail += drums.v[i].s[7];          /* fade what it was playing, not a step */
            }
    for (i = 0; i < NDRUM; i++) {                   /* free voice, else the oldest */
        if (!drums.v[i].active) {
            v = &drums.v[i];
            break;
        }
        if (drums.v[i].age < v->age)
            v = &drums.v[i];
    }
    if (v->active)
        drums.tail += v->s[7];                      /* stolen voice: fade its last value */
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->active = 1;
    v->s[7] = 0;
    v->age = ++drums.age;
    v->s[4] = zid;
    v->ph[0] = v->ph[1] = 0;
    v->s[0] = v->s[1] = v->s[2] = 0;
    {
        const smp_zone_t *z = drum_zone_of(v);
        v->s[3] = sample_next(z, v, 0);
        v->s[5] = (int32_t)((pow2_q16((int32_t)note * 16 - z->root16) >> 8) * (z->rate >> 8));
    }
    {
        uint32_t vi = (uint32_t)(v - drums.v);
        int32_t shift = kit == 1u ? (note <= 36u ? -5 : -2) : kit == 3u ? 2 : kit == 4u ? -1 : 0;
        drums.kit[vi] = (uint8_t)kit;
        drums.lane[vi] = (uint8_t)lane_of_note(note);
        drums.synth[vi] = 0;
        drums.filter[vi] = 0;
        drums.env[vi] = 32767;
        if (shift) v->s[5] = (int32_t)((pow2_q16((int32_t)note * 16 + shift * 16 - SMP_ZONES[zi].root16) >> 8) * (SMP_ZONES[zi].rate >> 8));
    }
}

/* the reverb and delay sends of voice k, Q15: the kit's plus its sound's own; own_only: the sound's alone */
static inline void drum_voice_sends(uint32_t k, int own_only, int32_t *rs, int32_t *ds)
{
    uint32_t ln = drums.lane[k] % DRUM_LANES;
    int32_t r = drum_send[ln][0] * 127 / DRUM_SEND_REV, d = drum_send[ln][1] * 127 / DRUM_SEND_DLY;
    if (own_only) {
        *rs = r * 258;
        *ds = d * 258;
        return;
    }
    r += song.g[G_DRREV];
    d += song.g[G_DRDLY];
    *rs = send_thr((r > 127 ? 127 : r) * 258, thr_rev);   /* (2.6: the THROWs) */
    *ds = send_thr((d > 127 ? 127 : d) * 258, thr_dly);
}

/* adds the drums into the dry mix and the reverb and delay sends; mono != 0: into mono instead, before
 * the pan and the kit's sends (the SLICER, slicer.c slicer_drums, does those after it), each sound's own
 * sends from here (so they stay the sound's with the SLICER on) */
/* the synthesised kits (HEX, the SYN ones) as loud as the synth tracks (2026-10-20: they were 5 .. 13 dB under):
 * a gain, then a soft knee rounds each hit's peak instead of the master limiter pumping the mix */
#ifndef DRUM_BOOST_Q8
#define DRUM_BOOST_Q8 1024                      /* x 4 (+12 dB) */
#endif
#ifndef DRUM_KNEE
#define DRUM_KNEE 9000                          /* (where the soft knee starts) */
#endif
/* and each kit level with the others (Q8; measured on a house beat, tests: about -16.5 LUFS): HEX BOOM, ACID,
 * METAL, GLITCH, ROOM, then 808, 909 (the rest: 1). The HEX kits' (2026-10-26, the LXR's voices): their grooves as
 * loud as the HEX kits' before (tests/hex_demo.c), each sound's LEVEL set by tools/level_hexkits.py */
static const uint16_t DRUM_KIT_TRIM[7] = {160, 145, 96, 149, 183, 215, 256};
static inline void drums_mix(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly, int32_t *mono, uint32_t n)
{
    uint32_t k, i;
#if MAIN_MENU_NO_DRUMS
    int32_t lvl = 0, send, dsend, csend, pk = drums.peak;   /* the drum track never sounds, whatever G_DRLVL is */
#else
    int32_t lvl = song.g[G_DRLVL] * 200, send, dsend, csend, pk = drums.peak;
#endif
    int32_t kboost = (int32_t)((DRUM_BOOST_Q8 * (drum_kit() < 7u ? DRUM_KIT_TRIM[drum_kit()] : 256u)) >> 8);
    int32_t pan = trk[TRK_DRUM].p[P_PAN], gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
    for (i = 0; i < n && drums.tail; i++) {         /* declick tail, ~0.4 ms */
        if (mono) {
            mono[i] += drums.tail;
        } else {
            ml[i] += drums.tail;
            mr[i] += drums.tail;
        }
        drums.tail -= drums.tail / 16 + (drums.tail > 0 ? 1 : drums.tail < 0 ? -1 : 0);
    }
#if MAIN_MENU_NO_DRUMS
    (void)lvl; (void)send; (void)dsend; (void)csend; (void)kboost; (void)gl; (void)gr;
    drums.peak = pk;
    return;
#endif
    for (k = 0; k < NDRUM; k++) {               /* synthesised voices: render, then as below */
        voice_t *v = &drums.v[k];
        uint32_t m = n < CTL ? n : CTL;             /* (the mix runs in blocks of CTL) */
        if (!v->active || !drums.synth[k])
            continue;
        drum_voice_sends(k, mono != 0, &send, &dsend);
        csend = drum_cloud[drums.lane[k] % DRUM_LANES] * 258;   /* the sound's own send to the CLOUD (cloud.c) */
        if (!(drums.synth[k] == 2 ? hex_render(&drums.hx[k], ds_buf, m) : ds_render(&drums.ds[k], ds_buf, m)))
            v->active = 0;
        for (i = 0; i < m; i++) {
            int32_t s = mulq15(ds_buf[i], mulq15(lvl, 32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
            s = soft_knee((s * kboost) >> 8, DRUM_KNEE);   /* louder, the hits' tips rounded (2026-10-20) */
            v->s[7] = s;
            if (csend)
                cloud_bus[i] += mulq15(s, csend);
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                if (send)
                    rev[i] += mulq15(s, send);
                if (dsend)
                    dly[i] += mulq15(s, dsend);
                continue;
            }
            ml[i] += (s * gl) >> 12;
            mr[i] += (s * gr) >> 12;
            if (send)
                rev[i] += mulq15(s, send);
            if (dsend)
                dly[i] += mulq15(s, dsend);
        }
        if (!v->active) {
            drums.tail += v->s[7];                  /* ended: no step at the end */
            v->s[7] = 0;
        }
    }
    for (k = 0; k < NDRUM; k++) {
        voice_t *v = &drums.v[k];
        const smp_zone_t *z;
        if (drums.synth[k])
            continue;
        z = drum_zone_of(v);
        uint32_t frac = v->ph[1], stepq = (uint32_t)v->s[5];   /* Q16 source samples per output (drum_on) */
        int32_t g;
        if (!v->active)
            continue;
        drum_voice_sends(k, mono != 0, &send, &dsend);
        csend = drum_cloud[drums.lane[k] % DRUM_LANES] * 258;   /* the sound's own send to the CLOUD (cloud.c) */
        g = mulq15(lvl, v->vel * 258);
        g += g * 3 >> 2;                           /* x1.75 (+5 dB): as loud as the synthesised kits */
        for (i = 0; i < n; i++) {
            int32_t s;
            frac += stepq;
            while (frac >= 65536u) {
                frac -= 65536u;
                v->s[2] = v->s[3];
                if (v->ph[0] >= z->n) {
                    v->active = 0;
                    break;
                }
                v->s[3] = sample_next(z, v, 0);
            }
            if (!v->active)
                break;
            s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
            s = mulq15(s, mulq15(g, 32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
            if (drums.kit[k] == 1u || drums.kit[k] == 4u) {
                drums.filter[k] += (s - drums.filter[k]) >> (drums.kit[k] == 1u ? 2 : 1);
                s = drums.filter[k];
                if (drums.kit[k] == 4u) s = (s >> 8) * 256;
            } else if (drums.kit[k] == 2u) {
                s = mulq15(s, drums.env[k]);
                drums.env[k] -= (drums.env[k] >> 11) + 1;
                if (drums.env[k] <= 0) v->active = 0;
            }
            v->s[7] = s;
            if (csend)
                cloud_bus[i] += mulq15(s, csend);
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                if (send)
                    rev[i] += mulq15(s, send);
                if (dsend)
                    dly[i] += mulq15(s, dsend);
                continue;
            }
            ml[i] += (s * gl) >> 12;
            mr[i] += (s * gr) >> 12;
            if (send)
                rev[i] += mulq15(s, send);
            if (dsend)
                dly[i] += mulq15(s, dsend);
        }
        v->ph[1] = frac;
    }
    drums.peak = pk;
}
static void drums_render(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly, uint32_t n) { drums_mix(ml, mr, rev, dly, 0, n); }
static void drums_render_mono(int32_t *rev, int32_t *dly, int32_t *mono, uint32_t n) { drums_mix(0, 0, rev, dly, mono, n); }
