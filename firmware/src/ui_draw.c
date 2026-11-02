/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI drawing: status bar (top), columns + gauges, graphs, focus readout,
 * footer (steps + engine / preset / page). */
static void draw_menu(void);
static uint32_t str_hash(uint32_t h, const char *s);
/* (the knob band shows no icon: the look-ups by label, icons.c, are not built in) */
#define param_icon(d, v) ICON_NONE
#define engine_icon(n) ICON_NONE

/* --------------------------------------------------------- drawing --- */
static int is_eng_name(const char *s)                 /* one of the ENGINES[]->name strings */
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (s == ENGINES[e]->name)
            return 1;
    return 0;
}

/* at most 5 characters, and no wider than maxw */
static void fit(char *d, const char *src, const felucca_font_t *f, int32_t maxw)
{
    str_cpy(d, src, is_eng_name(src) ? 8 : 6);           /* engine names are kept whole */
    while (d[0] && text_w(f, d) > maxw)
        d[str_len(d) - 1u] = 0;
}

static int32_t batt_level(void)                         /* ADC ch3 thresholds */
{
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
}
/* charging (a USB host powers us; there is no charger status line): bars fill 1, 2, 3
 * every 0.6 s, so the header is redrawn twice a second at most; else the level */
static int32_t batt_shown(void)
{
    if (usb.config && !usb.suspended)
        return 1 + (int32_t)((fm1_ms / 600u) % 3u);
    return batt_level();
}

/* the header (ui_op.c): the track's tag, the page's title; a message shows in the knob band */
static void draw_head(void)
{
    static const char *const TAG[NTRK] = {"SYN1", "SYN2", "SYN3", "DRUM"};
    const page_t *pg = cur_page();
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    const char *title = ui.home ? "HOME" : pg->scope == SC_ENGINE && !is_drum(TSEL) ? e->page_title[pg->id[0] != P_E0] : pg->title;
    if (mlu.on)                                       /* MIDI LEARN on (2.6, GLO + F#3) */
        title = "LEARN";
    op_header(!is_drum(TSEL) && e != &ENG_GONE ? e->name : TAG[song.sel & 3u],   /* a synth track: its engine, by name */
              TE_COL[song.sel & 3u], title, &ui.head_sig);
}
/* (the bands cover the screen: header, graph, the pages strip, the knob band, the track bar) */
static void draw_frame(void) {}

/* one knob: label, value, unit, gauge (ratio 0..1000, -1 = none), kept for the knob band (ui_op.c op_knobs),
 * which draw_columns draws when the four are set; vc C_DIM: a value that does nothing now. (icon: unused,
 * the labels carry the knob's colour) */
static struct {
    char lab[4][8], val[4][10], unit[4][8];
    int32_t ratio[4];
    uint8_t dim;
} opk;
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                        int32_t ratio, uint32_t icon)
{
    char l[8], v[10], u[8];
    (void)icon;
    fit(l, label, &FONT_S, 54);
    fit(v, val, &FONT_S, is_eng_name(val) ? 56 : 48);   /* engine names whole */
    fit(u, unit, &FONT_S, 54 - text_w(&FONT_S, v) - 3);
    if (c < 4u) {                                       /* (the big values: their own fit) */
        str_cpy(ui.big_l[c], l, 8);
        fit(ui.big_v[c], val, &FONT_L, 112 - (unit[0] ? 4 : 0));
        str_cpy(ui.big_u[c], unit, 8);
        ui.big_c[c] = vc;
        str_cpy(opk.lab[c], l, 8);
        str_cpy(opk.val[c], v, 10);
        str_cpy(opk.unit[c], u, 8);
        opk.ratio[c] = ratio;
        opk.dim = (uint8_t)((opk.dim & ~(1u << c)) | (uint32_t)(vc == C_DIM) << c);
    }
    if (c == ui.hot_col) {
        str_cpy(ui.focus_l, l, 8);
        str_cpy(ui.focus_v, v, 8);
        str_cpy(ui.focus_u, u, 8);
    }
}

/* ENV: the envelope, a segment per knob in its colour (ATK DEC SUS REL). Matches voice.c: attack is linear,
 * decay and release are exponential (env += (target - env) * k each tick, ~99 % after the set time); the
 * time axis is the parameter value (the times themselves are exponential). The track's character
 * (ui_op.c) travels it with the voice that sounds: up the attack, down to the sustain, along it while
 * the note is held, down the release; its level in large digits, GATE lit while a key is down. */
static void graph_adsr(const track_t *t, uint16_t c)
{
    static int32_t plat;                                                /* px along the sustain so far */
    int32_t a = 4 + t->p[P_ATK] * 50 / 127, d = 6 + t->p[P_DEC] * 50 / 127, r = 6 + t->p[P_REL] * 60 / 127;
    int32_t top = 38, bot = 92, sus = t->p[P_SUS] * 1000 / 127;          /* 0..1000 */
    int32_t x0 = 8, x1 = x0 + a, x2 = x1 + d, x3 = 230 - r, i, px, py, y, tx = x0, ty, dpos = -1, rpos = -1;
    int32_t e = 32768;                                                  /* exp(-4.6 u), Q15 */
    const voice_t *v = op_env_voice(t);
    int32_t stage = v ? v->stage : 0, cur = v ? clamp(((v->env >> 14) * 1000) >> 10, 0, 1000) : 0, gate = stage == 1 || stage == 2;
    (void)c;
#define EGY(lvl) (bot - (lvl) * (bot - top) / 1000)
    ty = bot;
    op_env_ghost((uint32_t)(t - trk), 128, 93, cur);                    /* the track's character, behind it all */
    cv_rect(0, bot + 1, 240, 1, TE_G1);
    for (y = top; y < bot; y += 3)                                      /* the corners, down to the floor */
        cv_pset(x1, y, OP_PUR_D);
    for (y = EGY(sus); y < bot; y += 3) {
        cv_pset(x2, y, OP_PUR_D);
        cv_pset(x3, y, OP_PUR_D);
    }
    cv_line(x0, bot, x1, top, TE_COL[0]);                               /* attack: linear */
    px = x1;
    py = top;
    for (i = 1; i <= d; i++) {                                          /* decay: exponential to SUS */
        int32_t lvl;
        e = (e * (32768 - 150733 / d)) >> 15;           /* k^d = exp(-4.6) */
        lvl = sus + ((1000 - sus) * e >> 15);
        cv_line(px, py, x1 + i, EGY(lvl), TE_COL[1]);
        px = x1 + i;
        py = EGY(lvl);
        if (dpos < 0 && lvl <= cur)
            dpos = px;
    }
    cv_line(px, py, x3, EGY(sus), TE_COL[2]);                            /* sustain */
    px = x3;
    py = EGY(sus);
    e = 32768;
    for (i = 1; i <= r; i++) {                                          /* release: exponential to 0 */
        e = (e * (32768 - 150733 / r)) >> 15;
        cv_line(px, py, x3 + i, EGY(sus * e >> 15), TE_COL[3]);
        px = x3 + i;
        py = EGY(sus * e >> 15);
        if (rpos < 0 && (sus * e >> 15) <= cur)
            rpos = px;
    }
    te_disc(x1, top, 2, TE_COL[1]);                                      /* a dot where the next knob takes over */
    te_disc(x2, EGY(sus), 2, TE_COL[2]);
    te_disc(x3, EGY(sus), 2, TE_COL[3]);
    if (stage == 1) {                                                   /* where the voice is on the line */
        plat = 0;
        tx = x0 + a * cur / 1000;
        ty = EGY(cur);
    } else if (stage == 2 && cur > sus + 20 && dpos >= 0) {
        tx = dpos;
        ty = EGY(cur);
    } else if (stage == 2) {                                            /* held: along the sustain, to its end */
        if (plat < x3 - x2)
            plat += 2;
        tx = x2 + (plat < x3 - x2 ? plat : x3 - x2);
        ty = EGY(sus);
    } else if (stage == 3) {
        tx = cur > sus || rpos < 0 ? x3 : rpos;
        ty = EGY(cur > sus ? sus : cur);
    } else {
        plat = 0;
    }
    te_digit(4, -2, 12, 26, 2, (uint32_t)(cur * 99 / 1000) / 10u, v ? C_WHITE : TE_G2);
    te_digit(20, -2, 12, 26, 2, (uint32_t)(cur * 99 / 1000) % 10u, v ? C_WHITE : TE_G2);
    op_box(196, 0, 40, 18, gate ? C_WHITE : TE_G2);
    te_text_c(216, 1, "GATE", gate ? C_WHITE : TE_G2);
    if (v)                                                              /* where the voice is on the line */
        te_disc(tx, ty, 3, C_WHITE);
#undef EGY
}

/* LFO: two cycles of the wave, running (KARMA 3.0): it scrolls with the track's LFO, "now" at x = LFO_NOW with a dot
 * on the value it gives (a dim line there), what it did to the left, what comes to the right */
#define LFO_NOW 160
static void graph_lfo(const track_t *t, uint16_t c)
{
    int32_t x, py = 50, ny = 50;
    uint32_t now = t->lfo_ph, step = 0xFFFFFFFFu / 120u;
    cv_line(0, 50, 239, 50, C_LINE);
    cv_line(LFO_NOW, 4, LFO_NOW, 96, C_LINE);
    for (x = 0; x < 240; x++) {                      /* (lfo_wave only reads the track) */
        uint32_t ph = now + (uint32_t)(x - LFO_NOW) * step;
        int32_t y = 50 - lfo_wave((track_t *)t, ph) * 38 / 32768;
        if (t->p[P_LWAVE] == 4)                      /* RANDOM: a held value a sixth of a cycle, scrolling with it */
            y = 50 - ((int32_t)(((ph / 0x2AAAAAABu) * 2654435761u) >> 16) - 32768) * 38 / 32768;
        if (x)
            cv_line(x - 1, py, x, y, c);
        if (x == LFO_NOW)
            ny = y;
        py = y;
    }
    cv_rect(LFO_NOW - 3, ny - 3, 7, 7, C_WHITE);    /* now */
}

static void graph_steps(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    for (i = 0; i < NSTEP; i++) {                    /* 4 rows of 16 thin bars */
        int32_t x = 6 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u) / 4 * 4, y = 6 + (int32_t)(i / 16u) * 24;
        const step_t *st = &t->step[i];
        if (i >= len)
            continue;
        if (step_on(st))
            cv_rect(x, y, 1, 14, c);
        else if (st->time == ST_TIE)
            cv_rect(x, y + 6, 1, 8, C_DIM);
        else
            cv_rect(x, y + 13, 1, 1, C_DIM);
        if (st->flags & SF_ACCENT)
            cv_rect(x - 1, y - 2, 3, 1, c);
        if ((song.playing && i == t->seq_idx) || i == ui.cursor)
            cv_rect(x - 1, y + 16, 3, 3, C_WHITE);
    }
}
/* the 16 steps the boxes show: STEP, the cursor's; SEQ > GEN, the ones playing */
static uint32_t roll_bank(const track_t *t)
{
    return cur_page()->scope == SC_STEP || !song.playing ? ui.bank : t->seq_idx / 16u;
}
/* STEP page: the cursor's 16-step bank as 2 rows of 8 boxes, each with its note, readable on the
 * small screen. A note: filled in the track's colour (an ACC a white bar on top, a SLD an arrow on
 * to the next step, a chord "+n" small); a TIE: a band carried on from the note before; a rest: an
 * empty box. The cursor's box in a white frame, the step playing all white. */
static void graph_roll(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN], base = roll_bank(t) * 16u;
    for (i = 0; i < 16u; i++) {
        uint32_t si = base + i;
        const step_t *st = &t->step[si];
        int32_t x = 1 + (int32_t)(i % 8u) * 30, y = 2 + (int32_t)(i / 8u) * 46, w = 28, h = 42;
        int play = song.playing && si == t->seq_idx, cur = si == ui.cursor && cur_page()->scope == SC_STEP;
        uint16_t fill = play ? C_WHITE : c, ink = C_BLACK;
        char nm[8];
        if (si >= len) {                             /* past the pattern's end */
            cv_rect(x + w / 2 - 2, y + h / 2, 4, 1, TE_G1);
            continue;
        }
        if (step_on(st)) {
            note_name(nm, st->note[0]);
            if (text_w(&FONT_S, nm) > w - 2)         /* (C#-1: the octave would not fit) */
                str_cpy(nm, N_NOTE[st->note[0] % 12u], 8);
            cv_rect(x, y, w, h, fill);
            if (st->flags & SF_ACCENT)
                cv_rect(x + 2, y + 2, w - 4, 3, play ? C_BLACK : C_WHITE);
            cv_text(x + (w - text_w(&FONT_S, nm)) / 2, y + 12, &FONT_S, nm, ink);
            if (st->n > 1) {                         /* a chord: how many more notes */
                char ch[4];
                ch[0] = '+';
                ch[1] = (char)('0' + (st->n - 1 > 9 ? 9 : st->n - 1));
                ch[2] = 0;
                cv_text(x + (w - text_w(&FONT_S, ch)) / 2, y + 26, &FONT_S, ch, ink);
            }
            if (st->flags & SF_SLIDE) {              /* an arrow on to the next step */
                int32_t k;
                for (k = 0; k < 4; k++)
                    cv_rect(x + w - 2 + k, y + h - 10 + k, 1, 8 - 2 * k, play ? C_BLACK : C_WHITE);
            }
        } else if (st->time == ST_TIE) {             /* the note before, held on */
            cv_rect(x - 2, y + 16, w + 2, 10, play ? C_WHITE : c);
            cv_rect(x, y, w, 1, TE_G2);
            cv_rect(x, y + h - 1, w, 1, TE_G2);
        } else {                                     /* a rest */
            op_box(x, y, w, h, play ? C_WHITE : TE_G2);
            fmt_int(nm, (int32_t)si + 1);             /* its number, to find the way */
            cv_text(x + (w - text_w(&FONT_S, nm)) / 2, y + 12, &FONT_S, nm, play ? C_WHITE : TE_G3);
        }
        if (step_fill(t, si) == FC_CHANCE) {         /* a CHANCE step (SEQ > PLAY): a ? in its corner */
            uint16_t q = step_on(st) && !play ? C_BLACK : play ? C_BLACK : TE_G4;
            cv_rect(x + 2, y + 3, 5, 2, q);
            cv_rect(x + 5, y + 5, 2, 2, q);
            cv_rect(x + 4, y + 7, 2, 1, q);
            cv_rect(x + 4, y + 9, 2, 2, q);
        }
        if (cur) {                                   /* the step the knobs edit */
            op_box(x - 1, y - 1, w + 2, h + 2, C_WHITE);
            op_box(x - 2, y - 2, w + 4, h + 4, C_WHITE);
        }
    }
}
/* DRUM GEN: the 16 steps playing (or the first) of KICK, SNARE and HAT, big: a hit filled in the drums' colour,
 * an accent white; the step playing lit; OFF: grey */
static void graph_dgen(void)
{
    static const char *const NM[3] = {"KICK", "SNARE", "HATS"};
    const track_t *t = TDRUM;
    uint32_t i, p, len = trk_len(t), base = song.playing ? t->seq_idx / 16u * 16u : 0u, off = !song.g[G_DGMODE];
    uint16_t c = off ? TE_G3 : TE_COL[3];
    for (p = 0; p < 3u; p++) {
        int32_t y = 4 + (int32_t)p * 31;
        cv_text(2, y + 6, &FONT_S, NM[p], off ? TE_G3 : TE_G4);
        for (i = 0; i < 16u; i++) {
            uint32_t si = base + i, l = DG_LANE[p];
            int32_t x = 58 + (int32_t)i * 11 + (int32_t)(i / 4u) * 2, w = 9, h = 26;
            const dstep_t *st = &t->dstep[si];
            if (si >= len)
                break;
            if (song.playing && si == t->seq_idx)
                cv_rect(x - 1, y - 2, w + 2, h + 4, TE_G2);
            if (dstep_has(st, l))
                cv_rect(x, y, w, h, dstep_lvl(st, l) == LV_HARD && !off ? C_WHITE : c);
            else
                cv_rect(x + 3, y + h / 2 - 1, 3, 3, TE_G2);
        }
    }
}
static void graph_scale(const track_t *t, uint16_t c)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    uint32_t i, mask = scale_mask(t);
    for (i = 0; i < 12u; i++) {
        uint32_t deg = (i + 12u - (uint32_t)t->p[P_ROOT]) % 12u;
        int32_t x = 6 + (int32_t)i * 19;
        uint16_t col = (mask >> deg) & 1u ? (deg == 0u ? C_WHITE : c) : C_DIM;
        cv_rect(x, BLACK[i] ? 10 : 40, 16, 1, col);
        cv_rect(x + 7, BLACK[i] ? 10 : 40, 1, 40, col);
    }
}

static void graph_fx(const track_t *t, uint16_t c)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t h = t->p[P_DIST + i] * 80 / 127, x = (int32_t)i * 60 + 28;
        cv_rect(x, 10, 1, 80, C_LINE);
        cv_rect(x, 90 - h, 1, h, c);
        cv_rect(x - 3, 90 - h, 7, 1, c);
    }
}
/* SLICER page: the pattern's 16 steps, a 'x' step a full bar; a '.' step: GATE a bar as high as
 * it stays open (DEPTH), STUT hatched (it repeats the last 'x'); the step playing underlined.
 * Grey when the SLICER is OFF. */
static void graph_slicer(const track_t *t, uint16_t c)
{
    uint32_t i, pat = sl_pattern(t), mode = (uint32_t)t->p[P_SLCR], cur = sl[t - trk].idx;
    int32_t open = 70 - t->p[P_SLDEPTH] * 70 / 127;      /* px a closed GATE step keeps */
    uint16_t col = mode == SL_OFF ? C_DIM : c;
    for (i = 0; i < 16u; i++) {
        int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2, y;
        if ((pat >> i) & 1u) {
            cv_rect(x, 10, 11, 70, col);
        } else if (mode == SL_STUT) {
            for (y = 10; y < 80; y += 4)
                cv_rect(x, y, 11, 1, col);
        } else {
            cv_rect(x, 79, 11, 1, C_LINE);
            if (open)
                cv_rect(x, 80 - open, 11, open, C_DIM);
        }
        if (mode != SL_OFF && i == cur)
            cv_rect(x, 85, 11, 3, C_WHITE);
    }
}
static uint32_t steps_hash(const track_t *t)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NSTEP; i++) {
        const step_t *st = &t->step[i];
        h = (h ^ (st->note[0] + st->n * 128u + st->time * 1024u + st->flags * 4096u + st->note[1] * 65536u)) *
            16777619u;
    }
    return h;
}

static uint32_t str_hash(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* a page drawn with its values large (graph_big) */
static int big_page(const page_t *pg)            /* (2.6: the INSERT pages too: their middle stayed empty) */
{
    return (pg->graph == GR_NONE || pg->graph == GR_INS) && pg->scope != SC_SONG && pg->scope != SC_DRUM;
}
static uint32_t graph_signature(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint32_t h = 2166136261u, i;
    if (ui.hot_t && settings.zoom)
        h = str_hash(str_hash(str_hash(h ^ 0x5555u, ui.focus_v), ui.focus_l), ui.focus_u);
    if (!ui.home && big_page(pg))                    /* the big values: as the columns show them */
        for (i = 0; i < 4u; i++)
            h = str_hash(str_hash(str_hash(h ^ ui.big_c[i] * 31u, ui.big_v[i]), ui.big_l[i]), ui.big_u[i]);
    if (ui.home)
        return h ^ (ui.frame / 2u);                  /* scope: redraw every other frame */
    h ^= (uint32_t)pg->graph * 131u + TSEL->eng_req + song.sel * 7777u;
    if (pg->fam == FAM_FX && (pg->graph == GR_FX || big_page(pg)))   /* the wizard: moves while the track sounds */
        h ^= ((is_drum(t) ? drums.peak : t->peak) > 64 || op_mx[song.sel & 3u] || op_hop[song.sel & 3u] ? ui.frame / 2u : 0u) * 40503u;
    else if (big_page(pg) && !is_drum(t))            /* the character's scene (ui_op.c): moves while the track sounds */
        h ^= (t->peak > 64 || op_lvl || op_hit ? ui.frame / 2u : 0u) * 40503u + op_note * 977u + user_of(t) * 31u;
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint32_t)t->p[i]) * 16777619u;
    h ^= (uint32_t)TSEL->preset * 7u + (uint32_t)song.g[G_SLOT] * 13u + TSEL->user * 257u + up_gen * 7919u + ui.uslot * 104729u;
    if (pg->graph == GR_HEX)                         /* the drum sound's picture: as it is edited, the lane */
        h ^= (hex_gen * 977u + drum_lane * 31u + (uint32_t)TDRUM->p[P_E0] * 7u) * 40503u;
    if (pg->graph == GR_ARP && !is_drum(t))          /* ARP: the stones as the keys change, the hops */
        h ^= (t->nheld * 31u + t->arp_note * 977u + (op_arp_hop ? ui.frame + 1u : 0u)) * 40503u;
    if (pg->graph == GR_LFO && !is_drum(t))          /* LFO: the wave runs (every other frame) */
        h ^= (ui.frame / 2u + 1u) * 40503u;
    if (pg->graph == GR_ADSR && !is_drum(t) && op_env_voice(t))   /* ENV: the character follows the voice */
        h ^= (ui.frame / 2u + 1u) * 40503u;
    if (pg->graph == GR_SLCR && t->p[P_SLCR])        /* the SLICER's step playing */
        h ^= (sl[song.sel].idx + 1u) * 2654435761u;
    if (pg->graph == GR_SLOTS)                       /* (a checksum over each slot) */
        for (i = 0; i < 4u; i++)
            h ^= (uint32_t)project_used(i) << (20u + i);
    if (pg->graph == GR_DGEN) {                      /* DRUM GEN: its lanes, the step playing */
        const uint8_t *b = (const uint8_t *)TDRUM->dstep;
        for (i = 0; i < sizeof TDRUM->dstep; i++)
            h = (h ^ b[i]) * 16777619u;
        h ^= (song.playing ? TDRUM->seq_idx + 1u : 0u) * 2654435761u;
    }
    if (pg->graph == GR_STEPS || pg->graph == GR_ROLL) {
        uint32_t ph = song.playing ? t->seq_idx : 0xFFFFu;
        if (pg->graph == GR_ROLL && ph / 16u != roll_bank(t))
            ph = 0xFFFFu;                            /* the roll shows the cursor's bank only */
        h ^= steps_hash(t) + ph * 31u + ui.cursor * 7919u;
    }
    return h;
}
/* preset browser: the list by kind (ui.c BANK), the current one in white */
static void graph_browse(void)
{
    uint32_t total, cur = preset_pos(&total), e, k;
    int32_t row;
    if (!total)
        return;
    for (row = -3; row <= 3; row++) {
        int32_t y = 4 + (row + 3) * 17;
        char tag[5], nm[13];
        int sel = row == 0;
        uint32_t n = (cur + total * 4u + (uint32_t)row) % total;
        e = preset_at(n, &k);
        if (e == NENGINES) {                             /* user preset: "U07" and its name */
            up_slot_label(tag, k);
            up_name(k, nm);
        } else {                                         /* its kind: BASS, KEYS, PAD... */
            str_cpy(tag, preset_kind(n), sizeof tag);
            str_cpy(nm, ENGINES[e]->presets[k].name, sizeof nm);
        }
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, tag, sel ? C_GRAY : C_DIM);
        cv_text(54, y, &FONT_S, nm, sel ? C_WHITE : C_GRAY);
    }
}

/* user preset slots around the selected one: "U07  NAME" / EMPTY, the selected one in white */
static void graph_user(void)
{
    int32_t row, first = clamp((int32_t)ui.uslot - 3, 0, UP_SLOTS - 7);
    for (row = 0; row < 7; row++) {
        uint32_t k = (uint32_t)(first + row);
        int32_t y = 4 + row * 17;
        char tag[4], nm[13];
        int sel = k == ui.uslot, used = up_used(k);
        up_slot_label(tag, k);
        if (used)
            up_name(k, nm);
        else
            str_cpy(nm, "EMPTY", sizeof nm);
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, tag, sel ? C_WHITE : C_GRAY);
        cv_text(54, y, &FONT_S, nm, used ? (sel ? C_WHITE : C_HI) : C_DIM);
    }
}

/* project slots: used / empty, the selected one in white */
static void graph_slots(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t y = 8 + (int32_t)i * 26;
        char b[4];
        int sel = (int32_t)i + 1 == song.g[G_SLOT];
        b[0] = (char)('1' + i);
        b[1] = 0;
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, b, sel ? C_WHITE : C_GRAY);
        cv_text(40, y, &FONT_S, project_used(i) ? "USED" : "EMPTY", project_used(i) ? (sel ? C_WHITE : C_HI) : C_DIM);
    }
}

/* TRACKS page: four channel strips (mixer style), one under each column: number +
 * REC / ARM / MUTE, the sound's short name, then a level fader with the output meter
 * (note activity), the pattern over its LEN (time running down; bar width = notes in
 * the step) and the play head. The selected track is drawn bright. Every part is
 * its own small canvas with its own signature: while the transport runs only the
 * head markers and the meters move. (No ZOOM focus here.) */
#define TS_HEAD_Y (Y_GRAPH + 2)
#define TS_NAME_Y (Y_GRAPH + 20)
#define TS_Y (Y_GRAPH + 38)
#define TS_H 84                                      /* body: rows Y_GRAPH + 38 .. + 121 */
static struct {
    uint32_t head[NTRK], name[NTRK], fader[NTRK], ov[NTRK], mark[NTRK];
    uint8_t meter[NTRK];
} ts;

static int32_t meter_px(int32_t a)                   /* |sample| (Q15) -> px: 6 dB = TS_H / 10 */
{
    int32_t lg = 0, v;
    if (a < 64)
        return 0;
    while ((a >> lg) > 1)
        lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);             /* 8 log2(a): 48 (-54 dB) .. 128 (+6 dB) */
    return clamp((v - 48) * (TS_H - 2) / 80, 0, TS_H - 2);
}

static uint32_t trk_level(uint32_t c)                /* LEVEL 0..127 (the drum track: GLO > DRUMS LEVEL) */
{
    return (uint32_t)(c == TRK_DRUM ? song.g[G_DRLVL] : trk[c].p[P_LEVEL]) & 127u;
}

static void trk_short_name(uint32_t c, char *b)      /* the track's sound, b holds 13 */
{
    const track_t *t = &trk[c];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    if (c == TRK_DRUM)
        str_cpy(b, "DRUM", 13);
    else if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, 13);
    else
        str_cpy(b, e->name, 13);
}

static void draw_tracks(void)
{
    uint32_t c;
    if (ui.force) {
        lcd_fill(0, Y_GRAPH, 240, H_GRAPH, C_BLACK);
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
    }
    for (c = 0; c < NTRK; c++) {
        track_t *t = &trk[c];
        uint32_t x0 = c * 60u + 4u, sel = c == song.sel, lvl = trk_level(c), mute = !lvl || t->p[P_MUTE];
        uint32_t arm = (song.rec >> c) & 1u, st = arm ? (song.playing ? 1u : 2u) : mute ? 3u : 0u, sig;
        uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, row = 0xFFFFu;
        int32_t pk, m;
        char b[16];
        if (c == TRK_DRUM) {
            pk = drums.peak;
            drums.peak = 0;
        } else {
            pk = t->peak;
            t->peak = 0;
        }
        /* head: number (white = selected), REC / ARM / MUTE */
        sig = 1u + sel + st * 2u;
        if (ui.force || sig != ts.head[c]) {
            ts.head[c] = sig;
            b[0] = (char)('1' + c);
            b[1] = 0;
            cv_begin(54, 16, C_BLACK);
            cv_text(0, 0, &FONT_S, b, sel ? C_WHITE : C_GRAY);
            if (sel)
                cv_rect(0, 15, 8, 1, C_WHITE);
            if (st == 1u || st == 2u) {
                cv_rect(14, 5, 6, 6, st == 1u ? C_WHITE : C_AMB);
                cv_text(24, 0, &FONT_S, st == 1u ? "REC" : "ARM", st == 1u ? C_WHITE : C_AMB);
            } else if (st) {
                cv_text(14, 0, &FONT_S, "MUTE", C_DIM);
            }
            cv_blit(x0, TS_HEAD_Y);
        }
        /* the sound: preset name (user preset, DRUM), cut to the column */
        trk_short_name(c, b);
        while (b[0] && text_w(&FONT_S, b) > 54)
            b[str_len(b) - 1u] = 0;
        sig = str_hash(2u + sel, b);
        if (ui.force || sig != ts.name[c]) {
            ts.name[c] = sig;
            cv_begin(54, 16, C_BLACK);
            cv_text(0, 0, &FONT_S, b, sel ? C_HI : C_DIM);
            cv_blit(x0, TS_NAME_Y);
        }
        /* fader (LEVEL) + meter of the output */
        m = mute ? 0 : meter_px(pk);
        if (m < ts.meter[c] - 3)
            m = ts.meter[c] - 3;                     /* falls ~20 dB/s */
        ts.meter[c] = (uint8_t)(m < 0 ? 0 : m);
        sig = 1u + lvl + ts.meter[c] * 128u + sel * 65536u + (sel && ui.hot_t && ui.hot_col == 1u) * 131072u +
              (t->p[P_MUTE] != 0) * 262144u;
        if (ui.force || sig != ts.fader[c]) {
            int32_t fy = (int32_t)(TS_H - 2u) - (int32_t)lvl * (TS_H - 4) / 127;
            ts.fader[c] = sig;
            cv_begin(12, TS_H, C_BLACK);
            cv_rect(3, 0, 1, TS_H, C_LINE);
            if (lvl) {
                cv_rect(2, fy, 3, TS_H - fy, C_DIM);
                cv_rect(0, fy - 1, 7, 2, (sig & 131072u) ? C_WHITE : sel ? C_HI : C_GRAY);
            } else {
                cv_rect(0, TS_H - 2, 7, 2, C_DIM);
            }
            if (ts.meter[c])
                cv_rect(9, TS_H - ts.meter[c], 3, ts.meter[c], C_AMB);
            cv_blit(x0, TS_Y);
        }
        /* the pattern: one band per step over LEN, a note step as wide as its notes */
        sig = steps_hash(t) * 31u + len * 7u + sel;
        if (ui.force || sig != ts.ov[c]) {
            uint32_t i;
            ts.ov[c] = sig;
            cv_begin(34, TS_H, C_BLACK);
            cv_rect(17, 0, 1, TS_H, C_LINE);
            for (i = 0; i < len; i++) {
                const step_t *s = &t->step[i];
                int32_t r0 = (int32_t)(i * TS_H / len), r1 = (int32_t)((i + 1u) * TS_H / len), h = r1 - r0;
                if (h > 2)
                    h--;                             /* a gap between steps when there is room */
                if (h < 1)
                    h = 1;
                if (step_on(s)) {
                    int32_t w = 2 + 6 * (int32_t)s->n;
                    cv_rect(17 - w / 2, r0, w, h, (s->flags & SF_ACCENT) && sel ? C_WHITE : sel ? C_HI : C_GRAY);
                } else if (s->time == ST_TIE) {
                    cv_rect(16, r0, 3, r1 - r0, C_DIM);
                }
            }
            cv_blit(x0 + 14u, TS_Y);
        }
        /* play head: a small arrow right of the pattern (white while recording) */
        if (song.playing)
            row = ((t->seq_idx % len) * 2u + 1u) * TS_H / (2u * len);
        sig = 1u + row + (st == 1u) * 65536u;
        if (ui.force || sig != ts.mark[c]) {
            int32_t i;
            ts.mark[c] = sig;
            cv_begin(5, TS_H, C_BLACK);
            if (row != 0xFFFFu)
                for (i = -2; i <= 2; i++) {
                    int32_t w = 5 - 2 * (i < 0 ? -i : i);
                    cv_rect(5 - w, (int32_t)row + i, w, 1, st == 1u ? C_WHITE : C_AMB);
                }
            cv_blit(x0 + 49u, TS_Y);
        }
    }
}

/* oscilloscope of the output, triggered on a rising zero crossing */
static void graph_scope(uint16_t c)
{
    static int16_t snap[SCOPE_N];
    uint32_t w = scope_w, i, trig = 0;
    int32_t py = 50, x, peak = 1500;
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak)
            peak = snap[i];
        else if (-snap[i] > peak)
            peak = -snap[i];
    }
    for (i = 1; i < SCOPE_N - 240u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) {
            trig = i;
            break;
        }
    cv_line(0, 50, 239, 50, C_LINE);
    for (x = 0; x < 240; x++) {
        int32_t y = 50 - snap[trig + (uint32_t)x] * 44 / peak;   /* auto-scaled */
        if (x)
            cv_line(x - 1, py, x, y, c);
        py = y;
    }
}

/* SLOOP 2.4: a page without a graph (EDIT, VOICE, the DEST pages, GLOBAL, MASTER, SYSTEM...): its values
 * large in the empty space, 2 x 2 as the knobs (KNOB 1 2 / KNOB 3 4), the one turned in white; one value
 * alone (FILTER) across the width. The columns (draw_columns, drawn first) fill ui.big_* */
static void graph_big(void)
{
    uint32_t c, n = 0;
    for (c = 0; c < 4u; c++)
        n += ui.big_l[c][0] != 0;
    if (n > 1u) {
        cv_rect(119, 6, 1, 112, C_LINE);
        cv_rect(6, 62, 228, 1, C_LINE);
    }
    for (c = 0; c < 4u; c++) {
        int32_t x0 = c & 1u ? 126 : 6, y0 = c & 2u ? 64 : 2, x;
        if (!ui.big_l[c][0])
            continue;
        if (n == 1u) {                                  /* one value: in the middle */
            x0 = (240 - text_w(&FONT_L, ui.big_v[c]) - (ui.big_u[c][0] ? text_w(&FONT_S, ui.big_u[c]) + 4 : 0)) / 2;
            y0 = 30;
        }
        cv_text(x0, y0 + 2, &FONT_S, ui.big_l[c], TE_COL[c & 3u]);
        x = cv_text(x0, y0 + 20, &FONT_L, ui.big_v[c], ui.big_c[c] == C_DIM ? C_DIM : ui.big_c[c] == C_WHITE ? C_WHITE : C_HI);
        if (ui.big_u[c][0])
            cv_text(x + 4, y0 + 34, &FONT_S, ui.big_u[c], C_GRAY);
    }
}

/* the settings pages (GLO, SAVE > TOOLS; 2026-10-29, Ariel: they should read as a menu): the four knobs as rows, each
 * its knob's number and colour, its name, its value large; the track's character small at the side */
static int menu_page(const page_t *pg)
{
    return pg->scope == SC_GLOBAL && pg->graph == GR_NONE && (pg->fam == FAM_GLO || pg->fam == FAM_SAVE);
}
static void graph_menu(const track_t *t)
{
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        int32_t y = 2 + (int32_t)c * 30, x, lw, vw, uw;
        uint16_t vc = ui.big_c[c];                      /* (the knob's colour, as the band below; dim: does nothing now) */
        char n[2] = {(char)('1' + c), 0};
        if (!ui.big_l[c][0])
            continue;
        cv_rect(4, y + 6, 12, 18, TE_COL[c & 3u]);      /* the knob: its number on its colour */
        cv_text(6, y + 7, &FONT_S, n, C_BLACK);
        lw = cv_text(22, y + 7, &FONT_S, ui.big_l[c], TE_COL[c & 3u]);
        uw = ui.big_u[c][0] ? text_w(&FONT_S, ui.big_u[c]) + 4 : 0;
        vw = text_w(&FONT_L, ui.big_v[c]);
        if (168 - uw - vw >= lw + 8) {                  /* the value large, to the right */
            x = cv_text(168 - uw - vw, y - 2, &FONT_L, ui.big_v[c], vc);
            if (uw)
                cv_text(x + 4, y + 12, &FONT_S, ui.big_u[c], C_GRAY);
        } else {                                        /* (too long for it: small) */
            x = cv_text(168 - uw - text_w(&FONT_S, ui.big_v[c]), y + 7, &FONT_S, ui.big_v[c], vc);
            if (uw)
                cv_text(x + 4, y + 7, &FONT_S, ui.big_u[c], C_GRAY);
        }
        cv_rect(22, y + 28, 146, 1, C_LINE);
    }
    if (is_drum(t))
        op_sprite_wh(OP_SPR_DRUMMER_MX, OP_SPR_DRUMMER_MX_W, OP_SPR_DRUMMER_MX_H, 208 - OP_SPR_DRUMMER_MX_W / 2,
                     100 - OP_SPR_DRUMMER_MX_H, 0, TE_COL[3]);
    else
        op_sprite_scaled(song.sel % 3u, 210, 100, TE_COL[song.sel & 3u]);
}

static void draw_graph(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint16_t c = TE_COL[song.sel & 3u];               /* LIVE: the curves in the track's colour */
    uint32_t sig, top, drum_note = !ui.home && is_drum(t) && !page_for_drum(pg);
    sig = graph_signature();
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_oy = G_OY;
    if (ui.home) {
        graph_scope(c);
    } else if (drum_note) {                          /* a page the drum track has no use for */
        static const char *const L[2] = {"DRUM TRACK", "SEQ  TRACKS  GLO DRUMS"};
        cv_text((240 - text_w(&FONT_S, L[0])) / 2, 26, &FONT_S, L[0], C_HI);
        cv_text((240 - text_w(&FONT_S, L[1])) / 2, 52, &FONT_S, L[1], C_DIM);
    } else {
        switch (pg->graph) {
        case GR_ADSR:
            graph_adsr(t, c);
            break;
        case GR_LFO:
            graph_lfo(t, c);
            break;
        case GR_STEPS:
            graph_steps(t, c);
            break;
        case GR_ROLL:
            graph_roll(t, c);
            break;
        case GR_DGEN:
            graph_dgen();
            break;
        case GR_SCALE:
            graph_scale(t, c);
            break;
        case GR_ARP:
            cv_oy = 0;
            op_arp_scene(t);                         /* the stepping stones (ui_op.c) */
            break;
        case GR_HEX:
            cv_oy = 0;
            op_hex_scene();                          /* the drum sound, its picture (ui_op.c) */
            break;
        case GR_FX:
            cv_oy = 0;
            op_fx_scene(opk.ratio);                  /* the wizard (ui_op.c) */
            break;
        case GR_SLCR:
            graph_slicer(t, c);
            break;
        case GR_BROWSE:
            cv_oy = 0;
            graph_browse();
            break;
        case GR_SLOTS:
            cv_oy = 0;
            graph_slots();
            break;
        case GR_USER:
            cv_oy = 0;
            graph_user();
            break;
        default:
            if (big_page(pg)) {
                cv_oy = 0;
                if (menu_page(pg)) {
                    graph_menu(t);
                } else if (pg->fam == FAM_FX) {              /* FILTER, INSERT, DLY, REV/CHO: the wizard too */
                    if (!is_drum(t))
                        op_names(t);
                    op_fx_scene(opk.ratio);
                } else if (is_drum(t)) {
                    graph_big();
                } else {
                    op_synth_scene((track_t *)t, opk.ratio, pg->id[0] == P_E0 && t->eng_req % NENGINES == 0u);
                }
            }
            break;
        }
    }
    if (!ui.home && !drum_note && !is_drum(t) && !big_page(pg) && pg->graph != GR_BROWSE && pg->graph != GR_SLOTS &&
        pg->graph != GR_USER) {                      /* over a graph (they keep to the lower rows): the track's sound */
        cv_oy = 0;
        op_names(t);
    }
    top = !ui.home && !drum_note && (pg->graph == GR_BROWSE || pg->graph == GR_SLOTS || pg->graph == GR_USER ||
                                     big_page(pg));   /* these draw from the top */
    cv_oy = 0;
    if (ui.hot_t && settings.zoom && !(top && big_page(pg) && !drum_note)) {   /* (the big values show it already) */                 /* focus (menu ZOOM): the touched value, large and white */
        int32_t x;
        top = 1;
        cv_rect(0, 0, 150, 50, C_BLACK);
        cv_text(4, 0, &FONT_S, ui.focus_l, C_GRAY);
        x = cv_text(4, 16, &FONT_L, ui.focus_v, C_WHITE);
        cv_text(x + 4, 30, &FONT_S, ui.focus_u, C_DIM);
    }
    /* graphs keep out of the top G_OY rows: skip them unless something is (or was) there */
    cv_blit_from(0, Y_GRAPH, top || ui.graph_top || ui.force ? 0u : G_OY);
    ui.graph_top = (uint8_t)top;
}

/* under the graph: the pages of this family (ui_op.c) */
static void draw_foot(void)
{
    const page_t *pg = cur_page();
    uint32_t i, n = 0, k = 0;
    for (i = 0; i < NPAGES && !ui.home; i++)
        if (PAGES[i].fam == pg->fam && (page_shown(&PAGES[i]) || i == ui.page)) {   /* (the pages it has) */
            n++;
            if (i == ui.page)
                k = n;
        }
    op_pages(k, n, &ui.foot_sig);
}
static void draw_columns_set(void);
static void draw_columns(void)                         /* the four knobs of the page, then their band */
{
    const char *lab[4], *val[4], *unit[4];
    uint32_t c;
    draw_columns_set();
    for (c = 0; c < 4u; c++) {
        lab[c] = opk.lab[c];
        val[c] = opk.val[c];
        unit[c] = opk.unit[c];
    }
    op_knobs(lab, val, unit, opk.ratio, opk.dim, song.sel * 7u + ui.page * 31u + ui.home, &ui.col_sig);
    {                                                   /* the track bar under them: it follows the track selected here */
        static uint32_t bar;
        op_trackbar(&bar);
    }
}
static void draw_columns_set(void)
{
    uint32_t c;
    char val[12];
    const char *unit;
    if (ui.home) {
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d = home_param(c, &vp);
            param_format(d, *vp, val, &unit);
            draw_column(c, d->label, val, unit, VAL(c), RATIO(d, *vp), param_icon(d, *vp));
        }
        return;
    }
    if (is_drum(TSEL) && !page_for_drum(cur_page())) {   /* "DRUM TRACK" (the graph says so) */
        for (c = 0; c < 4u; c++)
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_TRK) {                 /* TRACK LEVEL LEN PAN of the selected track */
        const track_t *t = TSEL;
        uint32_t lvl = trk_level(song.sel);
        fmt_int(val, (int32_t)song.sel + 1);
        draw_column(0, "TRACK", val, "/4", VAL(0u), (int32_t)song.sel * 1000 / (NTRK - 1), ICON_AUTO);
        if (!lvl || t->p[P_MUTE]) {                    /* (MUTE: a turn of KNOB 2 unmutes, tracks_edit) */
            str_cpy(val, "MUTE", 12);
            unit = "";
        } else if (is_drum(t)) {
            fmt_int(val, (int32_t)lvl);
            unit = "";
        } else {
            param_format(&TP[P_LEVEL], (int32_t)lvl, val, &unit);
        }
        draw_column(1, "LEVEL", val, unit, lvl && !t->p[P_MUTE] ? VAL(1u) : C_DIM, (int32_t)lvl * 1000 / 127, ICON_AUTO);
        param_format(&TP[P_SLEN], t->p[P_SLEN], val, &unit);
        draw_column(2, "LEN", val, unit, VAL(2u), RATIO(&TP[P_SLEN], t->p[P_SLEN]), ICON_AUTO);
        param_format(&TP[P_PAN], t->p[P_PAN], val, &unit);
        draw_column(3, "PAN", val, unit, VAL(3u), RATIO(&TP[P_PAN], t->p[P_PAN]), param_icon(&TP[P_PAN], t->p[P_PAN]));
        return;
    }
    if (cur_page()->graph == GR_BROWSE) {
        uint32_t total, cur = preset_pos(&total);
        char u[8];
        fmt_int(val, (int32_t)cur + 1);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)total);
        draw_column(0, "No.", val, u, VAL(0u), -1, ICON_NONE);
        draw_column(1, "ENG", ENGINES[TSEL->eng_req]->name, "", VAL(1u), -1, engine_icon(ENGINES[TSEL->eng_req]->name));
        draw_column(2, "", "", "", C_HI, -1, ICON_AUTO);
        draw_column(3, "", "", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_USER) {                  /* SLOT, then three GO buttons */
        int used = up_used(ui.uslot);
        up_slot_label(val, ui.uslot);
        draw_column(0, "SLOT", val, "", VAL(0u), (int32_t)ui.uslot * 1000 / (int32_t)(UP_SLOTS - 1u), ICON_AUTO);
        draw_column(1, "LOAD", "--", "", used ? C_HI : C_DIM, -1, ICON_AUTO);
        draw_column(2, "ERASE", "--", "", used ? C_HI : C_DIM, -1, ICON_AUTO);
        draw_column(3, "SAVE", "--", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_STEP) {
        static const char *const TIME_N[3] = {"NOTE", "TIE", "REST"};
        const step_t *st = &TSEL->step[ui.cursor];
        char u[8];
        if (st->n) {
            note_name(val, st->note[0]);
            u[0] = 0;
            if (st->n > 1) {
                str_cpy(u, "+", 8);
                fmt_int(u + 1, st->n - 1);
            }
        } else {
            str_cpy(val, "--", 12);
            u[0] = 0;
        }
        {
            static const char *const FLAG_N[4] = {"-", "ACC", "SLD", "A+S"};
            char sn[8], sl[8];
            fmt_int(sn, (int32_t)ui.cursor + 1);
            str_cpy(sl, "/", 8);
            fmt_int(sl + 1, TSEL->p[P_SLEN]);
            draw_column(0, "STEP", sn, sl, VAL(0u), -1, ICON_AUTO);
            draw_column(1, "NOTE", val, u, step_on(st) ? VAL(1u) : C_DIM, -1, ICON_AUTO);
            if (step_on(st)) {                          /* a note: its LEN (2.6), steps with the TIEs after it */
                char ln[8];
                fmt_int(ln, (int32_t)note_len(TSEL, ui.cursor));
                draw_column(2, "LEN", ln, "STEP", VAL(2u), -1, ICON_AUTO);
            } else {
                draw_column(2, "TIME", TIME_N[st->time % 3u], "", VAL(2u), -1, ICON_AUTO);
            }
            draw_column(3, "FLAG", FLAG_N[(st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u)], "",
                        VAL(3u), -1, ICON_AUTO);
        }
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), c, &vp);
        if (cur_page()->graph == GR_SLOTS && c == 1u) {   /* 2.6: ERASE, lit while the slot holds a project */
            draw_column(1, "ERASE", "--", "", project_used((uint32_t)song.g[G_SLOT] - 1u) ? C_HI : C_DIM, -1, ICON_AUTO);
            continue;
        }
        if (!d || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[c] == G_INFO && cur_page()->scope == SC_GLOBAL && !usb.config) {   /* not connected: the
                                                       * USB status instead of the CPU (2.4: MIDI is MIDI OUT, ROUT is IN) */
            str_cpy(val, !usb.up ? "OFF" : usb.config ? "MIDI" : usb.setups ? "ENUM" : usb.sof_seen ? "BUS" : "WAIT", 12);
            unit = "USB";
            draw_column(c, "USB", val, unit, C_HI, -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[c] == G_INFO && cur_page()->scope == SC_GLOBAL) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, *vp, val, &unit);
        }
        draw_column(c, d->label, val, unit, VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    param_icon(d, *vp));
    }
}


/* the UI's timers, once a frame whatever the screen (a message, the BPM highlight, an armed GO, the
 * white value of the knob just turned) */
/* the knob pop-up (ui.c knob_popup): shown in the header (ui_op.c op_header, ui_layers.c layer_title) while pop.t runs */
static void pop_draw(void)
{
    if (pop.t && !--pop.t)
        ui.force = 1;                                       /* gone: the header comes back */
}
static void ui_timers(void)
{
    pop_draw();
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.arm_t && !--ui.arm_t)
        ui.arm = 0;
    if (ui.hot_t)
        ui.hot_t--;
    if (live_store_t && !--live_store_t) {        /* MAIN MENU: STORE left armed too long: disarm it */
        live_store = 0;
        ui_message("STORE OFF");
    }
}

static void vis_draw(void);                     /* ui_vis.c */
static int boot_draw(void);                     /* ui_vis.c: the boot animation */
static int vis_shown(void);
static uint8_t vis_on, vis_shown_last, vis_name_t;   /* the visualiser (ui_vis.c): on, drawn last frame, its name's time */
static void ui_draw(void)
{
    ui.frame++;
    if (boot_draw())                                    /* (KARMA 3.0: the boot animation first) */
        return;
    pads_tick();
    if (rec_go) {                                       /* the take started: say so */
        rec_go = 0;
        ui_message("RECORDING");
    }
    if (ft_bars) {                                      /* a free take closed: the loop it made */
        char m[24];
        uint32_t n = ft_bars;
        ft_bars = 0;
        if (n == 0xFFu) {
            ui_message("TAKE DROPPED");
        } else {
            str_cpy(m, n == 1u ? "LOOP 1 BAR " : n == 2u ? "LOOP 2 BARS " : "LOOP 4 BARS ", sizeof m);
            fmt_int(m + str_len(m), song.g[G_BPM]);
            str_cpy(m + str_len(m), " BPM", sizeof m - str_len(m));
            ui_message(m);
        }
    }
    if (er_flash) {                                     /* EDIT + key took something out */
        er_flash = 0;
        if (!ui.msg_t)
            ui_message("ERASED");
    }
    if (!ui.menu && (ui.layer != LY_PLAY || ui.hold_kind)) {   /* a layer held / a hold to confirm */
        if (ui.hold_kind)
            hold_screen_draw();
        else
            layer_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (layer_shown) {                                  /* back to the page */
        layer_shown = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    if ((rec_wait || ft_on) && !ui.menu && !on_song_page()) {   /* armed / free take */
        rec_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (rec_shown) {                                    /* back to the page */
        rec_shown = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
#if FELUCCA_ARRANGER
    if (!ui.menu && on_song_page()) {
        song_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
#endif
    if (ui.menu) {
        draw_menu();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (vis_shown()) {                                  /* the visualiser over the TRACKS screen */
        vis_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (vis_shown_last) {                               /* back from it */
        vis_shown_last = 0;
        vis_on = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    if (!ui.home && cur_page()->scope == SC_TRK) {
        studio_tracks_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (on_drum_page()) {
        drum_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    cursor_fix();
    if (ui.force)
        draw_frame();
    felucca_dbg.stage = 3;
    draw_head();
    felucca_dbg.stage = 4;
    draw_columns();
    felucca_dbg.stage = 5;
    draw_graph();
    ui_timers();
    felucca_dbg.stage = 6;
    draw_foot();
    ui.force = 0;
}
