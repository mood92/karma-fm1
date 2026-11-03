/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ > VARY (2026-11): chance by level, a synth step's own chance, AMOUNT, LOOK / SEED, velocity randomness, ratchet chance
 *   default  every setting at its default plays every hit, as before
 *   level    a GHOST hit at ~30 % plays ~30 % of the passes; NORM and HARD hits are never touched
 *   amount   AMOUNT 0 plays every hit whatever the chances
 *   look     HOLD: the same hits on every pass for one SEED (and across a restart); another SEED differs; LIVE differs
 *   vel      VEL RND moves a hit's velocity within +- the setting; 0 changes nothing
 *   ratchet  RATCHET 0: only the first hit of a ratcheted step; 127: all of them
 *   step     a synth step's own chance: code 0 always, 31 never, 16 about half
 *   midi     a dropped hit sends no MIDI note
 * Exit status: the number of failed checks. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? i : 0; }
#include "../firmware/src/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/arranger_scene.c"

static uint64_t blk;
typedef struct { uint64_t blk; uint8_t note, vel; } hit_t;
static hit_t hits[200000];
static uint32_t nhits;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && nhits < 200000u) {
                hits[nhits].blk = blk;
                hits[nhits].note = drums.v[k].note;
                hits[nhits].vel = drums.v[k].vel;
                nhits++;
            }
    blk++;
}
static void mo_drain(uint32_t *on, uint32_t *off, uint32_t *onDrum, uint32_t *last_note)
{
    while (mo_r != mo_w) {
        uint32_t p = midi_out_q[mo_r % MQ], st = (p >> 8) & 0xF0u, ch = (p >> 8) & 15u;
        mo_r++;
        if (st == 0x90u && (p >> 24)) { (*on)++; if (ch == 9u) (*onDrum)++; else if (ch == 0u) *last_note = (p >> 16) & 127u; }
        else if (st == 0x80u || st == 0x90u) (*off)++;
    }
}
static void vary_defaults(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        vr_lv[i][0] = vr_lv[i][1] = vr_lv[i][2] = 127;
        vr_amt[i] = 127;
        vr_vel[i] = 0;
        vr_rat[i] = 127;
    }
    vr_look = 0;
    vr_seed = 0;
}
static void reset(uint32_t bpm)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    nhits = 0;
    blk = 0;
    song.g[G_BPM] = (int16_t)bpm;
    song.rec = 0;
    rec_wait = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    kb_prev = 0;
    vary_defaults();
}
static int fails;
static void check(int ok, const char *what)
{
    printf("vary: %-76s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
/* a one-step drum loop: lane 0 (kick, note 36) at the level asked, rat = ratchet - 1; `passes` steps are played */
static uint32_t drum_passes(uint32_t lvl, uint32_t rat, uint32_t passes)
{
    uint32_t step_blocks;
    reset(120);
    TDRUM->p[P_SLEN] = 1;
    dstep_set(&TDRUM->dstep[0], 0, lvl, rat);
    step_blocks = (uint32_t)((double)FS * 60.0 / 120.0 / 4.0 / CTL + 0.5);   /* a 1/16 step */
    return step_blocks * passes;
}
static void play_blocks(uint32_t n)
{
    uint32_t k;
    nhits = 0;
    blk = 0;
    transport_req = 1;
    for (k = 0; k < n; k++)
        run_block();
}
static uint32_t kicks(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < nhits; i++)
        n += hits[i].note == 36;
    return n;
}
/* the blocks of every kick: a fingerprint of which passes played */
static uint64_t fingerprint(void)
{
    uint32_t i;
    uint64_t h = 1469598103934665603ull;
    for (i = 0; i < nhits; i++)
        if (hits[i].note == 36)
            h = (h ^ (hits[i].blk * 31u + hits[i].vel)) * 1099511628211ull;
    return h;
}

#define PASSES 200u
static void t_default_and_levels(void)
{
    uint32_t n, nb, k;
    nb = drum_passes(LV_GHOST, 0, PASSES);
    play_blocks(nb);
    n = kicks();
    check(n >= PASSES - 2u, "default: a ghost hit plays on every pass");
    nb = drum_passes(LV_GHOST, 0, PASSES);
    vr_lv[TRK_DRUM][0] = 38;                         /* ~30 % */
    play_blocks(nb);
    n = kicks();
    printf("vary:   ghost at 30%%: %u of %u\n", n, PASSES);
    check(n > PASSES * 18u / 100u && n < PASSES * 42u / 100u, "GHOST 30 %: about 30 % of the passes play it");
    for (k = 0; k < 3u; k++) {
        static const uint8_t L[3] = {LV_NORM, LV_HARD, LV_SOFT};
        nb = drum_passes(L[k], 0, 60u);
        vr_lv[TRK_DRUM][0] = 0;                      /* GHOST never: no effect on a NORM / HARD hit; SOFT has its own */
        play_blocks(nb);
        n = kicks();
        if (k < 2u)
            check(n >= 58u, k ? "a HARD hit ignores the GHOST chance" : "a NORM hit ignores the GHOST chance");
        else
            check(n >= 58u, "a SOFT hit ignores the GHOST chance");
    }
    nb = drum_passes(LV_HARD, 0, 100u);
    vr_lv[TRK_DRUM][2] = 0;
    play_blocks(nb);
    check(kicks() == 0u, "HARD 0 %: a HARD hit never plays");
    nb = drum_passes(LV_SOFT, 0, 100u);
    vr_lv[TRK_DRUM][1] = 0;
    play_blocks(nb);
    check(kicks() == 0u, "SOFT 0 %: a SOFT hit never plays");
}
static void t_amount(void)
{
    uint32_t nb = drum_passes(LV_GHOST, 0, 100u), half;
    vr_lv[TRK_DRUM][0] = 0;
    vr_amt[TRK_DRUM] = 0;
    play_blocks(nb);
    check(kicks() >= 98u, "AMOUNT 0: every hit plays whatever the chance (a tight groove)");
    nb = drum_passes(LV_GHOST, 0, 200u);
    vr_lv[TRK_DRUM][0] = 0;
    vr_amt[TRK_DRUM] = 64;                           /* half of the dropping: GHOST 0 -> plays ~half */
    play_blocks(nb);
    half = kicks();
    printf("vary:   ghost 0%%, amount 50%%: %u of 200\n", half);
    check(half > 60u && half < 140u, "AMOUNT 50 %: a hit set to 0 % plays about half the time");
}
static void t_look(void)
{
    uint64_t a, b, c, d, e, f;
    uint32_t nb, sb = (uint32_t)((double)FS * 60.0 / 120.0 / 4.0 / CTL + 0.5), i, sd;
    for (sd = 0; sd < 5u; sd++) {                     /* (a 16-step loop of ghost kicks: a look is which of the 16 play) */
        reset(120);
        TDRUM->p[P_SLEN] = 16;
        for (i = 0; i < 16u; i++) dstep_set(&TDRUM->dstep[i], 0, LV_GHOST, 0);
        vr_lv[TRK_DRUM][0] = 64; vr_look = 1; vr_seed = (uint8_t)(sd == 4u ? 3u : sd);
        play_blocks(sb * 16u);
        if (sd == 0) a = fingerprint();
        if (sd == 1) c = fingerprint();
        if (sd == 3) b = fingerprint();
        if (sd == 4) d = fingerprint();
    }
    check(b == d, "HOLD, SEED 3 twice (a restart): exactly the same hits");
    check(a != c && a != b && c != b, "HOLD, SEEDs 0, 1, 3: three different variations");
    reset(120);
    TDRUM->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i++) dstep_set(&TDRUM->dstep[i], 0, LV_GHOST, 0);
    vr_lv[TRK_DRUM][0] = 64; vr_look = 0;
    play_blocks(sb * 16u * 6u);
    {   /* LIVE: six passes of the 16 steps do not all play the same steps */
        uint32_t mark[6], p, diff = 0;
        memset(mark, 0, sizeof mark);
        for (i = 0; i < nhits; i++)
            if (hits[i].note == 36) {
                uint32_t step = (uint32_t)(hits[i].blk / sb);
                if (step < 96u) mark[step / 16u] |= 1u << (step % 16u);
            }
        for (p = 1; p < 6u; p++) diff |= mark[p] != mark[0];
        e = diff;
        check(e == 1u, "LIVE: a new roll on every pass of the loop");
    }
    /* HOLD repeats within one run: pass k and pass k + len play the same: a 4-step loop, 2 loops compared */
    reset(120);
    TDRUM->p[P_SLEN] = 4;
    { uint32_t i; for (i = 0; i < 4u; i++) dstep_set(&TDRUM->dstep[i], 0, LV_GHOST, 0); }
    vr_lv[TRK_DRUM][0] = 64; vr_look = 1; vr_seed = 9;
    {
        uint32_t sb = (uint32_t)((double)FS * 60.0 / 120.0 / 4.0 / CTL + 0.5), same = 1, p, i;
        uint8_t mark[8][4];
        memset(mark, 0, sizeof mark);
        play_blocks(sb * 4u * 8u);
        for (i = 0; i < nhits; i++)
            if (hits[i].note == 36) {
                uint32_t step = (uint32_t)(hits[i].blk / sb);
                if (step < 32u) mark[step / 4u][step % 4u] = 1;
            }
        for (p = 1; p < 8u; p++)
            for (i = 0; i < 4u; i++)
                same &= mark[p][i] == mark[0][i];
        f = same;
        check(f == 1u, "HOLD: a 4-step loop plays the same hits on every pass");
    }
}
static void t_vel(void)
{
    uint32_t i, nb, inrange = 1, distinct = 0;
    uint8_t seen[128];
    nb = drum_passes(LV_GHOST, 0, 100u);
    play_blocks(nb);
    for (i = 0; i < nhits; i++)
        inrange &= hits[i].note != 36 || hits[i].vel == 42;
    check(inrange, "VEL RND 0: a GHOST hit stays at its velocity");
    nb = drum_passes(LV_GHOST, 0, 200u);
    vr_vel[TRK_DRUM] = 10;
    play_blocks(nb);
    memset(seen, 0, sizeof seen);
    inrange = 1;
    for (i = 0; i < nhits; i++)
        if (hits[i].note == 36) {
            inrange &= hits[i].vel >= 32u && hits[i].vel <= 52u;
            if (!seen[hits[i].vel]) { seen[hits[i].vel] = 1; distinct++; }
        }
    printf("vary:   VEL RND 10: %u distinct velocities\n", distinct);
    check(inrange && distinct >= 12u, "VEL RND 10: velocities move within 32..52, with variety");
    nb = drum_passes(LV_HARD, 0, 100u);
    vr_vel[TRK_DRUM] = 40;
    play_blocks(nb);
    inrange = 1;
    for (i = 0; i < nhits; i++)
        inrange &= hits[i].note != 36 || (hits[i].vel >= 87u && hits[i].vel <= 127u);
    check(inrange, "VEL RND 40 on a HARD hit (127): never above 127");
}
static void t_ratchet(void)
{
    uint32_t nb, n;
    nb = drum_passes(LV_NORM, 3, 40u);               /* x4 */
    play_blocks(nb);
    n = kicks();
    check(n >= 38u * 4u && n <= 41u * 4u, "RATCHET 100 %: a x4 step plays its 4 hits");
    nb = drum_passes(LV_NORM, 3, 40u);
    vr_rat[TRK_DRUM] = 0;
    play_blocks(nb);
    n = kicks();
    check(n >= 38u && n <= 41u, "RATCHET 0 %: only the first hit of each step plays");
    nb = drum_passes(LV_NORM, 3, 100u);
    vr_rat[TRK_DRUM] = 64;
    play_blocks(nb);
    n = kicks();
    printf("vary:   ratchet 50%%: %u hits of %u steps (1 + 3 x 50%%)\n", n, 100u);
    check(n > 100u * 21u / 10u && n < 100u * 29u / 10u, "RATCHET 50 %: the further hits play about half the time");
}
static void t_synth(void)
{
    uint32_t i, nb, starts, last, base = 0;
    static const uint8_t code[3] = {0, 31, 16};
    for (i = 0; i < 3u; i++) {
        reset(120);
        host_preset(&trk[0], 0, 7);
        put_step(&trk[0], 0, 1, (const uint8_t[]){60}, ST_NOTE, (uint32_t)code[i] << SF_CH_SH);
        trk[0].p[P_SLEN] = 1;
        transport_req = 1;
        starts = 0; last = vage;
        nb = (uint32_t)((double)FS * 60.0 / 120.0 / 4.0 / CTL + 0.5) * 200u;
        while (blk < nb) {
            run_block();
            if (vage != last) starts += vage - last;
            last = vage;
        }
        if (i == 0) check(starts >= 198u, "a synth step with chance code 0 plays every pass (as before)");
        if (i == 1) check(starts == 0u, "chance code 31: the step never plays");
        if (i == 2) {
            printf("vary:   step chance code 16: %u of 200\n", starts);
            check(starts > 70u && starts < 130u, "chance code 16 (~48 %): about half the passes play it");
        }
    }
    /* a chord with one GHOST note at 0 %: the other notes play, the ghost never (MIDI note-ons against the same chord at 100 %) */
    for (i = 0; i < 2u; i++) {
        uint32_t on = 0, off = 0, od = 0, ln = 0, k;
        reset(120);
        usb.config = 1;
        mo_r = mo_w;
        song.g[G_MIDI] = 1;
        host_preset(&trk[0], 0, 7);
        put_step(&trk[0], 0, 3, (const uint8_t[]){60, 64, 67}, ST_NOTE, 0);
        trk[0].step[0].lvl = LV_GHOST << 4;           /* note 2 (G) is a ghost */
        trk[0].p[P_SLEN] = 1;
        vr_lv[0][0] = i ? 0 : 127;
        transport_req = 1;
        nb = (uint32_t)((double)FS * 60.0 / 120.0 / 4.0 / CTL + 0.5) * 50u;
        for (k = 0; k < nb; k++) {
            run_block();
            mo_drain(&on, &off, &od, &ln);
        }
        song.g[G_MIDI] = 0;
        usb.config = 0;
        if (!i) base = on;
        else {
            printf("vary:   chord of 3: %u MIDI notes with all three, %u with the ghost dropped\n", base, on);
            check(base >= 145u && on >= 95u && on <= 102u, "a chord's GHOST note at 0 %: the other two notes still go out, 2 a step");
        }
    }
}
static void t_midi(void)
{
    uint32_t k, on = 0, off = 0, od = 0, ln = 0, nb;
    reset(120);
    usb.config = 1;
    mo_r = mo_w;
    song.g[G_MIDI] = 1;
    host_preset(&trk[0], 0, 7);
    put_step(&trk[0], 0, 1, (const uint8_t[]){60}, ST_NOTE, 31u << SF_CH_SH);
    trk[0].p[P_SLEN] = 1;
    TDRUM->p[P_SLEN] = 1;
    dstep_set(&TDRUM->dstep[0], 0, LV_GHOST, 0);
    vr_lv[TRK_DRUM][0] = 0;
    transport_req = 1;
    nb = (uint32_t)((double)FS * 60.0 / 120.0 / 4.0 / CTL + 0.5) * 40u;
    for (k = 0; k < nb; k++) {
        run_block();
        mo_drain(&on, &off, &od, &ln);
    }
    check(on == 0u, "MIDI OUT: a dropped step (synth) and a dropped hit (drum) send no note");
    vr_lv[TRK_DRUM][0] = 127;
    for (k = 0; k < nb; k++) {
        run_block();
        mo_drain(&on, &off, &od, &ln);
    }
    printf("vary:   MIDI after the chance is back: %u note-ons, %u on the drum channel\n", on, od);
    check(od >= 38u, "MIDI OUT: the same drum hit plays again with the chance back at 100 %");
    song.g[G_MIDI] = 0;
    usb.config = 0;
}
int main(void)
{
    t_default_and_levels();
    t_amount();
    t_look();
    t_vel();
    t_ratchet();
    t_synth();
    t_midi();
    printf("vary: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
