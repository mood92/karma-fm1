/* SPDX-License-Identifier: GPL-3.0-only */
/* The three bands every LIVE screen shares, top to bottom: the header (a tag that says where you are,
 * the tempo, the transport), the four knob readouts (KNOB 1..4 in blue, green, white, orange: the label
 * and its value in the knob's colour) and the track bar (the four tracks, the selected one filled).
 * The screen's own picture goes between the header and the readouts, rows OP_BODY_Y .. OP_KNOB_Y - 1.
 * Each band is its own canvas with its own signature, as the rest of ui_studio.c. */
#define OP_HEAD_H 22
#define OP_BODY_Y OP_HEAD_H
#define OP_KNOB_Y 154
#define OP_KNOB_H 58
#define OP_BAR_Y 212
#define OP_BAR_H 28
#define OP_PUR RGB(142, 134, 201)        /* line art: what is neither touched nor playing */
#define OP_PUR_D RGB(58, 50, 112)        /* stands, the floor, empty steps */
static int32_t batt_shown(void);         /* ui_draw.c */
static int32_t batt_level(void);
static int32_t meter_px(int32_t a);

static void op_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a 1 px outline */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

#include "mm_logo.h"
static void op_sprite_wh(const uint8_t *d, int32_t w, int32_t h, int32_t x0, int32_t y0, int flip, uint16_t c);
/* the header: [tag] title | BPM, play / stop, the four beats, REC, the battery; the Main Menu mark beside the tempo */
static void op_header(const char *tag, uint16_t tc, const char *title, uint32_t *cache)
{
    char b[12];
    uint32_t beat = clk_beat, k, playing = song.playing;
    uint32_t sig = studio_hash((uint32_t)song.g[G_BPM] * 7u + playing * 3u + (song.rec != 0) * 1999u +
                               (playing ? beat * 131u : 0u) + (ui.bpm_t != 0) * 104729u + (uint32_t)batt_shown() * 7777u, title);
    sig = studio_hash(sig * 31u + tc, tag);
    if (pop.t)
        sig = studio_hash(sig * 3u + (uint32_t)pop.ratio + pop.k, pop.val) + 1u;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, OP_HEAD_H, C_BLACK);
    {   /* the tag in its box (four characters, or as wide as a longer one), the title up to the tempo */
        int32_t w = text_w(&FONT_S, tag) + 6;
        w = w < 38 ? 38 : w;
        op_box(2, 1, w, 18, tc);
        te_text_c(2 + w / 2, 2, tag, tc);
        if (pop.t) {                                   /* a knob touched: its name and value, in the same place every time */
            uint16_t pc = TE_COL[pop.k & 3u];
            cv_text(w + 8, 2, &FONT_S, pop.lab, pc);
            cv_text(236 - text_w(&FONT_S, pop.val), 2, &FONT_S, pop.val, C_WHITE);
            if (pop.ratio >= 0)
                cv_rect(w + 8, 18, (236 - (w + 8)) * pop.ratio / 1000 + 1, 2, pc);
            cv_rect(0, OP_HEAD_H - 1, 240, 1, TE_G1);
            cv_blit(0, 0);
            return;
        }
        str_cpy(b, title, 10);
        while (b[0] && w + 8 + text_w(&FONT_S, b) > 98)
            b[str_len(b) - 1u] = 0;
        cv_text(w + 8, 2, &FONT_S, b, TE_G3);
    }
    op_sprite_wh(MM_LOGO, MM_LOGO_W, MM_LOGO_H, 102, 0, 0, C_WHITE);
    fmt_int(b, song.g[G_BPM]);
    cv_text(150 - text_w(&FONT_S, b), 2, &FONT_S, b, ui.bpm_t ? C_WHITE : TE_G4);
    if (playing) {
        int32_t i;
        for (i = 0; i < 10; i++)
            cv_rect(156 + i, 4 + i / 2, 1, 12 - i, TE_COL[1]);
    } else {
        cv_rect(156, 5, 10, 10, TE_G3);
    }
    for (k = 0; k < 4u; k++)                           /* the four beats of the bar */
        cv_rect(172 + (int32_t)k * 8, 7, 6, 6, playing && beat % 4u == k ? (k ? C_WHITE : TE_RED) : TE_G2);
    if (song.rec)
        te_disc(209, 10, 4, TE_RED);
    {   /* battery, 3 bars (filling in turn while a USB host charges it) */
        int32_t lvl = batt_shown(), i;
        op_box(217, 5, 17, 10, TE_G3);
        cv_rect(234, 8, 2, 4, TE_G3);
        for (i = 0; i < lvl; i++)
            cv_rect(219 + i * 5, 7, 3, 6, lvl == 1 && batt_level() <= 1 ? TE_RED : TE_G4);
    }
    cv_rect(0, OP_HEAD_H - 1, 240, 1, TE_G1);
    cv_blit(0, 0);
}

/* a value of up to three digits (and a sign): drawn tall and thin */
static int op_is_num(const char *s)
{
    uint32_t n = 0;
    if (*s == '-')
        s++;
    for (; *s; s++, n++)
        if (*s < '0' || *s > '9')
            return 0;
    return n >= 1u && n <= 3u;
}

/* the knob readouts: under each knob its label, a thin gauge and the value (then its unit, small), all
 * in the knob's colour; the knob being turned: its value white, underlined; a value that does nothing
 * now (dim: a bit per knob): grey. A label left empty: an empty column. unit may be 0. A message
 * replaces the band (as te_dials) */
static void op_knobs(const char *const lab[4], const char *const val[4], const char *const *unit, const int32_t ratio[4],
                     uint32_t dim, uint32_t sig, uint32_t *cache)
{
    uint32_t k;
    sig = studio_hash(sig + dim * 977u, ui.msg_t ? ui.msg : "");
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]);
        sig = studio_hash(sig, val[k]);
        if (unit)
            sig = studio_hash(sig, unit[k]);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, OP_KNOB_H, C_BLACK);
    cv_rect(0, 2, 240, 1, TE_G1);
    if (ui.msg_t) {                                     /* a message: a white bar */
        cv_rect(0, 14, 240, 32, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 22, &FONT_S, ui.msg, C_BLACK);
        cv_blit(0, OP_KNOB_Y);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t x0 = 4 + 58 * (int32_t)k, x = x0;
        uint32_t hot = ui.hot_t && ui.hot_col == k;
        uint16_t c = hot ? C_WHITE : (dim >> k) & 1u ? TE_G2 : TE_COL[k];
        const char *v = val[k], *u = unit ? unit[k] : "";
        int32_t uw = u[0] ? text_w(&FONT_S, u) + 3 : 0;
        if (!lab[k][0])
            continue;
        cv_text(x0, 5, &FONT_S, lab[k], TE_COL[k]);
        if (ratio[k] >= 0) {
            cv_rect(x0, 22, 52, 1, TE_G1);
            cv_rect(x0, 22, ratio[k] * 52 / 1000, 1, TE_MID[k]);
        }
        if (op_is_num(v) && (int32_t)str_len(v) * 16 + uw <= 58) {   /* (with a unit that fits beside them) */
            if (*v == '-') {
                cv_rect(x, 39, 7, 2, c);
                x += 10;
                v++;
            }
            for (; *v; v++) {                           /* (a 1 is a bar: it takes less room) */
                int32_t w = *v == '1' ? 4 : 12;
                te_digit(x, 27, w, 26, 2, (uint32_t)(*v - '0'), c);
                x += w + 4;
            }
        } else if (text_w(&FONT_L, v) + uw <= 56) {
            x = cv_text(x0, 24, &FONT_L, v, c);
        } else {
            x = cv_text(x0, 32, &FONT_S, v, c);
        }
        if (uw && x + uw <= x0 + 57)
            cv_text(x + 3, 38, &FONT_S, u, TE_G3);
        if (hot)
            cv_rect(x0, 55, 52, 2, TE_COL[k]);
    }
    cv_blit(0, OP_KNOB_Y);
}

/* the track bar: 1 2 3 DRUM, the selected track filled in its colour; a muted track dim, a soloed one
 * marked white, the one that records marked red */
static void op_trackbar(uint32_t *cache)
{
    static const char *const NAME[NTRK] = {"LANE 1", "LANE 2", "LANE 3", "DRUM"};
    uint32_t i, sig = 1u + song.sel + song.rec * 16u + song.solo * 256u;
    for (i = 0; i < NTRK; i++)
        sig = sig * 3u + (trk_silent(&trk[i]) != 0);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, OP_BAR_H, C_BLACK);
    for (i = 0; i < NPART; i++) {
        int32_t x = 4 + 78 * (int32_t)i;
        uint32_t sel = song.sel == i, mute = trk_silent(&trk[i]) != 0;
        uint16_t c = mute && !sel ? TE_DIM[i] : TE_COL[i];
        if (sel)
            cv_rect(x, 3, 74, 22, c);
        else
            op_box(x, 3, 74, 22, c);
        te_text_c(x + 37, 6, NAME[i], sel ? C_BLACK : mute ? TE_G3 : c);
        if ((song.rec >> i) & 1u)
            te_disc(x + 66, 14, 3, TE_RED);
        else if ((song.solo >> i) & 1u)
            cv_rect(x + 64, 11, 6, 6, sel ? C_BLACK : C_WHITE);
    }
    cv_blit(0, OP_BAR_Y);
}

/* the characters as their scenes show them: pictures in flash (op_sprites.h, drawn by tools/gen_sprites.py),
 * 4 bits a pixel; (x0, y0): the top left, flip: mirrored, c: the track's colour */
#include "op_sprites.h"
#define OP_SKIN RGB(92, 84, 150)
static uint8_t op_dim;                                  /* the next pictures dim (a muted, a silent track) */
static void op_sprite_wh(const uint8_t *d, int32_t w, int32_t h, int32_t x0, int32_t y0, int flip, uint16_t c)
{
    const uint16_t lit[16] = {0, OP_PUR, OP_PUR_D, C_WHITE, c, TE_COL[1], TE_COL[3], TE_COL[0], C_BLACK, OP_SKIN};
    const uint16_t dim[16] = {0, OP_PUR_D, RGB(30, 26, 58), TE_G3, c, TE_DIM[1], TE_DIM[3], TE_DIM[0], C_BLACK, RGB(40, 36, 76)};
    const uint16_t *pal = op_dim ? dim : lit;
    int32_t x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x += 2) {
            uint32_t b = *d++, v = b & 15u;
            if (v)
                cv_pset(x0 + (flip ? w - 1 - x : x), y0 + y, pal[v]);
            v = b >> 4;
            if (v)
                cv_pset(x0 + (flip ? w - 2 - x : x + 1), y0 + y, pal[v]);
        }
}
static void op_sprite(const uint8_t *d, int32_t x0, int32_t y0, int flip, uint16_t c)
{
    op_sprite_wh(d, OP_SPR_W, OP_SPR_H, x0, y0, flip, c);
}

/* ---------------------------------------------------------------- DRUMS --- */
/* the GRID page: a drummer at a kit of the 16 sounds (a piece flashes white on its hit, the sound of
 * KNOB 1 is blue, the sticks go to what is hit), the step in large digits, the 16 x 16 steps of this
 * page in small at the right, then the 16 steps of KNOB 1's sound as the 16 white keys. */
#define OP_SCENE_H 96
#define OP_STEPS_H 36

/* an arc of an ellipse, a0 .. a1 in 1/1024 turns (0 = right, 256 = down), as 2 px chords */
static void op_arc(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t a0, uint32_t a1, uint16_t c)
{
    int32_t px = 0, py = 0;
    uint32_t a, first = 1;
    for (a = a0;; a += 32u) {
        uint32_t m = (a > a1 ? a1 : a) & 1023u;
        int32_t x = cx + ((SINE[(m + 256u) & 1023u] * rx + 16384) >> 15), y = cy + ((SINE[m] * ry + 16384) >> 15);
        if (!first)
            cv_line(px, py, x, y, c);
        first = 0;
        px = x;
        py = y;
        if (a >= a1)
            break;
    }
}
static void op_ell(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c) { op_arc(cx, cy, rx, ry, 0, 1024, c); }
/* a drum seen from the front: the head, the sides, the bottom edge */
static void op_shell(int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t d, uint16_t c)
{
    op_ell(cx, cy, rx, ry, c);
    cv_line(cx - rx, cy, cx - rx, cy + d, c);
    cv_line(cx + rx, cy, cx + rx, cy + d, c);
    op_arc(cx, cy + d, rx, ry, 0, 512, c);
}
static void op_stand(int32_t x, int32_t y0)             /* a stand: a pole and two feet on the floor */
{
    cv_line(x, y0, x, 84, OP_PUR_D);
    cv_line(x, 84, x - 8, 92, OP_PUR_D);
    cv_line(x, 84, x + 8, 92, OP_PUR_D);
}

/* where a stick lands for each sound (0: no stick: the kick's pedal, the hat's, the clap, the shaker) */
static const uint8_t KIT_X[DRUM_LANES] = {0, 0, 80, 0, 58, 58, 0, 88, 80, 148, 108, 66, 160, 0, 174, 140};
static const uint8_t KIT_Y[DRUM_LANES] = {0, 0, 54, 0, 38, 34, 0, 56, 54, 56, 35, 13, 25, 0, 58, 36};

static void op_drum_scene(uint32_t bank, uint32_t len)
{
    uint32_t i, j, n, lused = 0, rused = 0;
    int32_t lx = 76, ly = 22, rx = 148, ry = 22;        /* the sticks at rest: up */
    uint16_t c;
#define LIT(a) (pad_lit[a] != 0)
#define SEL(a) (drum_lane == (a))
#define KC(lit, sel) ((lit) ? C_WHITE : (sel) ? TE_COL[0] : OP_PUR)
    cv_begin(240, OP_SCENE_H, C_BLACK);
    for (i = 40; i < 188u; i += 4u)
        cv_pset((int32_t)i, 93, OP_PUR_D);
    /* the step: the one that plays (white), stopped the one of KNOB 2 (green) */
    n = (song.playing ? TDRUM->seq_idx % len : drum_cursor) + 1u;
    c = song.playing ? C_WHITE : TE_COL[1];
    te_digit(4, 4, 14, 30, 2, n / 10u, c);
    te_digit(22, 4, 14, 30, 2, n % 10u, c);
    cv_text(4, 38, &FONT_S, "STEP", TE_G3);
    /* the drummer behind it all: headphones, a cap, a mouth that opens on the kick */
    op_sprite_wh(LIT(0) || LIT(1) ? OP_SPR_DRUMMER_HIT : OP_SPR_DRUMMER, OP_SPR_DRUMMER_W, OP_SPR_DRUMMER_H, 80, 0, 0, TE_DRUM);
    /* stands first, the kit over them */
    op_stand(54, 45);
    op_stand(78, 70);
    cv_line(62, 17, 68, 30, OP_PUR_D);
    cv_line(166, 29, 160, 40, OP_PUR_D);
    cv_line(95, 86, 91, 92, OP_PUR_D);
    cv_line(129, 86, 133, 92, OP_PUR_D);
    cv_line(138, 80, 136, 92, OP_PUR_D);
    cv_line(162, 80, 164, 92, OP_PUR_D);
    op_ell(62, 14, 14, 3, KC(LIT(11), SEL(11)));                         /* crash */
    op_ell(166, 26, 15, 3, KC(LIT(12), SEL(12)));                        /* ride */
    c = KC(LIT(4) || LIT(5) || LIT(6), SEL(4) || SEL(5) || SEL(6));      /* hi-hat: OPEN lifts the top */
    op_ell(54, LIT(5) ? 35 : 39, 13, 2, c);
    op_ell(54, 43, 13, 2, c);
    c = KC(LIT(2) || LIT(8), SEL(2) || SEL(8));                          /* snare; RIM: its hoop alone */
    op_shell(78, 56, 13, 4, 10, c);
    if (LIT(7) || SEL(7))
        op_ell(78, 56, 13, 4, KC(LIT(7), 1));
    op_ell(112, 68, 21, 21, KC(LIT(0), SEL(0)));                         /* kick, KICK2: the inner ring */
    op_ell(112, 68, 14, 14, KC(LIT(1), SEL(1)));
    op_shell(112, 37, 11, 3, 7, KC(LIT(10), SEL(10)));                   /* rack tom */
    op_shell(150, 58, 13, 4, 20, KC(LIT(9), SEL(9)));                    /* floor tom */
    c = KC(LIT(15), SEL(15));                                            /* cowbell */
    cv_line(136, 35, 144, 35, c);
    cv_line(144, 35, 147, 45, c);
    cv_line(147, 45, 133, 45, c);
    cv_line(133, 45, 136, 35, c);
    c = KC(LIT(14), SEL(14));                                            /* conga */
    op_ell(176, 60, 8, 3, c);
    cv_line(168, 60, 170, 90, c);
    cv_line(184, 60, 182, 90, c);
    cv_line(170, 90, 182, 90, c);
    c = KC(LIT(13), SEL(13));                                            /* shaker: it moves when it sounds */
    op_ell(22, LIT(13) ? 70 : 74, 5, 8, c);
    if (LIT(13)) {
        cv_line(30, 62, 33, 59, c);
        cv_line(31, 70, 35, 70, c);
        cv_line(30, 78, 33, 81, c);
    }
    c = KC(LIT(3), SEL(3));                                              /* clap: two palms, sparks on the hit */
    cv_line(138, 18, 144, 8, c);
    cv_line(LIT(3) ? 140 : 143, 20, LIT(3) ? 146 : 149, 10, c);
    if (LIT(3)) {
        cv_line(150, 7, 153, 4, c);
        cv_line(151, 13, 155, 13, c);
    }
    for (i = 0; i < DRUM_LANES; i++) {                  /* a stick to each of two hits, the nearer hand first */
        if (!LIT(i) || !KIT_X[i])
            continue;
        if ((KIT_X[i] < 112u && !lused) || rused) {
            if (lused)
                continue;
            lused = 1;
            lx = KIT_X[i];
            ly = KIT_Y[i];
        } else {
            rused = 1;
            rx = KIT_X[i];
            ry = KIT_Y[i];
        }
    }
    cv_line(93, 37, (93 + lx) / 2, (37 + ly) / 2 + 4, OP_PUR);          /* an arm to the stick's end, the stick */
    cv_line(94, 37, (93 + lx) / 2 + 1, (37 + ly) / 2 + 4, OP_PUR);
    cv_line((93 + lx) / 2, (37 + ly) / 2 + 4, lx, ly, lused ? C_WHITE : TE_DRUM);
    cv_line(131, 37, (131 + rx) / 2, (37 + ry) / 2 + 4, OP_PUR);
    cv_line(130, 37, (131 + rx) / 2 - 1, (37 + ry) / 2 + 4, OP_PUR);
    cv_line((131 + rx) / 2, (37 + ry) / 2 + 4, rx, ry, rused ? C_WHITE : TE_DRUM);
    /* this page of steps in small: 16 sounds down, 16 steps across, the column that plays framed */
    for (i = 0; i < DRUM_LANES; i++)
        for (j = 0; j < 16u; j++) {
            uint32_t p = bank * 16u + j;
            if (p >= len)
                continue;
            cv_rect(190 + (int32_t)j * 3, 4 + (int32_t)i * 3, 2, 2,
                    dstep_has(&TDRUM->dstep[p], i) ? (SEL(i) ? TE_COL[0] : OP_PUR) : SEL(i) ? TE_DIM[0] : RGB(30, 26, 58));
        }
    if (song.playing && TDRUM->seq_idx % len / 16u == bank)
        op_box(189 + (int32_t)(TDRUM->seq_idx % len % 16u) * 3, 3, 4, 50, C_WHITE);
    for (i = 0; i * 16u < len && len > 16u; i++) {      /* the pages of steps (more than one), this one filled */
        if (i == bank)
            cv_rect(190 + (int32_t)i * 8, 58, 6, 6, C_WHITE);
        else
            op_box(190 + (int32_t)i * 8, 58, 6, 6, OP_PUR);
    }
#undef LIT
#undef SEL
#undef KC
    cv_blit(0, OP_BODY_Y);
}

/* the 16 steps of KNOB 1's sound: a hit filled in its level's colour (a notch per ratchet), the cursor
 * framed white, the step that plays underlined */
static void op_drum_steps(uint32_t bank, uint32_t len)
{
    uint32_t j, r;
    cv_begin(240, OP_STEPS_H, C_BLACK);
    for (j = 0; j < 16u; j++) {
        uint32_t p = bank * 16u + j;
        const dstep_t *s = &TDRUM->dstep[p < NSTEP ? p : 0];
        int32_t x = 4 + (int32_t)j * 14 + (int32_t)(j / 4u) * 2;
        if (p >= len)
            continue;
        if (dstep_has(s, drum_lane)) {
            cv_rect(x, 4, 12, 22, lvl_col(dstep_lvl(s, drum_lane)));
            for (r = 0; r < dstep_rat(s, drum_lane); r++)
                cv_rect(x + 2, 7 + (int32_t)r * 4, 8, 2, C_BLACK);
        } else {
            op_box(x, 4, 12, 22, j % 4u ? OP_PUR_D : OP_PUR);
        }
        if (p == drum_cursor)
            op_box(x - 2, 2, 16, 26, C_WHITE);
        if (song.playing && p == TDRUM->seq_idx % len)
            cv_rect(x, 31, 12, 2, C_WHITE);
    }
    cv_blit(0, OP_BODY_Y + OP_SCENE_H);
}

/* ---------------------------------------------------------------- SYNTH --- */
/* a synth track's page without a graph of its own (EDIT, VOICE, the DEST pages...): the track's
 * character in its own scene (below), moved by the sound: the track's meter, its last note, each note
 * as it starts. */
static uint32_t op_ph;                                  /* the wave scrolls while there is sound */
static int32_t op_px;                                   /* PRISM's picture is at the top right: the scenes' things there go left by this */
static int32_t op_lvl;                                  /* the meter, falling */

/* one cycle of the wave, ph in 1/1024: kind 0..4 ANALOG's SAW SQR TRI SIN PWM, 5 a sine with more
 * harmonics as morph (0..1000) rises; -32767..32767 */
static int32_t op_wave(uint32_t ph, uint32_t kind, int32_t morph)
{
    int32_t p = (int32_t)(ph & 1023u);
    switch (kind) {
    case 0: return 32767 - p * 64;
    case 1: return p < 512 ? 32767 : -32767;
    case 2: return p < 512 ? -32767 + p * 128 : 32767 - (p - 512) * 128;
    case 3: return SINE[p];
    case 4: return p < 256 ? 32767 : -32767;
    default: return (SINE[p] + SINE[(p * 2) & 1023] * morph / 2000 + SINE[(p * 3) & 1023] * morph / 3000) * 2 / 3;
    }
}

/* the characters small (the mixer: op_k 2, HOME: op_k 1), still pictures: op_k says which size, op_ink dim a muted
 * or silent track */
static int32_t op_k = 3;
static uint16_t op_ink = OP_PUR;

/* the sound of the track, at the top of the graph canvas: engine, preset */
static void op_names(const track_t *t)
{
    char b[16];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    int32_t x = cv_text(4, 2, &FONT_S, e->name, TE_G4);
    b[0] = 0;
    if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, sizeof b);
    cv_text(x + 8, 2, &FONT_S, b, TE_G3);
}

/* the voice whose envelope the ENV page follows: the one earliest in its envelope (a new note first), then
 * the loudest; 0: the track is silent */
static const voice_t *op_env_voice(const track_t *t)
{
    const voice_t *best = 0;
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &t->v[i];
        if (!v->active || !v->stage || v->stage > 3u)
            continue;
        if (!best || v->stage < best->stage || (v->stage == best->stage && v->env > best->env))
            best = v;
    }
    return best;
}
/* the envelope page: the track's character behind the line, dim, as large as the voice's envelope is high
 * (lvl 0..1000: from about half at rest to most of the picture), standing on (cx, bot) */
static void op_env_ghost(uint32_t trk_i, int32_t cx, int32_t bot, int32_t lvl)
{
    const uint16_t pal[16] = {0, OP_PUR_D, RGB(30, 26, 58), TE_G2, TE_DIM[trk_i % 3u], TE_DIM[1], TE_DIM[3], TE_DIM[0], C_BLACK,
                              RGB(44, 40, 84)};
    const uint8_t *d = trk_i % 3u == 0u ? OP_SPR_BUDDHA : trk_i % 3u == 1u ? OP_SPR_GORILLA : OP_SPR_SAUCER;
    int32_t w = trk_i % 3u == 2u ? OP_SPR_SAUCER_W : OP_SPR_W, h = trk_i % 3u == 2u ? OP_SPR_SAUCER_H : OP_SPR_H;
    int32_t k = 120 + lvl * 100 / 1000, ow = w * k >> 8, oh = h * k >> 8, x, y;   /* the size, of 256 */
    if (trk_i % 3u == 2u) {                             /* (the saucer: larger for its size, and it rises too) */
        k = 170 + lvl * 86 / 1000;
        ow = w * k >> 8;
        oh = h * k >> 8;
        bot -= 8 + lvl * 26 / 1000;
    }
    for (y = 0; y < oh; y++) {
        const uint8_t *row = d + (uint32_t)((y << 8) / k) * (uint32_t)(w / 2);
        for (x = 0; x < ow; x++) {
            uint32_t sx = (uint32_t)((x << 8) / k), v = (row[sx >> 1] >> ((sx & 1u) * 4u)) & 15u;
            if (v)
                cv_pset(cx - ow / 2 + x, bot - oh + y, pal[v]);
        }
    }
}

/* The three scenes draw into the graph canvas (240 x 124, ui_draw.c draw_graph). ratio: the four knobs,
 * 0..1000 (-1: none); each owns a part of the picture, in its colour. */

/* synth 1, the laughing Buddha (the bass): a sumo. A note: he leans to one side, the other foot in the air, and
 * stamps it down (the sides take turns); with sound he rocks. KNOB 1 the wave behind him (ANALOG's WAVE as it
 * is, else from round to sharp; its ripples the last note's pitch), KNOB 2 the rays round his head, KNOB 3 a
 * moon that rises, KNOB 4 the dust of the stamp */
static void op_scene_sumo(track_t *t, const int32_t ratio[4], int analog_wave)
{
    static uint8_t prev;
    static int32_t side = 1;
    uint32_t kind = analog_wave ? (uint32_t)clamp(t->p[P_E0], 0, 4) : 5u;
    uint32_t cyc = (uint32_t)clamp(384 + ((int32_t)op_note - 36) * 13, 256, 1536), rnd = (ui.frame / 2u) * 2654435761u + 1u;
    int32_t morph = ratio[0] < 0 ? 0 : ratio[0], amp = 3 + op_lvl * 10 / 82, x, py = 0, cx, i, n, lean;
#define WY(xx, f, ph) (104 - ((op_wave((uint32_t)(((xx) - 8) * (int32_t)(f) / 56) + (ph), kind, morph) * amp) >> 15))
    if (op_hit > prev)
        side = -side;
    prev = op_hit;
    lean = op_hit && op_hit < 5u;                       /* the foot is down on the note; then the other one goes up */
    cx = 120 + (lean ? side * 4 : op_lvl ? (SINE[(op_ph / 3u) & 1023u] * 3) >> 15 : 0);
    if (ratio[2] >= 0)                                  /* KNOB 3: the moon */
        op_ell(222 - (op_px ? 146 : 0), 70 - ratio[2] * 34 / 1000, 5, 5, TE_COL[2]);
    for (x = 8; x <= 232; x += 2) {                     /* KNOB 1: the wave; white as a note starts */
        int32_t y = WY(x, cyc, op_ph);
        if (x > 8)
            cv_line(x - 2, py, x, y, op_hit > 6u ? C_WHITE : TE_COL[0]);
        py = y;
    }
    n = ratio[1] <= 0 ? 0 : 3 + ratio[1] * 10 / 1000;   /* KNOB 2: the rays */
    for (i = 0; i < n; i++) {
        uint32_t a = 512u + 64u + (uint32_t)i * 384u / (uint32_t)(n > 1 ? n - 1 : 1);
        int32_t hx = cx + (lean ? side * 9 : 0), co = SINE[(a + 256u) & 1023u], si = SINE[a & 1023u];
        cv_line(hx + ((co * 22) >> 15), 40 + ((si * 22) >> 15), hx + ((co * 28) >> 15), 40 + ((si * 28) >> 15), TE_COL[1]);
    }
    op_ell(120, 120, 84, 3, op_hit > 4u ? C_WHITE : OP_PUR_D);   /* the ring: it rings under the stamp */
    op_sprite(lean ? OP_SPR_BUDDHA_LEAN : OP_SPR_BUDDHA, cx - 56, 20, lean && side < 0, TE_COL[0]);
    n = ratio[3] <= 0 ? 0 : 2 + ratio[3] * 7 / 1000;    /* KNOB 4: dust where the foot came down */
    for (i = 0; op_hit > 4u && i < n; i++) {
        int32_t fx = 120 + side * 40, d = (9 - (int32_t)op_hit) * 3;
        rnd = rnd * 1664525u + 1013904223u;
        x = (int32_t)((rnd >> 8) % 21u) - 10;
        cv_line(fx + x, 118 - d / 2, fx + x * 2, 116 - d - (int32_t)((rnd >> 20) % 5u), TE_COL[3]);
    }
#undef WY
}

/* synth 2, the gorilla (ELEMENTS): it plays congas. A note: a hand comes down on one (they take turns), the head
 * flashes; then the other hand goes up, ready; with sound it nods along. KNOB 1 the size of the drums, KNOB 2 the vines,
 * KNOB 3 a moon that rises, KNOB 4 the rings of sound off the drum */
static void op_scene_conga(const int32_t ratio[4])
{
    static uint8_t prev, hand, next = 1;                /* the hand that struck last, the one in the air for the next */
    int32_t big = ratio[0] < 0 ? 0 : ratio[0] * 6 / 1000, x, i, n, s, upp = op_hit < 4u;
    if (op_hit > prev) {                                /* a note: the raised hand is down at once, as the drummer's stick */
        hand = next;
        next ^= 1u;
    }
    prev = op_hit;
    for (x = 8; x < 232; x += 8)
        cv_pset(x, 122, OP_PUR_D);
    if (ratio[2] >= 0)                                  /* KNOB 3: the moon */
        op_ell(222 - (op_px ? 146 : 0), 70 - ratio[2] * 34 / 1000, 5, 5, TE_COL[2]);
    n = ratio[1] < 0 ? 0 : 1 + ratio[1] * 5 / 1000;     /* KNOB 2: vines from above, at the sides, swinging with the sound */
    for (i = 0; i < n; i++) {
        static const uint8_t VX[6] = {62, 190, 40, 212, 20, 230};
        uint32_t h = (uint32_t)(i + 1) * 2246822519u;
        int32_t len = 20 + (int32_t)((h >> 9) % 30u), sw = op_lvl ? (SINE[(op_ph / 2u + (uint32_t)i * 200u) & 1023u] * 4) >> 15 : 0, y0 = VX[i] < 56 ? 58 : 20;
        x = VX[i];
        if (op_px && x > 150)
            y0 = 46;
        cv_line(x, y0, x + sw, y0 + len, TE_COL[1]);
        cv_line(x + sw, y0 + len, x + sw - 4, y0 + len + 5, TE_COL[1]);
        cv_line(x + sw, y0 + len, x + sw + 4, y0 + len + 5, TE_COL[1]);
        cv_line(x + sw / 2, y0 + len / 2, x + sw / 2 + 4, y0 + len / 2 + 4, TE_COL[1]);
        cv_line(x + sw / 3, y0 + len / 3, x + sw / 3 - 4, y0 + len / 3 + 4, TE_COL[1]);
    }
    op_sprite(upp ? OP_SPR_GORILLA_UP : OP_SPR_GORILLA, 64, 18 - (op_lvl && (op_ph / 160u) & 1u ? 1 : 0), upp && next, TE_COL[1]);
    {   /* KNOB 1: a keyboard in front of it, wider with the knob; the hand that came down presses a key */
        int32_t hw = 56 + big * 2, kw = hw * 2 / 14, kx0 = 120 - kw * 7, top = 99, k;
        cv_rect(kx0 - 2, top, kw * 14 + 5, 122 - top, C_BLACK);
        cv_rect(kx0 - 2, top, kw * 14 + 5, 2, TE_COL[0]);
        for (s = -1; s <= 1; s += 2) {
            int32_t mine = (s > 0) == (hand != 0), pk = (120 + s * 35 - kx0) / kw;
            if (op_hit > 3u && mine)
                cv_rect(kx0 + pk * kw + 1, top + 2, kw - 1, 122 - top - 2, C_WHITE);
        }
        for (k = 0; k <= 14; k++)
            cv_line(kx0 + k * kw, top + 2, kx0 + k * kw, 122, TE_COL[0]);
        for (k = 0; k < 14; k++)
            if (k % 7 != 2 && k % 7 != 6)
                cv_rect(kx0 + k * kw + kw * 2 / 3, top + 2, kw * 2 / 3 + 1, 13, TE_G2);
        cv_line(kx0, 122, kx0 + kw * 14, 122, TE_COL[0]);
    }
    for (s = -1; s <= 1; s += 2) {
        int32_t dx = 120 + s * 35, top = 99, mine = (s > 0) == (hand != 0);
        if (op_hit && mine && ratio[3] > 0) {           /* KNOB 4: the rings */
            int32_t r = 8 + (8 - (int32_t)op_hit) * (2 + ratio[3] * 3 / 1000);
            op_arc(dx + s * 14, top - 2, r, r, s > 0 ? 704 : 512, s > 0 ? 1024 : 832, TE_COL[3]);
            if (ratio[3] > 500 && r > 14)
                op_arc(dx + s * 14, top - 2, r - 7, r - 7, s > 0 ? 704 : 512, s > 0 ? 1024 : 832, TE_COL[3]);
        }
    }
}

/* synth 3, the flying saucer (PLAITS): it hovers among the stars. Its height is the last note's pitch (it glides
 * there); with sound it rocks, the lights on its rim run round and the stars pass, faster the louder; a note: its
 * beam flashes down and the lights all light. KNOB 1 the beam's width, KNOB 2 a planet that grows, KNOB 3 more
 * stars, KNOB 4 sparks drawn up the beam */
static void op_scene_saucer(const int32_t ratio[4])
{
    static int32_t alt = 60;
    int32_t ty = clamp(84 - ((int32_t)op_note - 36) * 5 / 4, 44, 84), i, n, x0, y0, bw, live = op_lvl || op_hit;
    alt += (ty - alt) / 3;
    x0 = 60 + (op_lvl ? (SINE[(op_ph / 2u) & 1023u] * 3) >> 15 : 0);
    y0 = alt - 30 + (op_lvl ? (SINE[(op_ph / 3u + 256u) & 1023u] * 2) >> 15 : 0);
    n = 8 + (ratio[2] < 0 ? 0 : ratio[2] * 26 / 1000);  /* KNOB 3: the stars, three depths, some twinkling */
    for (i = 0; i < n; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2654435761u;
        int32_t x = (int32_t)(((h >> 4) % 240u + 240u * 64u - (op_ph >> (3u + (uint32_t)i % 3u)) % 240u) % 240u);
        int32_t y = 22 + (int32_t)((h >> 14) % 100u), big = i % 5 == 0 && ((ui.frame / 8u + (uint32_t)i) & 3u);
        cv_rect(x, y, 1 + big, 1, TE_COL[2]);
        if (big && i % 10 == 0) {
            cv_pset(x, y - 1, TE_COL[2]);
            cv_pset(x, y + 1, TE_COL[2]);
        }
    }
    if (ratio[1] >= 0) {                                /* KNOB 2: the planet and its ring */
        int32_t r = 5 + ratio[1] * 10 / 1000, px = 206, py = 96;
        op_ell(px, py, r, r, TE_COL[1]);
        op_arc(px, py, r + 7, 3, 0, 512, TE_COL[1]);
        cv_pset(px - r - 7, py, TE_COL[1]);
        cv_pset(px + r + 7, py, TE_COL[1]);
        op_arc(px - 2, py - 2, r * 2 / 3, r * 2 / 3, 576, 768, TE_COL[1]);
    }
    bw = 6 + (ratio[0] < 0 ? 0 : ratio[0] * 20 / 1000); /* KNOB 1: the beam: wide open on a note, a glimmer with sound */
    if (op_hit || op_lvl) {
        uint16_t c = op_hit > 5u ? C_WHITE : op_hit ? TE_COL[0] : TE_DIM[0];
        int32_t bot = op_hit ? 122 : y0 + 60;
        if (bot > 122)
            bot = 122;
        cv_line(x0 + 48 - 12, y0 + 44, x0 + 48 - 12 - bw, bot, c);
        cv_line(x0 + 48 + 12, y0 + 44, x0 + 48 + 12 + bw, bot, c);
        if (op_hit)
            op_ell(x0 + 48, 121, 12 + bw, 2, c);
    }
    n = ratio[3] <= 0 || !live ? 0 : 2 + ratio[3] * 8 / 1000;   /* KNOB 4: the sparks, rising */
    for (i = 0; i < n; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2246822519u;
        int32_t up = (int32_t)(((h >> 7) + op_ph / 12u) % 64u);
        cv_rect(x0 + 48 - 10 + (int32_t)((h >> 3) % 21u), 120 - up * (120 - y0 - 44) / 64, 2, 2, TE_COL[3]);
    }
    op_sprite_wh(OP_SPR_SAUCER, OP_SPR_SAUCER_W, OP_SPR_SAUCER_H, x0, y0, 0, TE_COL[2]);
    for (i = 0; i < 7; i++) {                           /* the rim: one light runs round; all of them on a note */
        int on = op_hit > 4u || (live && (uint32_t)i == (op_ph / 160u) % 7u);
        te_disc(x0 + OP_SAUCER_LIGHTS[i][0], y0 + OP_SAUCER_LIGHTS[i][1], 2, on ? TE_COL[3] : TE_DIM[3]);
    }
}

/* PRISM: the model's own picture, 80 x 40 at the top right, from the three controls as they stand (not from the
 * sound: it shows what the knobs set). FOLD and WAVE: a cycle of the wave, made by the model itself; HARM: its 24
 * harmonics; SWARM: the eight oscillators, as far apart as HARM spreads them, as long as MRPH makes their grains;
 * CHORD: its notes on an octave of keys (green: CHORD's knob), the inversion under it (white: INVRT's). Kept until
 * a control moves. */
static void op_prism_panel(const track_t *t)
{
    static int8_t pts[80];
    static uint32_t key = 0xFFFFFFFFu;
    static const uint16_t TONES[11] = {0x001, 0x081, 0x0A1, 0x089, 0x489, 0x40D, 0x429, 0x215, 0x815, 0x891, 0x091};
    static const uint8_t WHITE[7] = {0, 2, 4, 5, 7, 9, 11}, BLACK[5] = {1, 3, 6, 8, 10}, BX[5] = {7, 17, 37, 47, 57};
    uint32_t model = prism_model(t), k = (uint32_t)t->p[P_E0] | (uint32_t)t->p[P_E1] << 4 | (uint32_t)t->p[P_E2] << 12 |
                                         (uint32_t)t->p[P_E3] << 20, i;
    int32_t h = t->p[P_E1] * 258, tb = t->p[P_E2] * 258, mo = t->p[P_E3] * 258, x0 = 152, y0 = 22, x;
    if (model == PRM_FOLD || model == PRM_WAVE || model == PRM_HARM) {
        if (k != key) {                                 /* a control moved: the picture again */
            int16_t b[32];
            key = k;
            if (model == PRM_HARM) {
                pr_harm_t hs;
                int32_t top = 1;
                pr_harm_init(&hs);
                for (i = 0; i < 60u; i++)               /* (her spectrum takes some blocks to settle) */
                    pr_harm_render(&hs, 0x01000000u, h, tb, mo, b, 1);
                for (i = 0; i < PR_HARM_N; i++)
                    top = hs.amp[i] > top ? hs.amp[i] : top;
                for (i = 0; i < PR_HARM_N; i++) {       /* (the square root of each: the quiet ones show too) */
                    uint32_t r = (uint32_t)hs.amp[i] * 1296u / (uint32_t)top, q = 0;
                    while ((q + 1u) * (q + 1u) <= r)
                        q++;
                    pts[i] = (int8_t)q;
                }
            } else {                                    /* two cycles of 160 samples, the second one kept */
                pr_fold_t fs;
                pr_wave_t ws;
                uint32_t blk;
                pr_fold_init(&fs);
                pr_wave_init(&ws);
                for (blk = 0; blk < 10u; blk++) {
                    if (model == PRM_FOLD)
                        pr_fold_render(&fs, 0x0199999Au, h, tb, mo, b, 32);
                    else
                        pr_wave_render(&ws, 0x0199999Au, h, tb, mo, b, 32);
                    for (i = 0; blk >= 5u && i < 32u; i += 2u)
                        pts[(blk - 5u) * 16u + i / 2u] = (int8_t)(b[i] * 18 / 32768);
                }
            }
        }
        if (model == PRM_HARM) {
            for (i = 0; i < PR_HARM_N; i++) {
                cv_rect(x0 + 4 + (int32_t)i * 3, y0 + 38 - pts[i], 2, pts[i] + 1, TE_COL[0]);
            }
        } else {
            for (x = 0; x < 80; x += 4)
                cv_pset(x0 + x, y0 + 20, OP_PUR_D);
            for (i = 1; i < 80u; i++)
                cv_line(x0 + (int32_t)i - 1, y0 + 20 - pts[i - 1], x0 + (int32_t)i, y0 + 20 - pts[i], TE_COL[0]);
        }
    } else if (model == PRM_SWARM) {
        int32_t spread = h, len = 8 + mo * 30 / 32767, n = 2 + tb * 10 / 32767;   /* (HARM as it is: its cube is the pitch) */
        for (i = 0; i < 8u; i++) {                      /* (their ranks: -1 .. 1.33, as the model's) */
            int32_t y = y0 + 22 - (((int32_t)i - 3) * spread * 13 / 3) / 32767;
            cv_line(x0 + 40 - len, y + 2, x0 + 40 + len, y - 2, TE_COL[0]);
            cv_line(x0 + 40 + len, y - 2, x0 + 40 + len, y + 2, TE_COL[0]);
        }
        for (i = 0; i < (uint32_t)n; i++)               /* TIMB: the grains' rate */
            cv_rect(x0 + 2 + (int32_t)i * 6, y0, 3, 2, TE_COL[2]);
    } else {                                            /* CHORD */
        uint32_t tones = TONES[(uint32_t)t->p[P_E1] * 11u / 128u];
        for (i = 0; i < 7u; i++) {
            int32_t on = (tones >> WHITE[i]) & 1u;
            if (on)
                cv_rect(x0 + 5 + (int32_t)i * 10, y0 + 2, 9, 30, WHITE[i] ? TE_COL[1] : C_WHITE);
            else
                op_box(x0 + 5 + (int32_t)i * 10, y0 + 2, 9, 30, OP_PUR);
        }
        for (i = 0; i < 5u; i++) {
            int32_t on = (tones >> BLACK[i]) & 1u;
            cv_rect(x0 + 5 + BX[i], y0 + 2, 5, 17, on ? TE_COL[1] : C_BLACK);
            if (!on)
                op_box(x0 + 5 + BX[i], y0 + 2, 5, 17, OP_PUR);
        }
        for (i = 0; i < 20u; i++)                       /* INVRT: twenty steps up */
            cv_rect(x0 + 1 + (int32_t)i * 4, y0 + 36, 3, 2, (int32_t)i <= t->p[P_E2] * 20 / 128 ? TE_COL[2] : OP_PUR_D);
    }
}

static void op_synth_scene(track_t *t, const int32_t ratio[4], int analog_wave)
{
    int32_t m = meter_px(t->peak);
    char b[8];
    op_px = t->eng_req % NENGINES == ENGI_PRISM;
    t->peak = 0;
    op_lvl = m > op_lvl ? m : op_lvl > 4 ? op_lvl - 4 : 0;
    if (op_lvl || op_hit)
        op_ph += 40u;
    op_names(t);
    note_name(b, op_note);                              /* the last note, large */
    cv_text(4, 22, &FONT_L, b, op_hit > 5u ? C_WHITE : TE_G4);
    switch ((uint32_t)(t - trk) % 3u) {
    case 0: op_scene_sumo(t, ratio, analog_wave); break;
    case 1: op_scene_conga(ratio); break;
    default: op_scene_saucer(ratio); break;
    }
    if (op_px)
        op_prism_panel(t);
}

/* the pages of the family, the one shown filled (one page: nothing) */
static void op_pages(uint32_t k, uint32_t n, uint32_t *cache)
{
    uint32_t i, sig = 1u + k + n * 16u;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 8, C_BLACK);
    for (i = 0; i < n && n > 1u; i++) {
        if (i + 1u == k)
            cv_rect(4 + (int32_t)i * 9, 1, 6, 6, C_WHITE);
        else
            op_box(4 + (int32_t)i * 9, 1, 6, 6, OP_PUR);
    }
    cv_blit(0, 146);
}

/* the four tracks' meters, for the screens that show the whole band (the mixer, HOME), once a frame: up at
 * once, falling; a rise: the track's character hops for 6 frames */
static uint8_t op_mx[NTRK], op_hop[NTRK];
static void op_band_tick(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        int32_t pk = i == TRK_DRUM ? drums.peak : trk[i].peak, m;
        if (i == TRK_DRUM)
            drums.peak = 0;
        else
            trk[i].peak = 0;
        m = trk_silent(&trk[i]) ? 0 : meter_px(pk);
        if (m > op_mx[i] + 8)
            op_hop[i] = 6;
        else if (op_hop[i])
            op_hop[i]--;
        op_mx[i] = (uint8_t)(m > op_mx[i] ? m : op_mx[i] > 3 ? op_mx[i] - 3 : 0);
    }
}
static void op_band_member(uint32_t i, int32_t cx, int32_t fy, int up, int hl, int hr, uint16_t c)
{
    static const uint8_t *const SPR[2][4] = {{OP_SPR_BUDDHA_MX, OP_SPR_GORILLA_MX, OP_SPR_SAUCER_MX, OP_SPR_DRUMMER_MX},
                                             {OP_SPR_BUDDHA_HM, OP_SPR_GORILLA_HM, OP_SPR_SAUCER_HM, OP_SPR_DRUMMER_HM}};
    static const uint8_t WH[2][4][2] = {
        {{OP_SPR_BUDDHA_MX_W, OP_SPR_BUDDHA_MX_H}, {OP_SPR_GORILLA_MX_W, OP_SPR_GORILLA_MX_H}, {OP_SPR_SAUCER_MX_W, OP_SPR_SAUCER_MX_H},
         {OP_SPR_DRUMMER_MX_W, OP_SPR_DRUMMER_MX_H}},
        {{OP_SPR_BUDDHA_HM_W, OP_SPR_BUDDHA_HM_H}, {OP_SPR_GORILLA_HM_W, OP_SPR_GORILLA_HM_H}, {OP_SPR_SAUCER_HM_W, OP_SPR_SAUCER_HM_H},
         {OP_SPR_DRUMMER_HM_W, OP_SPR_DRUMMER_HM_H}}};
    uint32_t sm = op_k == 1, k = i & 3u;
    int32_t w = WH[sm][k][0], h = WH[sm][k][1], x0 = cx - w / 2, y0 = fy - h + 1;
    (void)up;
    op_dim = op_ink == OP_PUR_D;
    if (k == 2u)                                        /* the saucer hovers */
        y0 -= sm ? 2 : 5;
    op_sprite_wh(SPR[sm][k], w, h, x0, y0, 0, c);
    op_dim = 0;
    if (k == 3u && !sm) {                               /* the drummer's sticks: hl / hr: that one comes down */
        cv_line(x0 + 7, y0 + 21, x0 - 2, hl ? y0 + 27 : y0 + 9, TE_COL[3]);
        cv_line(x0 + 28, y0 + 21, x0 + 37, hr ? y0 + 27 : y0 + 9, TE_COL[3]);
    }
}

/* ---------------------------------------------------------------- ARP --- */
/* the track's character of the scenes, a little over half size, standing on (cx, fy) (nearest pixels) */
static void op_sprite_scaled(uint32_t ti, int32_t cx, int32_t fy, uint16_t c)
{
    const uint16_t pal[16] = {0, OP_PUR, OP_PUR_D, C_WHITE, c, TE_COL[1], TE_COL[3], TE_COL[0], C_BLACK, OP_SKIN};
    const uint8_t *d = ti == 0u ? OP_SPR_BUDDHA : ti == 1u ? OP_SPR_GORILLA : OP_SPR_SAUCER;
    int32_t w = ti == 2u ? OP_SPR_SAUCER_W : OP_SPR_W, h = ti == 2u ? OP_SPR_SAUCER_H : OP_SPR_H;
    int32_t k = ti == 2u ? 184 : 142, ow = w * k >> 8, oh = h * k >> 8, x, y;   /* of 256 */
    if (ti == 2u)
        fy -= 4;                                    /* (the saucer hovers) */
    for (y = 0; y < oh; y++) {
        const uint8_t *row = d + (uint32_t)((y << 8) / k) * (uint32_t)(w / 2);
        for (x = 0; x < ow; x++) {
            uint32_t sx = (uint32_t)((x << 8) / k), v = (row[sx >> 1] >> ((sx & 1u) * 4u)) & 15u;
            if (v)
                cv_pset(cx - ow / 2 + x, fy - oh + 1 + y, pal[v]);
        }
    }
}

/* the ARP page: the arp as stepping stones, one a note in the order it plays them (the keys held, else C E G to
 * show the shape), high for a high note; the track's character hops to the stone of each note it plays. KNOB 1
 * (MODE, blue) the stones' order: up, down, up and down, shuffled, as played; KNOB 2 (RATE, green) the grid under
 * them; KNOB 3 (OCT, white) the octaves, a dashed line between them; KNOB 4 (GATE, orange) how much of each stone
 * is lit */
static uint8_t op_arp_hop;                             /* frames of the hop still to go (graph_signature) */
static void op_arp_scene(const track_t *t)
{
    static uint8_t pos, prev, last;
    static const uint8_t TICKS[6] = {4, 8, 16, 32, 6, 12};
    uint8_t base[16], seq[32];
    uint32_t mode = (uint32_t)clamp(t->p[P_AMODE], 0, 5), oct = (uint32_t)clamp(t->p[P_AOCT], 1, 4), nb = 0, n = 0, i, j, k;
    int32_t gate = clamp(t->p[P_AGATE], 1, 127), lo = 127, hi = 0, sp, x0, cx, fy, live = t->nheld && mode;
    uint32_t ti = (uint32_t)(t - trk) & 3u;
    for (i = 0; i < t->nheld && nb < 16u; i++)
        base[nb++] = t->held[i];
    if (!nb) {                                      /* nothing held: the shape on a C major chord */
        base[0] = 60, base[1] = 64, base[2] = 67;
        nb = 3;
    }
    if (mode != 5u)                                 /* (ORD: as pressed; the others from the lowest) */
        for (i = 1; i < nb; i++)
            for (j = i; j > 0 && base[j - 1] > base[j]; j--) {
                uint8_t s = base[j];
                base[j] = base[j - 1];
                base[j - 1] = s;
            }
    for (k = 0; k < oct; k++)
        for (i = 0; i < nb && n < 24u; i++)
            seq[n++] = (uint8_t)clamp(base[i] + 12 * (int32_t)k, 0, 127);
    if (mode == 2u)                                 /* DN */
        for (i = 0; i < n / 2u; i++) {
            uint8_t s = seq[i];
            seq[i] = seq[n - 1u - i];
            seq[n - 1u - i] = s;
        }
    else if (mode == 3u && n > 2u)                  /* UPDN: up, then down without the ends twice */
        for (i = n - 2u; i > 0 && n < 32u; i--)
            seq[n++] = seq[i];
    else if (mode == 4u) {                          /* RND: a shuffle (a new one each round would flicker: one) */
        uint32_t r = 0x9E3779B9u * (n + 7u);
        for (i = n - 1u; i > 0; i--) {
            uint8_t s;
            r = r * 1664525u + 1013904223u;
            j = (r >> 8) % (i + 1u);
            s = seq[i];
            seq[i] = seq[j];
            seq[j] = s;
        }
    }
    if (n > 16u)
        n = 16u;                                    /* (as many stones as fit) */
    for (i = 0; i < n; i++) {
        lo = seq[i] < lo ? seq[i] : lo;
        hi = seq[i] > hi ? seq[i] : hi;
    }
    if (hi - lo < 12)
        hi = lo + 12;
#define AY(nt) (112 - ((int32_t)(nt) - lo) * 34 / (hi - lo))
    sp = 216 / (int32_t)n;
    x0 = 12 + (216 - sp * (int32_t)n) / 2;
    for (k = 1; k * 12u <= (uint32_t)(hi - lo); k++)      /* KNOB 3: the octaves */
        for (i = 8; i < 232u; i += 6u)
            cv_pset((int32_t)i, AY(lo + 12 * (int32_t)k) + 3, TE_COL[2]);
    for (i = 0; i < TICKS[(uint32_t)t->p[P_ARATE] % 6u]; i++)   /* KNOB 2: the grid */
        cv_rect(12 + (int32_t)i * 216 / TICKS[(uint32_t)t->p[P_ARATE] % 6u], 120, 2, 2, TE_COL[1]);
    if (live && t->arp_note && t->arp_note != last) {   /* a new note: to its stone (the next one with it) */
        for (k = 1; k <= n; k++)
            if (seq[(pos + k) % n] == t->arp_note)
                break;
        if (k <= n) {
            prev = pos;
            pos = (uint8_t)((pos + k) % n);
            op_arp_hop = 6;
        }
    }
    last = live ? t->arp_note : 0;
    if (!live)
        pos = prev = 0;
    if (pos >= n)
        pos = prev = 0;
    for (i = 0; i < n; i++) {                       /* KNOB 1: the stones; KNOB 4: how much of each is lit */
        int32_t x = x0 + (int32_t)i * sp + 1, y = AY(seq[i]), w = sp - 3, lit = w * gate / 127;
        uint16_t c = !mode ? OP_PUR_D : TE_COL[0];
        if (i == pos && live && op_arp_hop > 3u)
            cv_rect(x, y, w, 4, C_WHITE);
        else {
            op_box(x, y, w, 4, c);
            if (mode)
                cv_rect(x + 1, y + 1, lit > 2 ? lit - 2 : 1, 2, i == pos && live ? C_WHITE : TE_COL[3]);
        }
    }
    {                                               /* the character: on its stone, or on the way there */
        int32_t xa = x0 + (int32_t)prev * sp + sp / 2, ya = AY(seq[prev]) - 1;
        int32_t xb = x0 + (int32_t)pos * sp + sp / 2, yb = AY(seq[pos]) - 1, u = 6 - (int32_t)op_arp_hop;
        cx = op_arp_hop ? xa + (xb - xa) * u / 6 : xb;
        fy = op_arp_hop ? ya + (yb - ya) * u / 6 - (SINE[u * 512 / 6] * 12 >> 15) : yb;
        op_sprite_scaled(ti, cx, fy, TE_COL[ti]);
    }
    if (op_arp_hop)
        op_arp_hop--;
    if (!mode)
        cv_text(170, 24, &FONT_S, "ARP OFF", TE_G3);
    else if (!t->nheld)
        cv_text(150, 24, &FONT_S, "HOLD KEYS", TE_G3);
#undef AY
}

/* ---------------------------------------------------------------- HEX --- */
/* the drum track's EDIT pages: the sound of the selected lane, played once into a picture (its first 0.75 s, the
 * louder half of each column), redrawn when it changes; the drummer above, the sound's name and its kit */
static void op_hex_scene(void)
{
    static int8_t col[200][2];
    static uint32_t made = 0xFFFFFFFFu;
    uint32_t kit = drum_kit(), i, k;
    const hex_snd_t *kits;
    if (kit >= DRUM_SAMPLED && kit < DRUM_SYN) {
        cv_text(28, 50, &FONT_S, "A HEX KIT ONLY:", TE_G3);
        cv_text(28, 68, &FONT_S, "PRESETS: KIT 1-5", TE_G3);
        return;
    }
    kits = kit < DRUM_SAMPLED ? HEX_KITS[kit].s : dsu_kit(kit - DRUM_SYN)->s;
    if (made != hex_gen * 64u + kit * 16u + drum_lane) {
        static hex_v_t v;
        static int32_t buf[166];
        hex_on(&v, kits, LANE_NOTE[drum_lane % DRUM_LANES], 110);
        for (i = 0; i < 200u; i++) {
            int32_t lo = 0, hi = 0;
            hex_render(&v, buf, 166);
            for (k = 0; k < 166u; k++) {
                lo = buf[k] < lo ? buf[k] : lo;
                hi = buf[k] > hi ? buf[k] : hi;
            }
            col[i][0] = (int8_t)clamp(lo / 1300, -32, 0);
            col[i][1] = (int8_t)clamp(hi / 1300, 0, 32);
        }
        made = hex_gen * 64u + kit * 16u + drum_lane;
    }
    for (i = 0; i < 200u; i += 4u)
        cv_pset(20 + (int32_t)i, 70, OP_PUR_D);
    for (i = 0; i < 200u; i++)
        if (col[i][1] - col[i][0] > 0)
            cv_line(20 + (int32_t)i, 70 - col[i][1], 20 + (int32_t)i, 70 - col[i][0], i < 6u ? C_WHITE : TE_DRUM);
    cv_text(4, 2, &FONT_S, LANE_NAME[drum_lane % DRUM_LANES], C_WHITE);
    cv_text(4 + text_w(&FONT_S, LANE_NAME[drum_lane % DRUM_LANES]) + 8, 2, &FONT_S, DRUM_KIT_NAMES[kit], TE_G3);
    cv_text(4, 106, &FONT_S, "KEY: PICK SOUND", TE_G2);
}

/* ------------------------------------------------------------------- FX --- */
/* the FX pages (the sends, and those without a graph: FILTER, INSERT, DLY, REV/CHO): a wizard, a spell a
 * knob from the tip of its wand, each in the knob's colour and as strong as the knob is turned: KNOB 1 a
 * bolt of lightning, KNOB 2 two ribbons that wave, KNOB 3 stars that bounce away, smaller each, KNOB 4
 * rings that spread. The selected track's sound moves them, each note flicks the wand. */
static void op_fx_scene(const int32_t ratio[4])
{
    static uint32_t ph;
    uint32_t sel = song.sel & 3u, live, rnd, j;
    int32_t flick, tx = 114, ty, x, py = 0, i, n;
    op_band_tick();
    live = op_mx[sel] != 0u || op_hop[sel] != 0u;
    if (live)
        ph += 48u;
    flick = op_hop[sel] > 3u ? 5 : op_hop[sel] ? 2 : 0;
    ty = 62 - flick;
    for (x = 8; x < 232; x += 8)
        cv_pset(x, 122, OP_PUR_D);
    /* the wizard (its star in the track's colour): it hops as the track sounds, the wand's tip is (tx, ty) */
    op_sprite(OP_SPR_WIZARD, 4, 18 - flick, 0, TE_COL[sel]);
    if (ratio[3] > 0) {                                 /* KNOB 4: the rings */
        int32_t maxr = 16 + ratio[3] * 30 / 1000;
        n = 1 + ratio[3] * 3 / 1000;
        for (i = 0; i < n; i++) {
            int32_t r = 6 + (int32_t)((ph / 6u + (uint32_t)i * (uint32_t)maxr / (uint32_t)n) % (uint32_t)maxr);
            op_arc(tx, ty, r, r, 800, 1248, r > maxr - 6 ? TE_DIM[3] : TE_COL[3]);
        }
    }
    if (ratio[0] > 0) {                                 /* KNOB 1: the bolt, new at each frame while it sounds */
        int32_t len = 50 + ratio[0] * 64 / 1000, jag = 2 + ratio[0] * 9 / 1000, px = tx + 2;
        rnd = (live ? ui.frame / 2u : 7u) * 2654435761u + 1u;
        py = ty - 2;
        for (i = 1; i <= 6; i++) {
            int32_t nx = tx + 2 + len * i / 6, ny;
            rnd = rnd * 1664525u + 1013904223u;
            ny = ty - 2 - (len * i / 6) * 26 / 114 + (i == 6 ? 0 : (int32_t)((rnd >> 12) % (uint32_t)(2 * jag + 1)) - jag);
            cv_line(px, py, nx, ny, i > 4 && live ? C_WHITE : TE_COL[0]);
            px = nx;
            py = ny;
        }
    }
    if (ratio[1] > 0) {                                 /* KNOB 2: the ribbons */
        int32_t amp = 2 + ratio[1] * 7 / 1000;
        for (j = 0; j < 2u; j++)
            for (x = tx + 6; x <= 232; x += (int32_t)j + 1) {
                int32_t y = ty + 12 + (int32_t)j * 7 + ((SINE[((uint32_t)x * 22u + ph * 2u + j * 300u) & 1023u] * amp) >> 15);
                if (j == 0 && x > tx + 6)
                    cv_line(x - 1, py, x, y, TE_COL[1]);
                else
                    cv_pset(x, y, TE_COL[1]);
                py = y;
            }
    }
    if (ratio[2] > 0) {                                 /* KNOB 3: the stars */
        n = 1 + ratio[2] * 5 / 1000;
        for (i = 0; i < n; i++) {
            int32_t sx = tx + 14 + i * 19, s = 5 - i * 4 / 5, b = SINE[(ph * 3u + (uint32_t)i * 170u) & 1023u];
            int32_t sy = ty + 44 - (((b < 0 ? -b : b) * (9 - i)) >> 15);
            cv_rect(sx - s, sy, 2 * s + 1, 1, TE_COL[2]);
            cv_rect(sx, sy - s, 1, 2 * s + 1, TE_COL[2]);
            if (s > 2) {
                cv_pset(sx - 2, sy - 2, TE_COL[2]);
                cv_pset(sx + 2, sy - 2, TE_COL[2]);
                cv_pset(sx - 2, sy + 2, TE_COL[2]);
                cv_pset(sx + 2, sy + 2, TE_COL[2]);
            }
        }
    }
}

/* ---------------------------------------------------------------- MIXER --- */
/* GLO held: the band on stage, a track a column (as the knobs and the track bar under it). A podium as
 * high as the track's level, its meter inside; its character on top, hopping as the track sounds. Muted:
 * dashed and dim, a red M; soloed: a white S. Under the stage the 16 white keys and what they do here:
 * M mute 1..4, S solo 1..4, F a fill while held, B fill the next bar, T tap the tempo (lit on the beat).
 * flags: 1 a fill now, 2 the next bar is one, 4 it is asked for, 8 the beat. */
static void op_mix_draw(const char *const val[4], const int32_t ratio[4], uint32_t flags)
{
    static const char *const LAB[4] = {"SYN1", "SYN2", "SYN3", "DRUM"};
    static const char KEYS[17] = "MMMMSSSSFB     T";
    static uint32_t head, scene, keys, knobs, bar;
    static uint8_t side;
    const uint8_t *mx = op_mx, *hop = op_hop;
    uint32_t i, sig = 7u, anylit = 0;
    op_header("MIX", C_WHITE, ui.msg_t ? ui.msg : "", &head);   /* (a message: in the title, the levels stay) */
    for (i = 0; i < DRUM_LANES; i++)
        anylit |= pad_lit[i] == 6u;
    if (anylit)
        side ^= 1u;
    op_band_tick();
    for (i = 0; i < NTRK; i++)
        sig = sig * 131u + mx[i] + hop[i] * 97u + (uint32_t)ratio[i] * 7u + (trk[i].p[P_MUTE] != 0) * 3u;
    sig = sig * 31u + song.solo + anylit * 64u + side * 128u;
    if (ui.force || sig != scene) {
        scene = sig;
        cv_begin(240, 96, C_BLACK);
        cv_rect(0, 94, 240, 1, TE_G1);
        op_k = 2;
        for (i = 0; i < NPART; i++) {
            int32_t cx = 4 + 78 * (int32_t)i + 37, h = 6 + ratio[i] * 46 / 1000, top = 94 - h, y, n = (h - 3) / 4, lit;
            int32_t air = hop[i] ? (SINE[(6 - hop[i]) * 85] * 5) >> 15 : 0;
            uint32_t mute = trk[i].p[P_MUTE] != 0;
            uint16_t c = mute ? TE_DIM[i] : TE_COL[i];
            if (mute) {                                 /* a muted podium: dashed */
                for (y = top; y < 94; y += 4) {
                    cv_rect(cx - 17, y, 1, 2, c);
                    cv_rect(cx + 16, y, 1, 2, c);
                }
                for (y = -17; y < 17; y += 4)
                    cv_rect(cx + y, top, 2, 1, c);
            } else {
                op_box(cx - 17, top, 34, h + 1, c);
            }
            lit = n * mx[i] / 82;
            for (y = 0; y < n; y++)
                cv_rect(cx - 13, 91 - y * 4, 26, y < lit ? 2 : 1, y < lit ? TE_COL[i] : TE_G1);
            op_ink = mute ? OP_PUR_D : OP_PUR;
            op_band_member(i, cx, top - 1 - (i == TRK_DRUM ? 0 : air), air > 2, anylit && !side, anylit && side, c);
            if (mute || (song.solo >> i) & 1u) {
                cv_rect(cx + 14, 2, 12, 14, mute ? TE_RED : C_WHITE);
                cv_text(cx + 16, 1, &FONT_S, mute ? "M" : "S", C_BLACK);
            }
        }
        op_ink = OP_PUR;
        op_k = 3;
        cv_blit(0, OP_BODY_Y);
    }
    sig = flags + song.solo * 16u;
    for (i = 0; i < NTRK; i++)
        sig = sig * 2u + (trk[i].p[P_MUTE] != 0);
    if (ui.force || sig != keys) {                      /* the 16 white keys */
        keys = sig;
        cv_begin(240, 36, C_BLACK);
        for (i = 0; i < 16u; i++) {
            int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2;
            uint32_t on = i < 4u ? trk[i].p[P_MUTE] != 0 : i < 8u ? (song.solo >> (i - 4u)) & 1u : i == 8u ? flags & 1u :
                          i == 9u ? (flags >> 1) & 1u : i == 15u ? (flags >> 3) & 1u : 0u;
            uint16_t c = i < 4u ? TE_COL[i] : i < 8u ? TE_MID[i - 4u] : KEYS[i] == ' ' ? OP_PUR_D : TE_G4;
            char ch[2] = {KEYS[i], 0};
            if (on)
                cv_rect(x, 4, 12, 22, i < 4u ? TE_RED : C_WHITE);
            else
                op_box(x, 4, 12, 22, c);
            if (i == 9u && (flags & 4u))
                cv_rect(x, 4, 12, 2, C_WHITE);
            cv_text(x + 2, 7, &FONT_S, ch, on ? C_BLACK : c);
        }
        cv_blit(0, OP_BODY_Y + 96);
    }
    {
        uint8_t m = ui.msg_t;
        ui.msg_t = 0;
        op_knobs(LAB, val, 0, ratio, 0, 4242u, &knobs);
        ui.msg_t = m;
    }
    op_trackbar(&bar);
}

/* ----------------------------------------------------------------- HOME --- */
/* TRACKS, between the header and the knobs. A tape deck: the bar in large digits, two reels that turn
 * while it plays (the tape goes from the left one to the right one over the selected track's loop), the
 * song's sections A..D (the one playing filled). Then the selected track's sound and state, and a lane a
 * track: its character in small (it hops as the track sounds), the 16 steps in view (a synth: each note
 * at its height; the drums: a bar, taller with more sounds), the step that plays, its level. */
static void op_home_body(void)
{
    static uint32_t top_sig, name_sig, lane_sig[NTRK];
    track_t *ts = TSEL;
    uint32_t i, j, playing = song.playing, slen = trk_len(ts), spos = ts->seq_idx % slen, sig;
    uint32_t ready = arrangement_ready();
    op_band_tick();
    sig = playing * 3u + (playing ? ui.frame / 2u * 7919u + spos * 31u : 0u) + ready * 5u + (uint32_t)(live_sec + 1) * 77u + slen * 13u;
    if (ui.force || sig != top_sig) {                   /* the deck */
        uint32_t bar = playing ? spos / 16u + 1u : 1u, a = playing ? (ui.frame * 12u) & 1023u : 0u;
        int32_t k, done = playing ? (int32_t)(spos * 8u / slen) : 0;
        top_sig = sig;
        cv_begin(240, 44, C_BLACK);
        te_digit(4, 7, 12, 28, 2, bar / 10u, playing ? C_WHITE : TE_G3);
        te_digit(20, 7, bar % 10u == 1u ? 4 : 12, 28, 2, bar % 10u, playing ? C_WHITE : TE_G3);   /* (a 1: a bar) */
        cv_text(bar % 10u == 1u ? 30 : 38, 20, &FONT_S, "BAR", TE_G3);
        for (k = 0; k < 2; k++) {
            int32_t cx = 100 + k * 48, pack = k ? 5 + done : 13 - done, sp;
            op_ell(cx, 21, 16, 16, OP_PUR);
            op_ell(cx, 21, pack, pack, TE_COL[3]);      /* the tape on this reel */
            op_ell(cx, 21, 3, 3, OP_PUR);
            for (sp = 0; sp < 3; sp++) {                /* three spokes */
                uint32_t an = (a + (uint32_t)sp * 341u) & 1023u;
                cv_line(cx + ((SINE[(an + 256u) & 1023u] * 4) >> 15), 21 + ((SINE[an] * 4) >> 15),
                        cx + ((SINE[(an + 256u) & 1023u] * 14) >> 15), 21 + ((SINE[an] * 14) >> 15), OP_PUR);
            }
        }
        cv_line(88, 33, 112, 41, TE_COL[3]);            /* the tape, past the head */
        cv_line(112, 41, 136, 41, TE_COL[3]);
        cv_line(136, 41, 160, 33, TE_COL[3]);
        cv_rect(119, 37, 10, 6, C_BLACK);
        op_box(119, 37, 10, 6, playing ? C_WHITE : TE_G3);
        for (i = 0; i < 4u; i++) {                      /* the sections */
            char ch[2] = {(char)('A' + i), 0};
            int32_t x = 188 + (int32_t)i * 12;
            int on = live_sec == (int8_t)i, used = (ready >> i) & 1u;
            if (on)
                cv_rect(x, 12, 11, 18, C_WHITE);
            else
                op_box(x, 12, 11, 18, used ? OP_PUR : OP_PUR_D);
            cv_text(x + 2, 13, &FONT_S, ch, on ? C_BLACK : used ? OP_PUR : OP_PUR_D);
        }
        cv_blit(0, OP_BODY_Y);
    }
    {   /* the selected track: its sound, its engine; REC / SOLO / MUTE */
        char b[24], e[12];
        uint32_t rec = (song.rec >> song.sel) & 1u, solo = (song.solo >> song.sel) & 1u, silent = trk_silent(ts) != 0;
        if (song.sel == TRK_DRUM) {
            str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
            str_cpy(e, "DRUMS", sizeof e);
        } else {
            trk_short_name(song.sel, b);
            str_cpy(e, ENGINES[ts->eng_req % NENGINES]->name, sizeof e);
        }
        b[13] = 0;
        sig = studio_hash(studio_hash(song.sel + rec * 8u + solo * 16u + silent * 32u, b), e);
        if (ui.force || sig != name_sig) {
            int32_t x;
            name_sig = sig;
            cv_begin(240, 16, C_BLACK);
            x = cv_text(4, 0, &FONT_S, b, C_WHITE);
            if (x + 8 + text_w(&FONT_S, e) < 196)
                cv_text(x + 8, 0, &FONT_S, e, TE_COL[song.sel & 3u]);
            if (rec)
                cv_text(236 - text_w(&FONT_S, "REC"), 0, &FONT_S, "REC", TE_RED);
            else if (solo)
                cv_text(236 - text_w(&FONT_S, "SOLO"), 0, &FONT_S, "SOLO", C_WHITE);
            else if (silent)
                cv_text(236 - text_w(&FONT_S, "MUTE"), 0, &FONT_S, "MUTE", TE_G3);
            cv_blit(0, OP_BODY_Y + 44);
        }
    }
    for (i = 0; i < NPART; i++) {                       /* the lanes */
        track_t *t = &trk[i];
        uint32_t sel = song.sel == i, len = trk_len(t), pos = t->seq_idx % len, bank = playing ? pos / 16u : 0u;
        uint32_t level = i == TRK_DRUM ? (uint32_t)song.g[G_DRLVL] : (uint32_t)t->p[P_LEVEL], silent = trk_silent(t) || !level;
        uint32_t rec = (song.rec >> i) & 1u;
        int32_t lo = 127, hi = 0, air = op_hop[i] > 2u ? 2 : 0;
        uint16_t col = silent ? TE_G2 : sel ? TE_COL[i] : TE_MID[i];
        sig = sel + level * 7u + silent * 997u + rec * 1999u + len * 37u + playing * 3u + (playing ? pos * 71u : 0u) + bank * 13u +
              (uint32_t)air * 5003u + ((song.solo >> i) & 1u) * 4999u;
        for (j = 0; j < len; j++) {
            if (i == TRK_DRUM) {
                sig = sig * 31u + dstep_mask(&t->dstep[j]);
            } else {
                const step_t *st = &t->step[j];
                sig = sig * 31u + (step_on(st) ? st->note[0] + 1u : 0u);
                if (step_on(st) && st->note[0] < lo)
                    lo = st->note[0];
                if (step_on(st) && st->note[0] > hi)
                    hi = st->note[0];
            }
        }
        if (!ui.force && sig == lane_sig[i])
            continue;
        lane_sig[i] = sig;
        if (hi - lo < 4)
            hi = lo + 4;
        cv_begin(240, 18, C_BLACK);
        if (sel)
            op_box(24, 0, 214, 18, TE_COL[i]);
        op_k = 1;                                       /* the character, half size */
        op_ink = silent ? OP_PUR_D : sel ? C_WHITE : OP_PUR;
        op_band_member(i, 12, 17 - air, air != 0, air != 0, 0, silent ? TE_G2 : TE_COL[i]);
        op_ink = OP_PUR;
        op_k = 3;
        for (j = 0; j < 16u; j++) {
            uint32_t p = bank * 16u + j;
            int32_t x = 28 + (int32_t)j * 11;
            if (p >= len)
                continue;
            if (i == TRK_DRUM) {                        /* a bar: 3 px a sound, up to 4 */
                uint32_t m = dstep_mask(&t->dstep[p]), n = 0;
                for (; m; m &= m - 1u)
                    n++;
                if (n)
                    cv_rect(x + 2, 14 - (int32_t)(n > 4u ? 4u : n) * 3, 5, (int32_t)(n > 4u ? 4u : n) * 3, col);
                else
                    cv_rect(x + 4, 13, 1, 1, TE_G2);
            } else if (step_on(&t->step[p])) {          /* a note at its height (the first of a chord) */
                cv_rect(x, 13 - (t->step[p].note[0] - lo) * 9 / (hi - lo), 9, 2, col);
                if (t->step[p].n > 1)
                    cv_rect(x, 9 - (t->step[p].note[0] - lo) * 9 / (hi - lo), 9, 1, col);
            } else if (t->step[p].time == ST_TIE) {
                cv_rect(x, 13, 9, 1, TE_G2);
            } else {
                cv_rect(x + 4, 13, 1, 1, TE_G2);
            }
            if (playing && p == pos)
                cv_rect(x, 15, 9, 2, C_WHITE);
        }
        cv_rect(207, 8, 28, 3, TE_G1);                  /* the level; REC: a red dot */
        if (!silent)
            cv_rect(207, 8, (int32_t)level * 28 / 127, 3, col);
        if (rec)
            te_disc(232, 4, 2, TE_RED);
        cv_blit(0, OP_BODY_Y + 60 + i * 18);
    }
}
