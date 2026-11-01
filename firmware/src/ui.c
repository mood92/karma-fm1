/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca user interface. Four columns map to KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "KARMA 3.0.3"  /* (3.0.3: a project loaded from the menu never overwrites another slot; 3.0.2: SYNC tempo steady; 3.0.1: the PLAITS song-mode crash fixed)  KARMA, free firmware for the FM-1: after SLOOP 2.6, based on Felucca (3.0: the editor reads the number, 2.5.1 and later) */
#endif
static void project_save(uint32_t slot);
static void arrangement_save(void);
static void panel_setup(void);
static void project_load(uint32_t slot);
static void project_erase(uint32_t slot);        /* SAVE > PROJECT > ERASE (2.6) */
static int project_used(uint32_t slot);
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static int up_load_to(track_t *t, uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static uint32_t up_engine(uint32_t k);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, UP_SLOTS = none */
{
    return t->user && up_used(t->user - 1u) ? t->user - 1u : UP_SLOTS;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

#define ACC C_HI                   /* amber everywhere; white is the only accent */
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? C_WHITE : TE_COL[(c) & 3u])   /* LIVE: one colour per knob */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout: four 60 px columns, 4 px inset */
/* Terminus 8x16 (S) and 16x32 (L) */
#define Y_HEAD 0
#define H_HEAD 20
#define Y_LABEL 26
#define Y_VALUE 44
#define Y_GAUGE 64
#define Y_SEP_END 70
#define Y_GRAPH 22
#define H_GRAPH 124
#define G_OY 24                       /* graphs draw in the lower 100 px; the focus readout sits on top */
#define Y_FOOT 202
#define H_FOOT 38

static struct {
    uint8_t home;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about (HOME held) */
    uint8_t menu_sel;
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t arm, arm_t;          /* destructive action armed: param id, frames left to confirm */
    uint32_t rec_t0;             /* REC press time (btn_hold) */
    uint8_t confirm;             /* 1 = "clear the sequence?" (REC held on SEQ / ARP), 2 = "clear track n?" (TRACKS) */
    uint8_t confirm_trk;         /* the track the dialog clears */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    /* layers (ui_layers.c): a function button held, the keys and knobs do something else */
    uint8_t layer;               /* the layer drawn (LY_*), LY_PLAY = none */
    uint8_t layer_btn;           /* its button (B_*), NB = none */
    uint8_t layer_used;          /* a key / knob / OCT was used while it was held: no tap on release */
    uint32_t layer_t0;           /* its press time (ms) */
    uint8_t step_page;           /* SEQ layer: the 16 steps shown (page x 16) */
    uint16_t step_held;          /* SEQ layer: the step keys held (white key index) */
    uint32_t step_sess;          /* SEQ layer: the undo session of this hold */
    uint8_t lock_par;            /* SEQ layer, a step held + KNOB 4: the parameter it locks (the last sound
                                  * parameter a knob changed on a page of the selected track; P_ED_FLT at boot) */
    uint8_t hold_kind;           /* a hold to confirm: 1 = clear the track (REC), 2 = save (SAVE) */
    uint32_t hold_t0;            /* (ms) */
    uint8_t hold_trk;
    uint32_t tap_ms[4];          /* tap tempo: the last taps */
    uint8_t tap_n;
    char msg[24];
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    char focus_l[8], focus_v[8], focus_u[8];   /* the touched column, shown large */
    char big_l[4][8], big_v[4][10], big_u[4][8];   /* the four columns, for the big values (2.4: pages without a graph) */
    uint16_t big_c[4];
    uint32_t graph_sig, head_sig, foot_sig, col_sig, frame;
    uint8_t graph_top;           /* the graph strip's top G_OY rows hold something */
} ui;

static const page_t *cur_page(void) { return &PAGES[ui.page]; }
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* ui_input.c */
static void layer_screen_draw(void);                            /* ui_layers.c */
static void hold_screen_draw(void);
static uint8_t layer_shown;

static uint32_t page_first(uint32_t fam)            /* the family's first page the selected track has */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam && page_shown(&PAGES[i]))
            return i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
            return i;
    return 0;
}

/* transient message in the top bar: a + b */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
}

static void ui_message(const char *s) { ui_say(s, ""); }

static void page_entered(void)
{
    const page_t *pg = cur_page();
    song.seq_mode = !ui.home && pg->fam == FAM_SEQ;
    if (!ui.home && pg->fam == FAM_GLO && pg->id[0] == G_CH1 && song.g[G_ROUTE]) {   /* (2.6) GLO > MIDI while MIDI 2 >
                                                                                   * IN sets a channel aside */
        if (song.g[G_ROUTE] == 1)
            ui_message("IN = CLOCK: NO NOTES");
        else {
            char b[10];
            str_cpy(b, "IN CH ", sizeof b);
            fmt_int(b + 6, song.g[G_ROUTE] - 1);
            ui_say(b, ": SELECTED TRK");
        }
    }
    ui.entry_open = 0;
    ui.hot_t = 0;                                /* the white value / focus box was the old page's */
    ui.force = 1;
}

/* (2.6) GLO > MIDI: two tracks never share a channel (it would play the first only, the other silent): a channel
 * turned (a knob, the editor, a backup) onto one another track has goes on to the next free one, the way it was
 * turned, and says whose it was. Every frame (ui_input) */
static void midi_chan_fix(void)
{
    static const uint8_t ID[4] = {G_CH1, G_CH2, G_CH3, G_DRCH};
    static int16_t was[4] = {-1, -1, -1, -1};
    uint32_t k, j;
    for (k = 0; k < 4u; k++) {
        int32_t v = song.g[ID[k]], dir, owner = -1;
        if (was[k] < 0 || v == was[k] || !v) {
            was[k] = (int16_t)v;
            continue;
        }
        dir = v > was[k] ? 1 : -1;
        for (;;) {
            int32_t o = -1;
            for (j = 0; j < 4u; j++)
                if (j != k && song.g[ID[j]] == v)
                    o = (int32_t)j;
            if (o < 0)
                break;
            if (owner < 0)
                owner = o;
            v += dir;
            if (v < 1) {                                /* (down past 1: OFF, never taken) */
                v = 0;
                break;
            }
            if (v > 16) {                               /* (none free up there: as it was) */
                v = was[k];
                break;
            }
        }
        if (owner >= 0) {
            char b[8];
            str_cpy(b, "CH ", sizeof b);
            fmt_int(b + 3, song.g[ID[k]]);
            ui_say(b, owner == 3 ? " = DRUMS" : owner == 0 ? " = TRACK 1" : owner == 1 ? " = TRACK 2" : " = TRACK 3");
            song.g[ID[k]] = (int16_t)v;
        }
        was[k] = (int16_t)v;
    }
}

static int step_on(const step_t *st) { return st->time == ST_NOTE && st->n; }
/* LEN (SLOOP 2.6, after Felucca 1.5 by Leo Kuroshita, Discussions #178, #173): a NOTE step's length is the step and
 * the TIE steps after it (inside LEN, no wrap); SEQ > STEP's KNOB 3 on a note sets it (ui_input.c step_edit). Longer:
 * the steps after it become clean TIEs (a REST or an empty step; it stops at the next step holding notes, which stays,
 * and at LEN); shorter: its last TIEs become RESTs (one holding notes keeps them). The steps' locks and nudges stay;
 * the turns on one step are one undo (ui_input.c). Returns the length it got */
static uint32_t note_len(const track_t *t, uint32_t i)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), n = 1;
    while (i + n < len && t->step[i + n].time == ST_TIE)
        n++;
    return n;
}
static uint32_t note_set_len(track_t *t, uint32_t i, uint32_t want)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), n = note_len(t, i);
    for (; n > want && n > 1u; n--) {
        if (!t->step[i + n - 1u].n)                    /* (a TIE holding notes, from TIME or an older SLOOP, */
            memset(&t->step[i + n - 1u], 0, sizeof(step_t));   /* keeps them, as TIME would) */
        t->step[i + n - 1u].time = ST_REST;
    }
    for (; n < want && i + n < len && !t->step[i + n].n; n++) {
        memset(&t->step[i + n], 0, sizeof(step_t));
        t->step[i + n].time = ST_TIE;
    }
    return n;
}
/* a synth step's velocity as it plays (seq.c step_vel: ACC 127, else its VEL, 96 when 0, through its first note's level) */
static uint32_t step_vel_shown(const step_t *st)
{
    uint32_t base = (st->flags & SF_ACCENT) ? 127u : (st->vel ? st->vel : 96u);
    return lvl_vel(st->lvl & 3u, base);
}
/* step i of track t has something to play (synth: notes, drums: a lane) */
static int trk_step_on(const track_t *t, uint32_t i)
{
    return is_drum(t) ? dstep_mask(&t->dstep[i % NSTEP]) != 0u : step_on(&t->step[i % NSTEP]);
}

static void step_clear(step_t *st)
{
    memset(st, 0, sizeof *st);
    st->time = ST_REST;
}

/* undo / redo (EDIT + OCT- / OCT+): the marked pattern and the one now swap places */
static int undo_swap(int redo)
{
    track_t *t;
    int16_t len;
    if (!undo.valid || (uint32_t)!!redo != undo.undone)
        return 0;
    t = &trk[undo.trk % NTRK];
    fm1_irq_off();
    {
        uint32_t i;
        for (i = 0; i < NSTEP; i++) {
            step_t x = t->step[i];
            t->step[i] = undo.st[i];
            undo.st[i] = x;
        }
    }
    len = t->p[P_SLEN];
    t->p[P_SLEN] = undo.len;
    undo.len = len;
    undo.undone = (uint8_t)!redo;
    fm1_irq_on();
    sync_reload = 1;
    ui.force = 1;
    return 1;
}

/* SEQ cursor: wraps inside the pattern length, the bank follows, a step entry ends */
static void cursor_set(int32_t c)
{
    int32_t len = TSEL->p[P_SLEN] > 0 ? TSEL->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
}

static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TSEL->p[P_SLEN])
        cursor_set(TSEL->p[P_SLEN] - 1);
}

static void note_name(char *b, uint32_t n)
{
    str_cpy(b, N_NOTE[n % 12u], 4);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}

static void open_family(uint32_t fam)
{
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t i = ui.page + 1u;
        while (i < NPAGES && PAGES[i].fam == fam && !page_shown(&PAGES[i]))
            i++;                                        /* (not a page of another engine: EDIT > FILTER) */
        if (i >= NPAGES || PAGES[i].fam != fam)
            i = page_first(fam);
        ui.page = (uint8_t)i;
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam && page_shown(&PAGES[ui.fam_last[fam]])
                      ? ui.fam_last[fam] : (uint8_t)page_first(fam);
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
}

/* SELECT on a page: the previous / next page of its family, as tapping the button again but both ways,
 * stopping at the ends. Not the screens of their own (SONG, DRUMS) nor a family of one page (TRACKS):
 * 0 then, and SELECT is the tempo there */
static int page_walk(int32_t s)
{
    const page_t *pg = cur_page();
    uint32_t i, fam = pg->fam, n = 0;
    int32_t cur = -1, to;
    uint8_t idx[12];                                    /* (FX has nine) */
    if (ui.home || pg->scope == SC_SONG || pg->scope == SC_DRUM)
        return 0;
    for (i = 0; i < NPAGES && n < 12u; i++) {
        if (PAGES[i].fam != fam || PAGES[i].scope == SC_SONG || PAGES[i].scope == SC_DRUM || !page_shown(&PAGES[i]))
            continue;
        if (i == ui.page)
            cur = (int32_t)n;
        idx[n++] = (uint8_t)i;
    }
    if (n < 2u || cur < 0)
        return 0;
    to = clamp(cur + s, 0, (int32_t)n - 1);
    if (to != cur) {
        ui.page = idx[to];
        ui.fam_last[fam] = ui.page;
        page_entered();
    }
    return 1;
}

/* the page shown is one the selected track has (2.6: EDIT > FILTER, ANALOG only): after a track or engine change,
 * from the previous page of the family that it has (a hidden page is never the first one), once a frame */
static void page_fix(void)
{
    uint32_t i = ui.page, fam = PAGES[i].fam;
    if (ui.home || page_shown(&PAGES[i]))
        return;
    while (i > 0u && PAGES[i - 1u].fam == fam && !page_shown(&PAGES[i - 1u]))
        i--;
    ui.page = (uint8_t)(i > 0u && PAGES[i - 1u].fam == fam ? i - 1u : page_first(fam));
    ui.fam_last[fam] = ui.page;
    page_entered();
}

static void go_home(void)
{
#if FELUCCA_ARRANGER
    ui.home = 0;
    ui.page = (uint8_t)page_first(FAM_TRK);
#else
    ui.home = 1;
#endif
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* ------------------------------------------------------- track setup --- */
/* LIVE: no factory sequence patterns. Loading a sound (factory or user preset) never
 * writes the sequencer: every pattern is the one the player records or enters. */

/* the parts' sounds at power-on (engine, preset): bass, pad, lead */
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_ACID, 0}, {ENGI_ELEM, 3}, {ENGI_PRISM, 0}};   /* ACID TB ACID, ELEMENTS PICKED BAR, PRISM FOLD LEAD */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }

static int seq_is_empty(const track_t *t) { return track_empty(t); }

static void track_defaults_steps(track_t *t) { steps_clear(t); }

/* what loading a sound (factory or user preset) leaves alone: the mix (LEVEL, PAN, MUTE:
 * the TRACKS faders, and the track FILTER), the pattern parameters (LEN, DIV, SWING, GATE) and the key the part plays
 * in (ROOT, SCALE, QNT, CHORD: the song's; SEL + key sets the root of every part). The SLICER is
 * part of the sound: a factory preset turns it OFF (its defaults), a user preset brings its own */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_SLEN && i <= P_SGATE) ||
           (i >= P_ROOT && i <= P_QUANT) || i == P_CHORD || i == P_TFLT || i == P_STRUM || i == P_VLEAD;
}

/* preset pi of the engine the track asked for: the whole sound (not the pattern parameters) */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
    sound_change(t);                                  /* (2.6: what sounds fades out with the old sound, no click) */
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    if (!e->npresets)
        return;
    pi %= e->npresets;
    t->preset = (uint8_t)pi;
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))
            t->p[i] = TP[i].def;                     /* sounds the same after any edit (not the pattern, not the mix) */
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)e->presets[pi].e[i];
    t->p[P_ATK] = e->presets[pi].env[0];
    t->p[P_DEC] = e->presets[pi].env[1];
    t->p[P_SUS] = e->presets[pi].env[2];
    t->p[P_REL] = e->presets[pi].env[3];
    t->p[P_ED_FLT] = e->presets[pi].fenv;
    t->p[P_ED_FX] = preset_trim(t->eng_req % NENGINES, pi);  /* level-matched (tools/level_presets.py) */
    t->p[P_VOICE] = e->presets[pi].mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
    {   /* the rest of the patch: sends, arpeggiator (never a pattern: LIVE) */
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        const preset_t *pr = &e->presets[pi];
        for (i = 0; i < 4u; i++) {
            t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
            t->p[P_AMODE + i] = (int16_t)(pr->arp[i] ? pr->arp[i] - 1 : TP[P_AMODE + i].def);
        }
        preset_extras(t->p, pr);                     /* glide, pitch / LFO modulation, voice mode */
    }
}

/* the engine's defaults and its first preset. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
    fm1_irq_off();
    locks_restore(t);                                 /* (a lock in force: its base first, then the new sound) */
    sound_change(t);                                  /* (2.6: the sound as it is, before anything new is written) */
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, 0);
    fm1_irq_on();
}

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* switch engine (its defaults + first preset) and say so */
static void select_engine(uint32_t e)
{
    if (is_drum(TSEL))
        return;
    set_engine(e);
    ui_say("ENGINE ", ENGINES[TSEL->eng_req]->name);
    ui.force = 1;
}

/* the factory presets as one list by kind (basses, keys, organs, pads, leads, plucks and bells, stabs,
 * the rest), then the used user presets: the PRESETS knob and the PRESETS page browse it. By name: an
 * engine's preset table may change order; tests/ui_pages_test.c checks every preset is here once */
enum { BK_BASS, BK_KEYS, BK_ORGAN, BK_PAD, BK_LEAD, BK_PLUCK, BK_STAB, BK_FX };
static const char *const BANK_KIND[] = {"BASS", "KEYS", "ORGN", "PAD", "LEAD", "PLCK", "STAB", "FX"};
static const struct { uint8_t kind, e; const char *name; } BANK[] = {   /* the PRESETS list: each track sees its engines' */
    /* ACID (track 1) */
    {BK_BASS, ENGI_ACID, "TB ACID"}, {BK_BASS, ENGI_ACID, "TB SQUARE"}, {BK_BASS, ENGI_ACID, "TB DIRTY"},
    {BK_BASS, ENGI_ACID, "TB DEEP"}, {BK_BASS, ENGI_ACID, "TB SQUELCH"}, {BK_BASS, ENGI_ACID, "TB RUBBER"},
    /* JUNO (track 1) */
    {BK_BASS, ENGI_JUNO, "JUNO BASS"}, {BK_BASS, ENGI_JUNO, "SUB 106"}, {BK_KEYS, ENGI_JUNO, "JUNO KEYS"},
    {BK_ORGAN, ENGI_JUNO, "JUNO ORGAN"}, {BK_PAD, ENGI_JUNO, "JUNO STRING"}, {BK_PAD, ENGI_JUNO, "JUNO PAD"},
    {BK_PAD, ENGI_JUNO, "JUNO SWEEP"}, {BK_LEAD, ENGI_JUNO, "JUNO LEAD"}, {BK_LEAD, ENGI_JUNO, "JUNO HOOVER"},
    {BK_STAB, ENGI_JUNO, "JUNO BRASS"}, {BK_STAB, ENGI_JUNO, "JUNO STAB"},
    /* ELEMENTS (track 2) */
    {BK_PAD, ENGI_ELEM, "BOW STRING"}, {BK_PAD, ENGI_ELEM, "BOW GLASS"}, {BK_BASS, ENGI_ELEM, "BOW BASS"},
    {BK_LEAD, ENGI_ELEM, "PIPE"}, {BK_LEAD, ENGI_ELEM, "BREATHY"}, {BK_LEAD, ENGI_ELEM, "BLOWN BELL"},
    {BK_FX, ENGI_ELEM, "BOUNCE BAR"}, {BK_FX, ENGI_ELEM, "MARBLES"}, {BK_PLUCK, ENGI_ELEM, "RATTLE BELL"},
    {BK_PLUCK, ENGI_ELEM, "PICKED BAR"}, {BK_PLUCK, ENGI_ELEM, "PICK CHIME"},
    {BK_PAD, ENGI_ELEM, "OMINOUS"}, {BK_PAD, ENGI_ELEM, "DARK DRONE"}, {BK_PLUCK, ENGI_ELEM, "OMEN BELL"},
    /* PLAITS (track 3) */
    {BK_LEAD, ENGI_PRISM, "FOLD LEAD"}, {BK_BASS, ENGI_PRISM, "FOLD BASS"}, {BK_PLUCK, ENGI_PRISM, "WEST PLUCK"},
    {BK_PAD, ENGI_PRISM, "GLASS FOLD"}, {BK_PLUCK, ENGI_PRISM, "BUCHLA BELL"}, {BK_PAD, ENGI_PRISM, "FOLD DRONE"},
    {BK_LEAD, ENGI_PRISM, "SUPER SWARM"}, {BK_PAD, ENGI_PRISM, "SWARM PAD"}, {BK_PAD, ENGI_PRISM, "GLISS CLOUD"},
    {BK_FX, ENGI_PRISM, "GRAIN RAIN"}, {BK_BASS, ENGI_PRISM, "SWARM BASS"}, {BK_PAD, ENGI_PRISM, "TABLE SWEEP"},
    {BK_BASS, ENGI_PRISM, "TABLE BASS"}, {BK_KEYS, ENGI_PRISM, "DIGI KEYS"}, {BK_PAD, ENGI_PRISM, "VOX TABLE"},
    {BK_LEAD, ENGI_PRISM, "TABLE LEAD"}, {BK_ORGAN, ENGI_PRISM, "GLASS ORGAN"}, {BK_PAD, ENGI_PRISM, "OVERTONES"},
    {BK_PAD, ENGI_PRISM, "SPECTRAL PD"}, {BK_PLUCK, ENGI_PRISM, "ADD BELL"}, {BK_PAD, ENGI_PRISM, "STRING MCH"},
    {BK_PAD, ENGI_PRISM, "SOLINA MIN"}, {BK_ORGAN, ENGI_PRISM, "ORGAN CHRD"}, {BK_STAB, ENGI_PRISM, "HOUSE STAB"},
    {BK_STAB, ENGI_PRISM, "RAVE 5TH"}, {BK_PAD, ENGI_PRISM, "DREAM 69"},
    {BK_LEAD, ENGI_PRISM, "VA SAW LEAD"}, {BK_PAD, ENGI_PRISM, "VA PULSE PAD"}, {BK_BASS, ENGI_PRISM, "VCF BASS"},
    {BK_PAD, ENGI_PRISM, "VCF SWEEP"}, {BK_KEYS, ENGI_PRISM, "PD KEYS"}, {BK_LEAD, ENGI_PRISM, "PD RESO"},
    {BK_PLUCK, ENGI_PRISM, "FM BELL"}, {BK_BASS, ENGI_PRISM, "FM BASS"}, {BK_KEYS, ENGI_PRISM, "DX BANK A"},
    {BK_KEYS, ENGI_PRISM, "DX BANK B"}, {BK_KEYS, ENGI_PRISM, "DX BANK C"}, {BK_PAD, ENGI_PRISM, "FORMANT VOX"},
    {BK_LEAD, ENGI_PRISM, "GRAIN BUZZ"}, {BK_LEAD, ENGI_PRISM, "TERRAIN"}, {BK_PAD, ENGI_PRISM, "TERRAIN PAD"},
    {BK_PAD, ENGI_PRISM, "STR MACHINE"}, {BK_PAD, ENGI_PRISM, "POLY STRING"}, {BK_LEAD, ENGI_PRISM, "CHIP LEAD"},
    {BK_PLUCK, ENGI_PRISM, "CHIP ARP"}, {BK_PLUCK, ENGI_PRISM, "KS GUITAR"}, {BK_PLUCK, ENGI_PRISM, "KS HARP"},
    {BK_PLUCK, ENGI_PRISM, "MODAL BELL"}, {BK_PLUCK, ENGI_PRISM, "MODAL WOOD"}, {BK_FX, ENGI_PRISM, "NOISE SWEEP"},
    {BK_FX, ENGI_PRISM, "NOISE TONE"}, {BK_FX, ENGI_PRISM, "SPEECH"}, {BK_FX, ENGI_PRISM, "ROBOT WORDS"},
    {BK_FX, ENGI_PRISM, "PARTICLES"}, {BK_FX, ENGI_PRISM, "DROPLETS"}, {BK_FX, ENGI_PRISM, "PL KICK"},
    {BK_FX, ENGI_PRISM, "PL SNARE"}, {BK_FX, ENGI_PRISM, "PL HIHAT"},
};
#define NBANK (sizeof BANK / sizeof BANK[0])
static uint8_t bank_pi[NBANK];                       /* the preset index of each entry in its engine */
static uint8_t bank_ready;
static void bank_resolve(void)
{
    uint32_t i, k;
    for (i = 0; i < NBANK; i++) {
        const engine_t *e = ENGINES[BANK[i].e % NENGINES];
        bank_pi[i] = 0xFF;
        for (k = 0; k < e->npresets; k++)
            if (str_eq(e->presets[k].name, BANK[i].name))
                bank_pi[i] = (uint8_t)k;
    }
    bank_ready = 1;
}
/* the list a track browses: the factory sounds of its engines (eng_allowed), then its engines' user presets.
 * pl_* walk it: n-th entry -> BANK index (< NBANK) or NBANK + user slot */
static uint32_t pl_entry(const track_t *t, uint32_t n)
{
    uint32_t i, k, c = 0;
    if (!bank_ready)
        bank_resolve();
    for (i = 0; i < NBANK; i++)
        if (eng_allowed(t, BANK[i].e) && bank_pi[i] != 0xFF && c++ == n)
            return i;
    for (i = 0; i < up_count(); i++) {
        k = up_nth(i);
        if (eng_allowed(t, up_engine(k)) && c++ == n)
            return NBANK + k;
    }
    return 0xFFFFu;
}
static uint32_t pl_total(const track_t *t)
{
    uint32_t i, c = 0;
    if (!bank_ready)
        bank_resolve();
    for (i = 0; i < NBANK; i++)
        c += eng_allowed(t, BANK[i].e) && bank_pi[i] != 0xFF;
    for (i = 0; i < up_count(); i++)
        c += eng_allowed(t, up_engine(up_nth(i)));
    return c;
}
static uint32_t preset_pos(uint32_t *total)          /* list index of the selected track's preset */
{
    uint32_t n, tot = pl_total(TSEL), cur = 0;
    for (n = 0; n < tot; n++) {
        uint32_t x = pl_entry(TSEL, n);
        if (user_of(TSEL) < UP_SLOTS ? x == NBANK + user_of(TSEL)
                                     : x < NBANK && BANK[x].e == TSEL->eng_req && bank_pi[x] == TSEL->preset)
            cur = n;
    }
    *total = tot;
    return cur;
}

/* list index n (< total) of track t -> engine, *k its preset; NENGINES = user preset, *k its slot */
static uint32_t preset_at_of(const track_t *t, uint32_t n, uint32_t *k)
{
    uint32_t x = pl_entry(t, n);
    if (x == 0xFFFFu) {
        *k = 0;
        return eng_default(t);
    }
    if (x >= NBANK) {
        *k = x - NBANK;
        return NENGINES;
    }
    *k = bank_pi[x];
    return BANK[x].e;
}
static uint32_t preset_at(uint32_t n, uint32_t *k) { return preset_at_of(TSEL, n, k); }
static uint32_t preset_list_idx(uint32_t n) { uint32_t x = pl_entry(TSEL, n); return x < NBANK ? x : NBANK; }
static const char *preset_kind(uint32_t n) { uint32_t x = preset_list_idx(n); return x < NBANK ? BANK_KIND[BANK[x].kind] : "USER"; }

/* list index n into track t (2.6: MIDI Program Change, any track); 0 done, 1 no such sound */
static int preset_go_to(track_t *t, uint32_t n)
{
    uint32_t k, e;
    if (is_drum(t) || n >= pl_total(t))
        return 1;
    e = preset_at_of(t, n, &k);
    if (e == NENGINES)
        return up_load_to(t, k);
    if (e != t->eng_req)
        set_engine_of(t, e);
    apply_preset_to(t, k);
    ui.force = 1;
    return 0;
}
/* LIVE SETLIST: 20 slots, each a list index of the PRESETS list (as preset_go_to takes it). The LIVE layer (hold
 * LFO) turns the keys into the slots; MIDI Program Change 0..19 picks a slot (pc_poll) */
#define SETLIST_N 20u
static uint8_t setlist[SETLIST_N];
static uint8_t live_slot = 0xFF;                     /* the slot last recalled (0xFF none) */
static uint8_t setlist_ready;
static uint8_t live_store;                           /* OCT+ tapped in LIVE: the next slot key stores */
static void setlist_default(void)
{
    uint32_t i;
    for (i = 0; i < SETLIST_N; i++)
        setlist[i] = (uint8_t)i;
    setlist_ready = 1;
}
/* a knob touched: its name and value in the message line for a moment (LIVE and HOME) */
static void knob_popup(const param_desc_t *d, int16_t v)
{
    char val[12], b[24];
    const char *unit;
    uint32_t n;
    param_format(d, v, val, &unit);
    str_cpy(b, d->label, 12);
    n = str_len(b);
    b[n++] = ' ';
    str_cpy(b + n, val, sizeof b - n);
    n = str_len(b);
    if (unit && unit[0] && n + 1u < sizeof b)
        str_cpy(b + n, unit, sizeof b - n);
    ui_message(b);
}
/* the name of slot s's sound on the selected track (12 characters + 0 at b) */
static void setlist_name(uint32_t s, char *b)
{
    uint32_t x = pl_entry(TSEL, setlist[s % SETLIST_N]);
    b[0] = 0;
    if (x < NBANK)
        str_cpy(b, BANK[x].name, 13);
    else if (x != 0xFFFFu)
        up_name(x - NBANK, b);
}
/* a slot key (the LIVE layer): recall the slot's sound on the selected track; with OCT+ held, store the sound
 * the track has now into the slot instead */
static void live_key(uint32_t s)
{
    uint32_t tot;
    char b[4];
    if (s >= SETLIST_N)
        return;
    if (!setlist_ready)
        setlist_default();
    fmt_int(b, (int32_t)s + 1);
    if (is_drum(TSEL)) {
        ui_message("DRUM TRACK");
        return;
    }
    if (live_store || ((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u)) {
        live_store = 0;
        setlist[s] = (uint8_t)preset_pos(&tot);
        settings_later = 1;                          /* (saved with the settings, once stopped) */
        ui_say("STORED SLOT ", b);
        return;
    }
    if (preset_go_to(TSEL, setlist[s])) {
        ui_say("EMPTY SLOT ", b);
        return;
    }
    live_slot = (uint8_t)s;
    ui_say("SLOT ", b);
}
/* MIDI Program Change (2.6, after Felucca 1.5.1, PR #179 by @renebohne): seq.c queues it per track (the track its
 * channel plays, GLO > MIDI); here, in the main loop (a flash read, a sound loaded): a synth track takes the n-th sound
 * of the PRESETS list (0 the first factory sound, the user presets after the 153), the drum track kit n. A number past
 * the end does nothing (no wrap) */
static void pc_poll(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        uint32_t r = pc_req[i], n;
        char b[12];
        if (!r)
            continue;
        pc_req[i] = 0;
        n = r - 1u;
        if (!is_drum(&trk[i])) {                       /* LIVE: the program is a setlist slot */
            if (n >= SETLIST_N)
                continue;
            if (!setlist_ready)
                setlist_default();
            live_slot = (uint8_t)n;
            n = setlist[n];
        }
        if (is_drum(&trk[i])) {
            if (n >= DRUM_KITS)
                continue;
            trk[i].p[P_E0] = (int16_t)n;
            if (&trk[i] == TSEL)
                sync_reload = 1;
        } else if (preset_go_to(&trk[i], n)) {
            continue;
        }
        str_cpy(b, "T1 PROG ", sizeof b);
        b[1] = (char)('1' + i);
        fmt_int(b + 8, (int32_t)n);
        ui_message(b);
        ui.force = 1;
    }
}
static void preset_go(uint32_t n)                    /* load list index n into the selected track */
{
    uint32_t k, e = preset_at(n, &k);
    if (is_drum(TSEL))
        return;                                      /* one GM kit: nothing to browse */
    if (e == NENGINES) {
        up_load(k);
        return;
    }
    if (e != TSEL->eng_req)
        select_engine(e);
    apply_preset(k);
    ui.force = 1;
}

/* HOME: what KNOB k edits: the engine's four main parameters; on the drum track
 * LEVEL and REV (GLO > DRUMS), PAN and LEN */
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    static const uint8_t DRUM_HOME[4][2] = {{1, G_DRLVL}, {1, G_DRREV}, {0, P_PAN}, {0, P_SLEN}};
    uint32_t id;
    if (is_drum(TSEL)) {
        id = DRUM_HOME[k & 3u][1];
        if (DRUM_HOME[k & 3u][0]) {
            *vp = &song.g[id];
            return &GP[id];
        }
        *vp = &TSEL->p[id];
        return &TP[id];
    }
    id = ENGINES[TSEL->eng_req % NENGINES]->macro[k & 3u];
    *vp = &TSEL->p[id];
    return track_desc(TSEL, id);
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    rec_follow(i);                                   /* LIVE: recording follows the selected track */
    ui.entry_open = 0;
    ui.cursor = 0;
    ui.bank = 0;
    sync_reload = 1;
    ui.force = 1;
}

/* ------------------------------------------------------- MIDI LEARN --- */
/* MIDI LEARN (SLOOP 2.6, after Felucca 1.5 by Leo Kuroshita, Discussion #170; the map: seq.c ml_tab). GLO held +
 * the black key F#3 turns it on and off. On: a knob turned on a track's page (the sound's pages, HOME, TRACKS' LEVEL
 * and PAN) picks its parameter on the selected track as it edits it; the next CC that comes (any channel IN hears) is
 * set to it, and stays until something else is learned on it or HOME > SYSTEM > LEARN CLEAR. Then turn the next knob.
 * One CC sets one parameter, a parameter has one CC: learning one again replaces both. The settings keep the map */
static struct { uint8_t on, pick, trk, id; } mlu;
static uint32_t ml_code(uint32_t id) { return id >= P_E0 ? ML_E0 + id - P_E0 : id + 1u; }
static void ml_put(uint32_t i, uint32_t e)       /* (the ISR reads the entry: never half of one) */
{
    fm1_irq_off();
    ml_tab[i % ML_N] = (uint16_t)e;
    fm1_irq_on();
}
static uint32_t ml_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ML_N; i++)
        n += (ml_tab[i] >> 9) != 0u;
    return n;
}
/* cc -> parameter id of track k, in place of what either had; 0 = no room, 2 = a CC that is never learned */
static uint32_t ml_learn(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i, e;
    if (!ml_free_cc(cc) || k >= NTRK || id >= P_COUNT)
        return 2;
    for (i = 0; i < ML_N; i++)
        if ((e = ml_tab[i]) >> 9 && ((e & 127u) == cc || ((e >> 9) == ml_code(id) && ((e >> 7) & 3u) == k)))
            ml_put(i, 0);
    for (i = 0; i < ML_N; i++)
        if (!(ml_tab[i] >> 9)) {
            ml_put(i, cc | k << 7 | ml_code(id) << 9);
            return 1;
        }
    return 0;
}
static int ml_clear_all(void)                    /* HOME > SYSTEM > LEARN CLEAR; 1 = there were */
{
    uint32_t i, n = ml_count();
    for (i = 0; i < ML_N; i++)
        ml_put(i, 0);
    if (n)
        settings_later = 1;                      /* (saved when quiet) */
    return n != 0u;
}
static void ml_toggle(void)                      /* GLO + F#3 */
{
    mlu.on = (uint8_t)!mlu.on;
    mlu.pick = 0;
    ml_arm = 0;
    ml_heard = 0;
    ui_message(mlu.on ? "LEARN: TURN A KNOB" : "LEARN DONE");
    ui.force = 1;
}
/* a knob turned parameter id of track t: picked while learning (ui_input.c) */
static void ml_knob(const track_t *t, uint32_t id)
{
    char b[16];
    if (!mlu.on || id >= P_COUNT)
        return;
    if (!mlu.pick || mlu.id != id || mlu.trk != (uint8_t)trk_index(t)) {
        mlu.trk = (uint8_t)trk_index(t);
        mlu.id = (uint8_t)id;
        b[0] = 'T';
        b[1] = (char)('1' + mlu.trk);
        b[2] = ' ';
        str_cpy(b + 3, track_desc(t, id)->label, 9);
        ui_say(b, ": SEND A CC");
    }
    mlu.pick = 1;
    ml_heard = 0;
    ml_arm = 1;
}
/* each frame: a CC heard for the picked parameter is learned */
static void ml_poll(void)
{
    uint32_t cc = ml_heard, r;
    char a[16], b[16];
    if (!cc)
        return;
    ml_heard = 0;
    if (!mlu.on || !mlu.pick)
        return;
    cc--;
    r = ml_learn(cc, mlu.trk, mlu.id);
    if (r != 1u) {
        ui_message(r ? "CC NOT LEARNABLE" : "LEARN FULL");
        return;
    }
    str_cpy(a, "CC", 4);
    fmt_int(a + 2, (int32_t)cc);
    str_cpy(a + str_len(a), " = T", 5);
    b[0] = (char)('1' + mlu.trk);
    b[1] = ' ';
    str_cpy(b + 2, track_desc(&trk[mlu.trk % NTRK], mlu.id)->label, 9);
    ui_say(a, b);
    mlu.pick = 0;
    ml_arm = 0;
    settings_later = 1;                          /* (saved when quiet) */
}
