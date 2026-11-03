/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ > GEN (firmware/src/gen_seq.c): CHANGE 0 keeps the loop, all the way rolls every step; the notes it writes
 * stay in the track's SCALE and inside OCT .. RANGE; DENS how many play; ON per track from the page; NEW.
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/gen_seq_test tests/gen_seq_test.c -lm */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"

static int fails;
static void ck(int ok, const char *what) { printf("gen: %-70s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

static int same(const track_t *t, const step_t *was, uint32_t len)
{
    return !memcmp(t->step, was, sizeof(step_t) * len);
}

int main(void)
{
    static step_t was[NSTEP];
    track_t *t;
    uint32_t i, k, len, inscale = 1, inrange = 1, notes = 0, changed;
    host_tracks_init();
    song.sel = 0;
    for (i = 0; i < NPART; i++) {                        /* C minor; PROG SWAY (i bVII), a chord every 2 bars */
        trk[i].p[P_ROOT] = 0;
        trk[i].p[P_SCALE] = 2;
        trk[i].p[P_SLEN] = 64;                           /* (GEN keeps a track's LEN: these tests on 64 steps) */
    }
    song.g[G_GKPROG] = 1; song.g[G_GKBARS] = 1;
    song.g[G_GNDENS] = 45; song.g[G_GNRANGE] = 2; song.g[G_GNOCT] = 3; song.g[G_GNACC] = 25; song.g[G_GNSLD] = 13;
    gk_load();
    gk_fit(&gk_ui, &trk[0]);
    ck(gk_ui.len == 64u && gk_ui.cst == 32u && gk.n == 2u && gk.deg[1] == 6u, "HARMONY: SWAY on 64 steps: i bVII, 32 each");
    {   /* BASS */
        uint32_t ok_key = 1, ok_root = 1, ok_rh = 1, ok_reg = 1, r;
        t = &trk[0];
        for (r = 0; r < 30u; r++) {
            uint16_t m0 = 0;
            gen_new(t);
            len = trk_len(t);
            ok_rh &= len == 64u;
            for (k = 0; k < 4u; k++) {
                uint16_t m = 0;
                int first = 1;
                for (i = 0; i < 16u; i++) {
                    const step_t *s = &t->step[k * 16u + i];
                    if (s->time != ST_NOTE || !s->n)
                        continue;
                    m |= (uint16_t)(1u << i);
                    ok_key &= (SCALE_MASK[2] >> (s->note[0] % 12u)) & 1u;
                    ok_reg &= s->note[0] >= 36u && s->note[0] < 36u + 24u;
                    if (first && k % 2u == 0u)
                        ok_root &= s->note[0] % 12u == (k ? 10u : 0u);   /* C, then Bb */
                    first = 0;
                }
                if (!k)
                    m0 = m;
                ok_rh &= m == m0;
            }
        }
        ck(ok_key, "BASS: every note in the key (C minor)");
        ck(ok_root, "BASS: a chord's first hit is its root (C, then Bb)");
        ck(ok_rh, "BASS: one rhythm in every bar; LEN kept (64)");
        ck(ok_reg, "BASS: from C2, under two octaves");
    }
    {   /* LINE: a slow sound held, a short one struck on the beats */
        uint32_t ok_tone = 1, ok_held = 1, ok_reg = 1, struck = 0, r, moves = 0, notes2 = 0;
        t = &trk[1];
        t->p[P_ATK] = 20; t->p[P_REL] = 60;
        for (r = 0; r < 30u; r++) {
            gen_new(t);
            for (i = 0; i < 64u; i++) {
                const step_t *s = &t->step[i];
                if (s->time == ST_NOTE && s->n) {
                    uint32_t pc = s->note[0] % 12u, c = i < 32u ? 0u : 10u;
                    uint32_t rel = (pc + 12u - c) % 12u;
                    ok_tone &= c ? (rel == 0u || rel == 4u || rel == 7u || rel == 10u || rel == 2u)
                                 : (rel == 0u || rel == 3u || rel == 7u || rel == 10u);
                    ok_held &= i % 8u == 0u;
                    {                                     /* (not the note before again) */
                        int32_t pv = gen_held(t, i, -1);
                        notes2 += pv >= 0;
                        moves += pv >= 0 && pv != s->note[0];
                    }
                    ok_reg &= s->note[0] >= 55u && s->note[0] <= 74u;
                } else {
                    ok_held &= s->time == ST_TIE;
                }
            }
        }
        ck(ok_tone, "LINE: chord tones of each chord (i: C Eb G Bb; bVII: Bb D F Ab, C)");
        ck(ok_held, "LINE: a slow sound: long notes held (TIE), a change each bar (DENS: half way)");
        ck(notes2 > 0u && moves * 10u >= notes2 * 9u, "LINE: each new note another than the one before (no note stuck)");
        ck(ok_reg, "LINE: G3 .. D5");
        t->p[P_ATK] = 0; t->p[P_REL] = 10;
        for (r = 0; r < 10u; r++) {
            gen_new(t);
            for (i = 0; i < 64u; i++)
                struck += i % 16u && i % 4u == 0u && t->step[i].time == ST_NOTE;
        }
        ck(struck > 40u, "LINE: a short sound struck again on the beats");
        {                                                 /* ELEMENTS PICK with a long release (PICKED BAR): struck */
            uint8_t eng = t->engine;
            int16_t m = t->p[P_E0];
            t->engine = ENGI_ELEM; t->p[P_E0] = EM_PICK; t->p[P_ATK] = 0; t->p[P_REL] = 56;
            for (r = 0, struck = 0; r < 10u; r++) {
                gen_new(t);
                for (i = 0; i < 64u; i++)
                    struck += i % 16u && i % 4u == 0u && t->step[i].time == ST_NOTE;
            }
            ck(struck > 100u, "LINE: ELEMENTS PICK (a long release) struck on the beats, not held");
            {
                uint32_t ties = 0, same = 0, notes = 0, j, last = 0;
                for (j = 0; j < 64u; j++) {
                    ties += t->step[j].time == ST_TIE;
                    if (t->step[j].time == ST_NOTE && t->step[j].n) {
                        same += notes && t->step[j].note[0] == last;
                        last = t->step[j].note[0];
                        notes++;
                    }
                }
                ck(!ties && notes > 12u && same * 2u < notes, "LINE: struck: short notes (no ties), mostly moving through the chord");
            }
            t->p[P_E0] = EM_BOW; gen_new(t);
            for (i = 0, struck = 0; i < 64u; i++)
                struck += i % 8u != 0u && t->step[i].time == ST_NOTE;
            ck(struck == 0u, "LINE: ELEMENTS BOW holds its notes");
            t->engine = eng; t->p[P_E0] = m;
        }
    }
    {   /* LEAD */
        uint32_t ok_key = 1, ok_beat = 1, ok_end = 1, ok_rep = 1, ok_reg = 1, r;
        t = &trk[2];
        for (r = 0; r < 30u; r++) {
            uint32_t last = 0, hits0 = 0, hits1 = 0;
            gen_new(t);
            for (i = 0; i < 64u; i++) {
                const step_t *s = &t->step[i];
                if (s->time != ST_NOTE || !s->n)
                    continue;
                last = i;
                hits0 += i < 16u;
                hits1 += i >= 16u && i < 32u;
                ok_key &= (SCALE_MASK[2] >> (s->note[0] % 12u)) & 1u;
                ok_reg &= s->note[0] >= 58u && s->note[0] <= 79u;
            }
            for (i = 0; i < 64u; i++) {
                const step_t *s = &t->step[i];
                uint32_t pc, c, rel;
                if (s->time != ST_NOTE || !s->n || i % 4u || i == last)
                    continue;
                pc = s->note[0] % 12u; c = i < 32u ? 0u : 10u; rel = (pc + 12u - c) % 12u;
                ok_beat &= c ? (rel == 0u || rel == 4u || rel == 7u) : (rel == 0u || rel == 3u || rel == 7u);
            }
            ok_end &= t->step[last].note[0] % 12u == 0u;
            ok_rep &= hits0 == hits1 || gpart[2].form == 2u;   /* (A A ..: the motif again) */
        }
        ck(ok_key, "LEAD: every note in the key");
        ck(ok_beat, "LEAD: on the beats, tones of the bar's chord");
        ck(ok_end, "LEAD: the phrase ends on the tonic");
        ck(ok_rep, "LEAD: the motif comes back (the second bar as the first)");
        ck(ok_reg, "LEAD: Bb3 .. G5");
    }
    {   /* DRIFT */
        t = &trk[0];
        gen_new(t);
        len = trk_len(t);
        memcpy(was, t->step, sizeof(step_t) * len);
        gen_on = 1;
        song.g[G_GNCHG] = 0;
        for (k = 0; k < 64u; k++)
            for (i = 0; i < len; i++)
                gen_tick(t, i);
        ck(same(t, was, len), "DRIFT 0: 64 passes, the loop as it was (locked)");
        song.g[G_GNCHG] = 64;
        for (k = 0, changed = 0; k < 8u; k++)
            for (i = 0; i < len; i++) {
                step_t s0 = t->step[i];
                gen_tick(t, i);
                changed += memcmp(&s0, &t->step[i], sizeof s0) != 0;
            }
        for (i = 0, inscale = 1; i < len; i++)
            if (t->step[i].time == ST_NOTE && t->step[i].n)
                inscale &= (SCALE_MASK[2] >> (t->step[i].note[0] % 12u)) & 1u;
        ck(changed > 0u && inscale, "DRIFT: the bass changes, still in the key");
        for (k = 0, i = 0; i < 16u; i++) {
            uint32_t j;
            for (j = 16u; j < len; j += 16u)
                k += (t->step[i].time == ST_NOTE) != (t->step[i + j].time == ST_NOTE);
        }
        ck(k == 0u, "DRIFT: the rhythm still the same in every bar");
        gen_on = 0;
        song.g[G_GNCHG] = 32;
    }
    song.g[G_GNDENS] = 64;
    {                                                     /* the page: ON follows the selected track */
        song.sel = 1; frames(2);
        memset(trk[1].step, 0, sizeof(step_t) * trk_len(&trk[1]));
        memcpy(was, trk[1].step, sizeof(step_t) * trk_len(&trk[1]));
        song.g[G_GNON] = 1; frames(2);
        ck(gen_on == 2u, "page: GEN ON on track 2 sets its bit only");
        ck(!same(&trk[1], was, trk_len(&trk[1])), "page: GEN ON writes a new loop at once (no wait for DRIFT)");
        song.sel = 0; frames(2);
        ck(song.g[G_GNON] == 0, "page: back on track 1 it shows OFF");
        song.sel = 1; frames(2);
        ck(song.g[G_GNON] == 1, "page: track 2 again: ON");
        memcpy(was, trk[1].step, sizeof(step_t) * trk_len(&trk[1]));
        song.g[G_GNNEW] = 1; frames(2);
        ck(!same(&trk[1], was, trk_len(&trk[1])) && !song.g[G_GNNEW], "page: NEW rolls track 2's loop, the knob back to --");
        {                                                 /* a key on the GEN page: GEN on, the loop from that note */
            uint32_t pg = ui.page, kk;
            go_home(); song.sel = 2; ly_lock = LY_PLAY; frames(2);
            gen_on = 0; memset(trk[2].step, 0, sizeof(step_t) * trk_len(&trk[2]));
            key(4); frames(2);
            ck(gen_on == 0u && !trk[2].step[0].n, "a key off the GEN page: GEN untouched");
            open_family(FAM_SEQ); frames(2);
            for (kk = 0; kk < NPAGES; kk++)
                if (!strcmp(PAGES[kk].title, "GEN"))
                    ui.page = (uint8_t)kk;
            frames(2);
            key(4); frames(2);
            ck(gen_on == 4u && song.g[G_GNON] == 1, "a key on the GEN page (track 3): GEN ON");
            ck(trk[2].step[0].time == ST_NOTE && trk[2].step[0].n, "... the lead written");
            {                                             /* HARMONY: a key sets the KEY only, GEN stays off */
                uint32_t pk = ui.page;
                for (kk = 0; kk < NPAGES; kk++)
                    if (!strcmp(PAGES[kk].title, "HARMONY"))
                        ui.page = (uint8_t)kk;
                song.sel = 1; gen_on = 4u; frames(3);
                key(9); frames(3);
                ck(gen_on == 4u && trk[0].p[P_ROOT] == (int16_t)(last_note % 12u),
                   "a key on the HARMONY page: the KEY moves, GEN not turned on");
                ui.page = (uint8_t)pk; frames(2);
            }
            ck(trk[0].p[P_ROOT] == (int16_t)(last_note % 12u) && trk[2].p[P_ROOT] == trk[0].p[P_ROOT] &&
               song.g[G_AGROOT] == trk[0].p[P_ROOT], "... its note the KEY of every synth track and ACID GEN");
            gen_on = 2u; song.sel = 1; ui.page = (uint8_t)pg; frames(2);
        }
        {                                                 /* HARMONY: KEY turned: every synth track and GEN part in D */
            uint32_t ok = 1, j;
            gen_on = 7u; song.g[G_GKROOT] = 4; frames(2);   /* (from another key: the change is seen) */
            song.g[G_GKROOT] = 2; frames(2);
            for (j = 0; j < NPART; j++) {
                ok &= trk[j].p[P_ROOT] == 2;
                for (i = 0; i < trk_len(&trk[j]); i++)
                    if (trk[j].step[i].time == ST_NOTE && trk[j].step[i].n)
                        ok &= (SCALE_MASK[2] >> ((trk[j].step[i].note[0] + 10u) % 12u)) & 1u;   /* D minor */
            }
            ck(ok && song.g[G_AGROOT] == 2, "HARMONY: KEY D: the three tracks' ROOT, ACID GEN's, every GEN part in D minor");
            song.g[G_GKPROG] = 0; frames(2);
            ck(gk.n == 1u && trk_len(&trk[0]) == 64u, "HARMONY: PROG DRONE: one chord, the parts written again, LEN kept");
            gen_on = 2u; song.g[G_GKPROG] = 1; song.g[G_GKROOT] = 0; frames(2);
        }
        {                                                 /* GEN keeps LEN (2026-10-27: it jumped 16 -> 64); BARS sets it */
            uint32_t ok = 1, j, seen = 0, r;
            gen_on = 3u;
            for (r = 0; r < 20u; r++) {
                trk[0].p[P_SLEN] = 16;
                gen_new(&trk[0]);
                ok &= trk_len(&trk[0]) == 16u;
                for (j = 8; j < 16u && !seen; j++)        /* SWAY on 16 steps: C for 8, Bb for 8 */
                    if (trk[0].step[j].time == ST_NOTE && trk[0].step[j].n)
                        seen = 1, ok &= trk[0].step[j].note[0] % 12u == 10u;
                seen = 0;
            }
            ck(ok, "GEN keeps the track's LEN (16); SWAY's two chords 8 steps each, the second's first hit its root");
            song.g[G_GKBARS] = 2; frames(2);
            ck(trk_len(&trk[0]) == 64u && trk_len(&trk[1]) == 64u && trk_len(&trk[2]) == 64u && trk[2].p[P_SLEN] == 64,
               "HARMONY: BARS 4: the GEN tracks' loops 64 steps");
            trk[2].p[P_SLEN] = 16; gen_on = 3u;
            song.g[G_GKBARS] = 0; frames(2);
            ck(trk_len(&trk[0]) == 16u && trk_len(&trk[1]) == 16u && trk_len(&trk[2]) == 16u, "HARMONY: BARS 1: 16 steps (GEN tracks only)");
            trk[2].p[P_SLEN] = 64;
            song.g[G_GKBARS] = 1; frames(2);
            ck(trk_len(&trk[0]) == 32u && trk_len(&trk[2]) == 64u, "HARMONY: BARS 2: 32 steps; a track without GEN keeps its own");
            gen_on = 2u;
            for (j = 0; j < NPART; j++)
                trk[j].p[P_SLEN] = 64;
        }
        song.sel = NPART; frames(2);
        song.g[G_GNON] = 1; frames(2);
        ck(song.g[G_GNON] == 0 && gen_on == 2u, "the drum track: GEN stays OFF");
    }
    {   /* DRUM GEN */
        track_t *d = TDRUM;
        uint32_t dl = 16, ok, p, x, y, n;
        d->p[P_SLEN] = 16;
        dl = trk_len(d);
        memset(d->dstep, 0, sizeof d->dstep);
        for (i = 0; i < dl; i++)
            d->dstep[i].on[1] = 0x81;                     /* SNARE 2 and COWBELL on every step: never touched */
        song.g[G_DGMODE] = DG_MAPM; song.g[G_DGCHAOS] = 0;
        song.g[G_DGK] = song.g[G_DGS] = song.g[G_DGH] = 127;
        memset(dg_pert, 0, sizeof dg_pert);
        for (ok = 1, y = 0; y < 5u; y++)
            for (x = 0; x < 5u; x++) {                    /* each node of the map, at its corner: exactly Grids' */
                const uint8_t *nd = DG_NODE[DG_MAP[x][y]];
                song.g[G_DGX] = (int16_t)(x < 4u ? x * 32u : 127u);
                song.g[G_DGY] = (int16_t)(y < 4u ? y * 32u : 127u);
                if (x == 4u || y == 4u)
                    continue;                             /* (127 * 2 = 254: not quite the last node) */
                drum_gen_fill(d);
                for (p = 0; p < 3u; p++)
                    for (i = 0; i < dl; i++)
                        ok &= dstep_has(&d->dstep[i], DG_LANE[p]) == (nd[p * 32u + i] > 0u) &&
                              (!dstep_has(&d->dstep[i], DG_LANE[p]) || (dstep_lvl(&d->dstep[i], DG_LANE[p]) == LV_HARD) == (nd[p * 32u + i] > 192u));
            }
        ck(ok, "MAP: at each node, KICK SNARE HATS are Grids' pattern (accents over 192 HARD)");
        song.g[G_DGX] = 40; song.g[G_DGY] = 90;
        drum_gen_fill(d);
        for (ok = 1, i = 0; i < dl; i++)
            ok &= d->dstep[i].on[1] == 0x81;
        ck(ok, "the other lanes (SNARE 2, COWBELL) untouched");
        song.g[G_DGK] = song.g[G_DGS] = song.g[G_DGH] = 0;
        drum_gen_fill(d);
        for (n = 0, i = 0; i < dl; i++)
            for (p = 0; p < 3u; p++)
                n += dstep_has(&d->dstep[i], DG_LANE[p]);
        ck(n == 0u, "MAP: density 0, no hits");
        song.g[G_DGMODE] = DG_EUCL;
        song.g[G_DGK] = 32; song.g[G_DGS] = 16; song.g[G_DGH] = 127; song.g[G_DGROT] = 4;
        drum_gen_fill(d);
        for (ok = 1, i = 0; i < dl; i++) {
            ok &= dstep_has(&d->dstep[i], 0) == (i % 4u == 0u);          /* 4 kicks in 16: on the beats */
            ok &= dstep_has(&d->dstep[i], 2) == (i == 4u || i == 12u);   /* 2 snares, turned 4: the backbeat */
            ok &= dstep_has(&d->dstep[i], 4);                            /* hats on every step */
        }
        ck(ok, "EUCL: 4 kicks on the beats, 2 snares on 5 and 13 (ROT 4), 16 hats");
        song.g[G_DGMODE] = DG_OFF;
        memcpy(was, d->dstep, sizeof(dstep_t) * dl);
        song.g[G_DGK] = 127;
        for (i = 0; i < dl; i++)
            drum_gen_tick(d, i);
        ck(!memcmp(was, d->dstep, sizeof(dstep_t) * dl), "MODE OFF: the lanes stay as they are");
        song.g[G_DGMODE] = DG_MAPM; song.g[G_DGCHAOS] = 127; song.g[G_DGX] = 64; song.g[G_DGY] = 64;
        song.g[G_DGK] = song.g[G_DGS] = song.g[G_DGH] = 50;
        {
            uint32_t pass, diff = 0;
            step_t prev[16];
            drum_gen_fill(d);
            for (pass = 0; pass < 8u; pass++) {
                memcpy(prev, d->dstep, sizeof(dstep_t) * dl);
                for (i = 0; i < dl; i++)
                    drum_gen_tick(d, i);
                diff += memcmp(prev, d->dstep, sizeof(dstep_t) * dl) != 0;
            }
            ck(diff >= 4u, "CHAOS: the passes differ");
        }
        song.g[G_DGMODE] = DG_OFF; frames(2);
    }
    {   /* SEQ > PLAY: the order (DIR, STRIDE), CHANCE steps */
        track_t *u = &trk[0];
        uint32_t n, ok = 1, seen = 0, hits = 0, moves = 0, prev;
        static const uint8_t PEND[10] = {0, 1, 2, 3, 2, 1, 0, 1, 2, 3};
        song.rec = 0;
        pl_dir[0] = PD_FWD; pl_stride[0] = 1;
        for (n = 0; n < 40u; n++) ok &= play_idx(u, n, 16) == n % 16u;
        ck(ok, "PLAY DIR FWD: 0, 1, 2 .. 15, 0 ..");
        pl_dir[0] = PD_REV;
        for (ok = 1, n = 0; n < 40u; n++) ok &= play_idx(u, n, 16) == 15u - n % 16u;
        ck(ok, "PLAY DIR REV: 15, 14 .. 0, 15 ..");
        pl_dir[0] = PD_PEND;
        for (ok = 1, n = 0; n < 10u; n++) ok &= play_idx(u, n, 4) == PEND[n];
        ck(ok, "PLAY DIR PEND (4 steps): 0 1 2 3 2 1 0 1 2 3 (the ends once)");
        pl_dir[0] = PD_FWD; pl_stride[0] = 3;
        for (ok = 1, n = 0; n < 16u; n++) { seen |= 1u << play_idx(u, n, 16); ok &= play_idx(u, n, 16) == n * 3u % 16u; }
        ck(ok && seen == 0xFFFFu, "PLAY STRIDE 3: 0 3 6 9 .. every step of 16 once a round");
        pl_dir[0] = PD_RAND; pl_stride[0] = 1; seen = 0;
        for (n = 0; n < 400u; n++) seen |= 1u << play_idx(u, n, 16);
        ck(seen == 0xFFFFu, "PLAY DIR RAND: every step comes up");
        pl_dir[0] = PD_WALK; pl_pos[0] = 5; prev = 5; ok = 1;
        for (n = 0; n < 400u; n++) {
            uint32_t q = play_idx(u, n, 16), d = (q + 16u - prev) % 16u;
            ok &= d == 0u || d == 1u || d == 15u;
            moves += d != 0u;
            prev = q;
        }
        ck(ok && moves > 150u, "PLAY DIR WALK: a step back, stay or a step on, never a leap");
        song.rec = 1;
        ck(play_idx(u, 7, 16) == 7u, "PLAY: while recording the steps go in order");
        song.rec = 0; pl_dir[0] = PD_FWD;
        step_fill_set(u, 3, FC_CHANCE);
        pl_chance[0] = 32;
        for (n = 0; n < 4000u; n++) hits += step_plays(u, 3);
        ck(hits > 800u && hits < 1200u, "CHANCE step at 25 %: plays about 1 time in 4");
        pl_chance[0] = 0;
        for (hits = 0, n = 0; n < 400u; n++) hits += step_plays(u, 3);
        ck(hits == 0u && step_plays(u, 4), "CHANCE 0: never; a normal step: always");
        step_fill_set(u, 3, FC_NORM); pl_chance[0] = 64;
    }
    {   /* SEQ > PLAY 2 GHOST: only ghost-level notes / hits roll; a synth step rolls once (a chord is one group) */
        track_t *u = &trk[0], *dr = &trk[TRK_DRUM];
        step_t keep = u->step[5], *gs = &u->step[5];
        dstep_t dkeep = dr->dstep[5], *ds = &dr->dstep[5];
        uint32_t k, bad = 0, plays = 0, m, l0 = 0, l1 = 0;
        memset(gs, 0, sizeof *gs);
        gs->n = 3, gs->time = ST_NOTE, gs->note[0] = 60, gs->note[1] = 64, gs->note[2] = 67;
        gs->lvl = (uint8_t)(LV_GHOST | LV_GHOST << 2 | LV_GHOST << 4);
        pl_ghost[0] = 127;
        ck(ghost_skip(u, 5) == 0u, "GHOST 100 %: nothing skipped (the dice are not even rolled)");
        pl_ghost[0] = 0;
        ck(ghost_skip(u, 5) == 7u, "GHOST 0: a ghost chord stays silent, every note of it");
        pl_ghost[0] = 64;
        for (k = 0; k < 4000u; k++) {
            m = ghost_skip(u, 5);
            bad += m != 0u && m != 7u;                  /* never one note of the chord alone */
            plays += m == 0u;
        }
        ck(!bad && plays > 1700u && plays < 2300u, "GHOST 50 %: a ghost chord plays about half the time, as one group");
        gs->lvl = (uint8_t)(LV_NORM | LV_NORM << 2 | LV_NORM << 4);
        pl_ghost[0] = 0;
        ck(ghost_skip(u, 5) == 0u, "GHOST 0: a chord at the normal level always plays");
        gs->lvl = (uint8_t)(LV_GHOST | LV_NORM << 2 | LV_HARD << 4);
        ck(ghost_skip(u, 5) == 1u, "GHOST 0: only the ghost note of a mixed chord is skipped");
        memset(ds, 0, sizeof *ds);
        dstep_set(ds, 1, LV_GHOST, 0);
        dstep_set(ds, 2, LV_GHOST, 0);
        dstep_set(ds, 3, LV_NORM, 0);
        pl_ghost[TRK_DRUM] = 0;
        ck(ghost_skip(dr, 5) == 6u, "GHOST 0 (drums): the ghost hits are skipped, the normal one plays");
        pl_ghost[TRK_DRUM] = 64;
        for (k = 0; k < 4000u; k++) {
            m = ghost_skip(dr, 5);
            l0 += (m >> 1) & 1u, l1 += (m >> 2) & 1u;
            bad += (m & 1u) || (m & 8u);               /* never a lane that is not ghost */
        }
        ck(!bad && l0 > 1700u && l0 < 2300u && l1 > 1700u && l1 < 2300u, "GHOST 50 % (drums): each ghost hit rolls on its own");
        pl_ghost[0] = pl_ghost[TRK_DRUM] = 127;
        *gs = keep, *ds = dkeep;
    }
    printf(fails ? "gen: FAILED\n" : "gen: all ok\n");
    return fails ? 1 : 0;
}
