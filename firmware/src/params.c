/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
/* SLOOP 2.6, after Felucca 1.4 / 1.5 (Leo Kuroshita) ----------------------------------------------------------------
 * LFO 2. SYNC: OFF (RATE), or one cycle per note value of the tempo, longest first (LSYNC_UNITS: in core.h div_units'
 * units); TRIG: NOTE restarts it at PHS on a fresh phrase (as before), FREE never; POL: BI -1..+1 (as before), UNI 0..+1 */
static const char *const N_LSYNC[] = {"OFF", "4BAR", "2BAR", "1BAR", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
static const char *const N_LTRIG[] = {"NOTE", "FREE"};
static const char *const N_LPOL[] = {"BI", "UNI"};
/* GLO > MIDI (2.6): a synth track's MIDI channel, and the other channels */
static const char *const N_MCH[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static const char *const N_MOTH[] = {"SEL", "OFF"};
/* GLO > TEXTURE (2.6, fx.c texture_*): the bed's sources (the user slots last), its movement's length, when it plays */
static const char *const N_TXSRC[] = {"OFF", "VINYL", "TAPE", "RAIN", "WIND", "RADIO", "ROOM", "USR1", "USR2", "USR3", "USR4"};
static const char *const N_TXRATE[] = {"1BAR", "2BAR", "4BAR", "8BAR", "16BAR"};
static const char *const N_TXWHEN[] = {"PLAY", "ALWAYS"};
/* ANALOG's filter TYPE (eng_analog.c): the SVF's low-pass (as always), band-pass, high-pass output */
static const char *const N_FTYPE[] = {"LP", "BP", "HP"};
/* ENV SYNC (voice.c): ATK DEC REL as note values of the tempo, shortest first. 0..127 split evenly over these 25
 * (param_format's F_INT name list: value v is ESYNC_NAMES[v * 25 / 128]); ESYNC_UNITS: each in 1/384 of a bar of
 * 4/4 (a quarter 96), 0 = the shortest time ATK / DEC / REL 0 has. 0-terminated (param_format) */
#define ESYNC_N 25u
static const char *const ESYNC_NAMES[ESYNC_N + 1u] = {
    "0", "1/64T", "1/64", "1/32T", "1/64D", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D",
    "1/4", "1/2T", "1/4D", "1/2", "1/1T", "1/2D", "1/1", "1/1D", "2BAR", "3BAR", "4BAR", 0};
static const uint16_t ESYNC_UNITS[ESYNC_N] = {0, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 64, 72,
                                              96, 128, 144, 192, 256, 288, 384, 576, 768, 1152, 1536};
static uint32_t esync_idx(int32_t v) { return (uint32_t)(v & 127) * ESYNC_N >> 7; }   /* (param_format's split) */
/* the INSERT's types (fx.c IT_*; append-only: stored). Its three values A B C mean what the type says (ins_desc) */
static const char *const N_ITYPE[] = {"OFF", "SOFT", "HARD", "FOLD", "FUZZ", "CRUSH", "PHASR", "FLANG", "CHOR"};
/* CRUSH: BITS 1..16 and RATE (the held sample's rate) over the range in 16 even parts (0-terminated) */
static const char *const N_IBITS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                      "16", 0};
static const char *const N_IRATE[] = {"689", "919", "1.1k", "1.4k", "1.8k", "2.2k", "2.8k", "3.7k", "4.4k", "5.5k",
                                      "7.4k", "8.8k", "11k", "15k", "22k", "44k", 0};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD"};
static const char *const N_MOUT[] = {"KEYS", "SEQ"};
static const char *const N_MIN[] = {"NOTES", "CLOCK", "CH 1", "CH 2", "CH 3", "CH 4", "CH 5", "CH 6", "CH 7", "CH 8", "CH 9",
                                     "CH 10", "CH 11", "CH 12", "CH 13", "CH 14", "CH 15", "CH 16"};  /* G_ROUTE: MIDI in, notes and clock, the clock only, or (2.6) one channel only, on the selected track (seq.c) */    /* G_MIDI: what goes to MIDI OUT (seq.c seq_out_on) */
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
static const char *const N_SDIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1BAR", "2BAR"};   /* core.h div_units */
static const char *const N_DLY[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/8D", "1/16D", "1/4D"};   /* core.h dly_units
                                                                          * (2.6: 1/4D, after Felucca 1.5.1) */
static const char *const N_SCALE[] = {"CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                    "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_GKMODE[] = {"MIN", "DOR", "PHRY", "MAJ", "MIX"};   /* SEQ > HARMONY (gen_seq.c GK_SC) */
static const char *const N_GKPROG[] = {"DRONE", "SWAY", "DEEP", "EPIC", "POP", "MOOD"};
static const char *const N_GKBARS[] = {"1", "2", "4"};
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE"};   /* seq.c kb_map; 1 = SNAP as the old ON */
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_GLMODE[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_ORDER[] = {"NOTE", "PLAY"};
static const char *const N_CLICK[] = {"OFF", "REC", "ON"};   /* G_CLOCK is the metronome (seq.c click_tick) */
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_SYNC[] = {"INT", "USB", "TRS"};   /* G_SYNC: the tempo and PLAY / STOP from (seq.c mclk) */
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_DGMODE[] = {"OFF", "MAP", "EUCL"};
static const char *const N_PLDIR[] = {"FWD", "REV", "PEND", "RAND", "WALK"};
static const char *const N_VRLOOK[] = {"LIVE", "HOLD"};     /* SEQ > VARY: re-rolled on every pass of the loop, or the same each time (SEED) */
static const char *const N_KNOBS[] = {"GM", "70-77", "1-8", "AUTO", "PANEL"};
/* HEX (hex_drums.c: the LXR's drum voices, lxr_dsp.c): HM_*, HW_*, HC_* (the transients), HF_* */
static const char *const N_HXMODEL[] = {"DRUM", "SNARE", "CLAP", "CYM", "HAT"};
static const char *const N_HXWAVE[] = {"SIN", "TRI", "SAW", "SQR", "NOIS", "P12", "P25", "P37"};
static const char *const N_HXCLICK[] = {"SNAP", "OFFS", "CLK", "CK2", "TICK", "KICK", "RIM", "DRIP*", "HAT", "CLAP",
                                        "KK2*", "SNR", "TOM", "SP2*"};   /* *: not in the flash, TICK KICK SNR play */
static const char *const N_HXFILT[] = {"LP", "HP", "BP", "UBP", "NTCH", "PEAK", "LP2", "OFF"};
static const char *const N_HXMODW[] = {"FMSIN", "FMTRI", "FMSAW", "FMSQR", "FMNOI", "FMPWM",   /* DRUM: the mod osc */
                                       "MXSIN", "MXTRI", "MXSAW", "MXSQR", "MXNOI", "MXPWM"};
static const char *const N_HXWAV2[] = {"SI/SI", "SI/TR", "SI/SA", "SI/SQ", "SI/NO", "SI/PW", "TR/SI", "TR/TR", "TR/SA",
                                       "TR/SQ", "TR/NO", "TR/PW", "SA/SI", "SA/TR", "SA/SA", "SA/SQ", "SA/NO", "SA/PW",
                                       "SQ/SI", "SQ/TR", "SQ/SA", "SQ/SQ", "SQ/NO", "SQ/PW", "NO/SI", "NO/TR", "NO/SA",
                                       "NO/SQ", "NO/NO", "NO/PW", "PW/SI", "PW/TR", "PW/SA", "PW/SQ", "PW/NO", "PW/PW"};
static const char *const N_AGSCALE[] = {"MIN", "PHRY", "HARM", "MPEN", "DOR", "MAJ"};   /* ACID GEN: TB-3PO's scales */
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
static const char *const N_CHORD[] = {"OFF", "TRIAD", "7TH", "9TH", "SUS4", "POWER"};   /* seq.c CHORD_DEG */
static const char *const N_ROLL[] = {"1/8", "1/16", "1/32", "32T", "1/64"};   /* seq.c ROLL_DEN */
static const char *const N_ENGNAME[] = {"ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN",
                                        "FM6", "PHYS", "NOISE",
#if FELUCCA_SLICE
                                             "SLICE",
#endif
                                        "PLAITS", "ELEMENTS", "ACID", "JUNO",
};
_Static_assert(sizeof N_ENGNAME / sizeof N_ENGNAME[0] == NENGINES, "a name an engine");

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

static const param_desc_t TP[P_COUNT] = {
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_ED_FX] = PD("TRIM", F_INT, -64, 63, 0),   /* the sound's level trim, 1/2 dB (presets set it) */
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIV, 2),
    [P_AOCT] = PD("OCT", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWG", F_SWING, 0, 100, 0),
    [P_APROB] = PD("PROB", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORD", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("QNT", N_QUANT, 0),
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_SDIV, 2),
    [P_SSWING] = PD("SWG", F_SWING, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_DIST] = PD("DST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DLY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GLMOD", N_GLMODE, 0),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DTUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
    [P_CHORD] = PE("CHORD", N_CHORD, 0),
    [P_TFLT] = PD("FILT", F_FILT, -64, 63, 0),
    [P_STRUM] = PD("STRUM", F_INT, -60, 60, 0),   /* ms a note: > 0 low to high (down), < 0 high to low */
    [P_VLEAD] = PE("VLEAD", N_ONOFF, 0),
    /* 2.6, after Felucca 1.5 / 1.4. The INSERT (fx.c track_insert): TYPE OFF = as before. A B C as the editor
     * protocol names them; the device shows the type's own labels (ins_desc) */
    [P_ITYPE] = PE("INSRT", N_ITYPE, 0),
    [P_IA] = PD("INS A", F_PCT, 0, 127, 64),
    [P_IB] = PD("INS B", F_PCT, 0, 127, 96),
    [P_IC] = PD("INS C", F_PCT, 0, 127, 96),
    [P_IMIX] = PD("MIX", F_PCT, 0, 127, 127),
    [P_FTYPE] = PE("TYPE", N_FTYPE, 0),           /* ANALOG's filter, LP = as before (eng_analog.c) */
    [P_ESYNC] = PE("ESYNC", N_ONOFF, 0),          /* OFF = ATK DEC REL in ms, as before (voice.c) */
    [P_LSYNC] = PE("SYNC", N_LSYNC, 0),           /* LFO 2: OFF = RATE, as before */
    [P_LTRIG] = PE("TRIG", N_LTRIG, 0),
    [P_LPOL] = PE("POL", N_LPOL, 0),
};
/* a preset's extra parameters (preset_t.x) into p, each clamped to its range */
static void preset_extras(int16_t *p, const preset_t *pr)
{
    uint32_t i;
    for (i = 0; i + 1u < 8u; i += 2u) {
        int32_t id = pr->x[i] - 1;
        if (id >= 0 && id < P_E0)
            p[id] = (int16_t)clamp(pr->x[i + 1u], TP[id].min, TP[id].max);
    }
}


static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 90),
    [G_SWING] = PD("SWING", F_SWING, 0, 100, 0),
    [G_CLOCK] = PE("CLICK", N_CLICK, 0),            /* (the old CLK slot: projects keep their format) */
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),
    [G_DTIME] = PE("TIME", N_DLY, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    [G_MIDI] = PE("OUT", N_MOUT, 0),            /* MIDI OUT: the keys, or the sequencer too (a setting of the FM-1) */
    [G_SYNC] = PE("SYNC", N_SYNC, 0),           /* a setting of the FM-1, not of a project (panel.c lights_sync) */
    [G_ROUTE] = PE("IN", N_MIN, 0),               /* MIDI IN: NOTES (and the clock), CLOCK only, or CH n: that channel plays the
                                                   * selected track (a setting of the FM-1; it starts at CH 1: panel.c lights_min) */
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, 4, 1),
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_ENGGO] = PE("SET", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    [G_DRCH] = PE("CH4", N_MCH, 10),                  /* GM drum part (track 4) MIDI channel, 0 = OFF (2.6: as GLO > MIDI's CH1..3) */
    [G_DRLVL] = PD("LVL", F_INT, 0, 127, 100),
    [G_DRREV] = PD("REV", F_INT, 0, 127, 0),          /* (dry until asked for; SLOOP: 16) */
    [G_DUST] = PD("DUST", F_PCT, 0, 127, 0),
    [G_DUCK] = PD("DUCK", F_PCT, 0, 127, 0),
    [G_FILT] = PD("FILT", F_FILT, -64, 63, 0),
    [G_ROLL] = PE("ROLL", N_ROLL, 1),
    [G_NEWPRJ] = PE("NEW", N_GO, 0),
    [G_DRDLY] = PD("DLY", F_INT, 0, 127, 0),
    [G_CH1] = PE("CH1", N_MCH, 1),                /* 2.6, GLO > MIDI: synth track 1's channel, in and out (OFF: */
    [G_CH2] = PE("CH2", N_MCH, 2),                /* no MIDI in; out on its old one) */
    [G_CH3] = PE("CH3", N_MCH, 3),
    [G_CHOTH] = PE("OTHER", N_MOTH, 0),           /* the channels no track has: SEL the selected track, OFF none */
    [G_TXSRC] = PE("SRC", N_TXSRC, 0),            /* 2.6, GLO > TEXTURE: the bed under the mix (OFF: none) */
    [G_TXLVL] = PD("LEVEL", F_PCT, 0, 127, 64),
    [G_TXTONE] = PD("TONE", F_FILT, -64, 63, 0),  /* left a low-pass, right a high-pass, centre open */
    [G_TXMOVE] = PD("MOVE", F_PCT, 0, 127, 40),   /* how much it breathes (level and colour), over RATE */
    [G_TXRATE] = PE("RATE", N_TXRATE, 2),
    [G_TXWHEN] = PE("WHEN", N_TXWHEN, 0),         /* PLAY: while the transport runs (fades in / out); ALWAYS */
    [G_TXDUCK] = PE("DUCK", N_ONOFF, 0),          /* ON: the kick pumps it too (GLO > MASTER > DUCK) */
    [G_CLSEND] = PD("SEND", F_PCT, 0, 127, 0),   /* FX > CLOUD (cloud.c) */
    [G_CLPOS] = PD("POS", F_PCT, 0, 127, 24),
    [G_CLSIZE] = PD("SIZE", F_PCT, 0, 127, 64),
    [G_CLPIT] = PD("PITCH", F_SEMI, -24, 24, 0),
    [G_CLDENS] = PD("DENS", F_PCT, 0, 127, 104),
    [G_CLTEX] = PD("TEXT", F_PCT, 0, 127, 90),
    [G_CLFB] = PD("FDBK", F_PCT, 0, 127, 0),
    [G_CLFRZ] = PE("FRZ", N_ONOFF, 0),
    [G_AGDENS] = PD("DENS", F_PCT, 0, 127, 89),   /* SEQ > ACID GEN (acid_gen.c): TB-3PO's 70 / 40 / 25 % */
    [G_AGACC] = PD("ACC", F_PCT, 0, 127, 51),
    [G_AGSLD] = PD("SLIDE", F_PCT, 0, 127, 32),
    [G_AGSEED] = PD("SEED", F_INT, 0, 999, 0),
    [G_AGSCALE] = PE("SCALE", N_AGSCALE, 0),
    [G_AGOCT] = PD("OCT", F_INT, 1, 3, 2),
    [G_AGROOT] = PD("ROOT", F_NOTE, 0, 11, 0),   /* (C, as the tracks: SEQ > HARMONY sets it with them) */
    [G_AGMUT] = PE("VARY", N_GO, 0),
    [G_HXMODEL] = PE("MODEL", N_HXMODEL, 0),     /* EDIT on the drum track: the HEX sound (ui_studio.c hex_ui_sync); */
    [G_HXWAVE] = PE("WAVE", N_HXWAVE, 0),        /* the LXR's 0..127 values. These are the DRUM model's: the pages */
    [G_HXTUNE] = PD("TUNE", F_INT, 0, 127, 36),  /* show each model's own (hex_desc) */
    [G_HXFINE] = PD("P.SLP", F_INT, -64, 62, 0),
    [G_HXATK] = PD("ATK", F_INT, 0, 127, 0),
    [G_HXDEC] = PD("DEC", F_INT, 0, 127, 30),
    [G_HXSLOPE] = PD("SLOPE", F_INT, 0, 127, 40),
    [G_HXLVL] = PD("LEVEL", F_INT, 0, 127, 110),
    [G_HXPAMT] = PD("P.AMT", F_INT, 0, 127, 0),
    [G_HXPDEC] = PD("P.DEC", F_INT, 0, 127, 30),
    [G_HXRATIO] = PD("M.PIT", F_INT, 0, 127, 48),
    [G_HXFM] = PD("FM", F_INT, 0, 127, 0),
    [G_HXCLICK] = PE("TRANS", N_HXCLICK, 0),
    [G_HXCLVL] = PD("T.LVL", F_INT, 0, 127, 60),
    [G_HXCTONE] = PD("T.PIT", F_INT, 0, 127, 25),
    [G_HXFTYPE] = PE("FILT", N_HXFILT, 0),
    [G_HXCUT] = PD("CUT", F_INT, 0, 127, 110),
    [G_HXRES] = PD("RES", F_INT, 0, 127, 0),
    [G_HXDRIVE] = PD("DRIVE", F_INT, 0, 127, 40),
    [G_HXAUX1] = PD("MOD", F_INT, 0, 127, 0),
    [G_HXAUX2] = PD("DIST", F_INT, 0, 127, 0),
    [G_HXCRUSH] = PD("CRUSH", F_INT, 0, 126, 0),
    [G_GNON] = PE("GEN", N_ONOFF, 0),                /* SEQ > GEN (gen_seq.c) */
    [G_GNCHG] = PD("DRIFT", F_PCT, 0, 127, 32),
    [G_GNDENS] = PD("DENS", F_PCT, 0, 127, 45),   /* (about a third of the steps: 89 started too busy) */
    [G_GNRANGE] = PD("RANGE", F_INT, 1, 3, 2),
    [G_GNOCT] = PD("OCT", F_INT, 1, 5, 3),
    [G_GNACC] = PD("ACC", F_PCT, 0, 127, 25),
    [G_GNSLD] = PD("SLIDE", F_PCT, 0, 127, 13),
    [G_GNNEW] = PE("NEW", N_GO, 0),
    [G_GKROOT] = PD("KEY", F_NOTE, 0, 11, 0),         /* SEQ > HARMONY (gen_seq.c) */
    [G_GKMODE] = PE("MODE", N_GKMODE, 0),
    [G_GKPROG] = PE("PROG", N_GKPROG, 1),
    [G_GKBARS] = PE("BARS", N_GKBARS, 1),
    [G_VRGHO] = PD("GHOST", F_PCT, 0, 127, 127),    /* SEQ > VARY (seq.c vr_*): the selected track's chance that a GHOST hit plays, */
    [G_VRSFT] = PD("SOFT", F_PCT, 0, 127, 127),     /* a SOFT one, a HARD one (a step's own chance is on the STEP layer), */
    [G_VRHRD] = PD("HARD", F_PCT, 0, 127, 127),
    [G_VRAMT] = PD("AMOUNT", F_PCT, 0, 127, 127),   /* how much of all of it is heard: 0 = every hit plays (a tight groove) */
    [G_VRVEL] = PD("VEL RND", F_INT, 0, 40, 0),     /* a hit's velocity moves by up to +- this */
    [G_VRRAT] = PD("RATCHET", F_PCT, 0, 127, 127),  /* the chance each further ratchet hit plays */
    [G_VRLOOK] = PE("LOOK", N_VRLOOK, 0),           /* LIVE: new every pass. HOLD: the same every pass, for this SEED */
    [G_VRSEED] = PD("SEED", F_INT, 0, 99, 0),
    [G_DGMODE] = PE("MODE", N_DGMODE, 0),           /* SEQ > DRUM GEN (gen_seq.c) */
    [G_DGX] = PD("MAP X", F_INT, 0, 127, 64),
    [G_DGY] = PD("MAP Y", F_INT, 0, 127, 64),
    [G_DGCHAOS] = PD("CHAOS", F_PCT, 0, 127, 0),
    [G_DGK] = PD("KICK", F_PCT, 0, 127, 80),
    [G_DGS] = PD("SNARE", F_PCT, 0, 127, 64),
    [G_DGH] = PD("HATS", F_PCT, 0, 127, 80),
    [G_DGROT] = PD("ROT", F_INT, 0, 15, 4),
    [G_PLDIR] = PE("DIR", N_PLDIR, 0),              /* SEQ > PLAY (seq.c pl_*): the selected track's */
    [G_PLCHN] = PD("CHANCE", F_PCT, 0, 127, 64),
    [G_PLJMP] = PD("JUMP", F_PCT, 0, 127, 0),
    [G_PLSTR] = PD("STRIDE", F_INT, 1, 7, 1),
    [G_KNOBS] = PE("KNOBS", N_KNOBS, 4),            /* a controller's 8 knobs (CC 1-8 or 70-77: the Akai MPK minis):
                                                     * PANEL the FM-1's eight rotaries (PRESETS SELECT ALGORITHM MASTER,
                                                     * KNOB 1..4); 70-77, 1-8, AUTO: EDIT's two pages; GM: the GM map */
    [G_BEND] = PD("BEND", F_SEMI, 1, 12, 2),      /* 2.6, GLO > MIDI 2: MIDI pitch bend's range (a setting of the FM-1) */
};

static const param_desc_t DRUM_KIT_DESC = PE("KIT", DRUM_KIT_NAMES, 0);
/* the INSERT's A B C as its TYPE means them (TP's ranges and defaults): the drives DRIVE TONE LEVEL, CRUSH BITS RATE
 * LPF, the swept ones RATE DEPTH FDBK; OFF TP's own */
#define PU(l, f, df, n, u) {l, f, 0, 127, df, n, u}
static const param_desc_t INS_DESC[3][3] = {
    {PU("DRIVE", F_PCT, 64, 0, 0), PU("TONE", F_CUTOFF, 96, 0, 0), PU("LEVEL", F_DB, 96, 0, 0)},
    {PU("BITS", F_INT, 64, N_IBITS, 0), PU("RATE", F_INT, 96, N_IRATE, "Hz"), PU("LPF", F_CUTOFF, 96, 0, 0)},
    {PU("RATE", F_LFOHZ, 64, 0, 0), PU("DEPTH", F_PCT, 96, 0, 0), PU("FDBK", F_PCT, 96, 0, 0)},
};
#undef PU
static const param_desc_t *ins_desc(int32_t type, uint32_t k)
{
    if (type <= 0 || type >= (int32_t)NELEM(N_ITYPE))
        return &TP[P_IA + k];
    return &INS_DESC[type < 5 ? 0 : type == 5 ? 1 : 2][k % 3u];   /* (SOFT .. FUZZ, CRUSH, the swept ones) */
}
static const param_desc_t ESYNC_DESC[3] = {   /* ATK DEC REL with ENV SYNC on: the range and defaults as ever */
    {"ATK", F_INT, 0, 127, 10, ESYNC_NAMES, 0},
    {"DEC", F_INT, 0, 127, 70, ESYNC_NAMES, 0},
    {"REL", F_INT, 0, 127, 60, ESYNC_NAMES, 0},
};

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if(is_drum(t) && id==P_E0) return &DRUM_KIT_DESC;
    if (id >= P_IA && id <= P_IC)                     /* the INSERT's values as its TYPE names them */
        return ins_desc(t->p[P_ITYPE], id - P_IA);
    if (t->p[P_ESYNC] && (id == P_ATK || id == P_DEC || id == P_REL))   /* ENV SYNC: note values */
        return &ESYNC_DESC[id == P_ATK ? 0 : id == P_DEC ? 1 : 2];
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

/* a knob turned `steps` on an F_INT with a 0-terminated name list (ENV SYNC's note values, the INSERT's BITS and
 * RATE): a name a detent, landing on the first value that shows it (2.6, as Felucca 1.5's ENV SYNC) */
static int32_t names_turn(const param_desc_t *d, int32_t v, int32_t steps)
{
    int32_t n = 0, range = d->max - d->min + 1, k;
    while (d->names[n])
        n++;
    k = clamp((clamp(v, d->min, d->max) - d->min) * n / range + steps, 0, n - 1);
    return d->min + (k * range + n - 1) / n;
}

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    switch (d->fmt) {
    case F_PCT:                                       /* of the range: 0 .. 100 % */
        fmt_int(val, d->max > 0 ? (v * 100 + d->max / 2) / d->max : v);
        *unit = "%";
        break;
    case F_SWING:                                     /* 0 straight .. 100 the most (2.5: it showed MPC's 50..75 %,
                                                       * the share of a step pair the first one gets: confusing) */
        fmt_int(val, clamp(v, 0, 100));
        break;
    case F_FILT:                                      /* LP 1..100 % closed, HP 1..100 % */
        if (!v) {
            str_cpy(val, "OFF", 6);
        } else {
            str_cpy(val, v < 0 ? "LP" : "HP", 6);
            fmt_int(val + 2, v < 0 ? (-v * 100 + 32) / 64 : (v * 100 + 31) / 63);
            *unit = "%";
        }
        break;
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else if (h < 10000u) {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        } else {
            fmt_int(val, (int32_t)((h + 500u) / 1000u));   /* "12 kHz": "12.5" would leave no room for the unit */
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else if (LEVEL_DB_X10[v] > -100) {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        } else {
            fmt_int(val, (LEVEL_DB_X10[v] - 5) / 10);   /* "-12 dB": five characters leave no room for the unit */
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK, SC_SONG, SC_DRUM };
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_INS, GR_HEX, GR_DGEN };

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ESYNC}},   /* (P_ED_FX: the level trim, no
                                                                       * page); 2.6: ESYNC, ENV's times as note values */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO 2", FAM_LFO, SC_TRACK, GR_LFO, {P_LSYNC, P_LTRIG, P_LPOL, 0xFF}},   /* 2.6 (Felucca 1.4): SYNC TRIG POL */
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"FILTER", FAM_FX, SC_TRACK, GR_NONE, {P_TFLT, 0xFF, 0xFF, 0xFF}},   /* the track's filter (drum track too) */
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},   /* drum track too */
    {"INSERT", FAM_FX, SC_TRACK, GR_INS, {P_ITYPE, P_IA, P_IB, P_IC}},   /* 2.6 (Felucca 1.5): the track's INSERT (fx.c), */
    {"INSERT 2", FAM_FX, SC_TRACK, GR_INS, {P_IMIX, 0xFF, 0xFF, 0xFF}},  /* .. its dry / wet; the drum track too */
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
    {"CLOUD", FAM_FX, SC_GLOBAL, GR_NONE, {G_CLSEND, G_CLPOS, G_CLSIZE, G_CLPIT}},     /* Clouds' grains (cloud.c): the */
    {"CLOUD 2", FAM_FX, SC_GLOBAL, GR_NONE, {G_CLDENS, G_CLTEX, G_CLFB, G_CLFRZ}},   /* track's send, then the cloud's own */
    {"SEL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_QUANT, P_CHORD}},
    {"SEL 2", FAM_SCL, SC_TRACK, GR_SCALE, {P_TRANS, P_STRUM, P_VLEAD, 0xFF}},   /* (2.4: the chords played) */
    {"HEX OSC", FAM_EDIT, SC_GLOBAL, GR_HEX, {G_HXMODEL, G_HXWAVE, G_HXTUNE, G_HXFINE}},   /* the drum track's EDIT: */
    {"HEX AEG", FAM_EDIT, SC_GLOBAL, GR_HEX, {G_HXATK, G_HXDEC, G_HXSLOPE, G_HXLVL}},       /* the selected sound of */
    {"HEX MOD", FAM_EDIT, SC_GLOBAL, GR_HEX, {G_HXPAMT, G_HXPDEC, G_HXRATIO, G_HXFM}},      /* its HEX kit (page_shown: */
    {"HEX CLICK", FAM_EDIT, SC_GLOBAL, GR_HEX, {G_HXCLICK, G_HXCLVL, G_HXCTONE, G_HXCRUSH}}, /* the drum track only) */
    {"HEX FILT", FAM_EDIT, SC_GLOBAL, GR_HEX, {G_HXFTYPE, G_HXCUT, G_HXRES, G_HXDRIVE}},
    {"HEX MORE", FAM_EDIT, SC_GLOBAL, GR_HEX, {G_HXAUX1, G_HXAUX2, 0xFF, 0xFF}},
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
    {"FILTER", FAM_EDIT, SC_TRACK, GR_NONE, {P_FTYPE, P_E4, P_E5, P_E7}},   /* ANALOG only (2.6, Felucca 1.5): TYPE CUT
                                                                          * RES KTR (page_shown) */
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_TUNE}},
    {"MASTER", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DUST, G_DUCK, G_FILT, G_ROLL}},
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_SYNC, G_ROUTE, G_INFO}},
    {"DRUMS", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DRCH, G_DRLVL, G_DRREV, G_DRDLY}},   /* GM kit on MIDI ch 10 */
    {"MIDI", FAM_GLO, SC_GLOBAL, GR_NONE, {G_CH1, G_CH2, G_CH3, G_DRCH}},   /* 2.6: each track's channel, the drums' too */
    {"MIDI 2", FAM_GLO, SC_GLOBAL, GR_NONE, {G_ROUTE, G_CHOTH, G_MIDI, G_BEND}},   /* 2.6: IN, OTHER, OUT, BEND: every MIDI
                                                                           * setting in these two pages (IN and OUT on
                                                                           * SYSTEM too, the drums' channel on DRUMS) */
    {"MIDI 3", FAM_GLO, SC_GLOBAL, GR_NONE, {G_KNOBS, 0xFF, 0xFF, 0xFF}},   /* a controller's knobs (2026-10-20) */
    {"TEXTURE", FAM_GLO, SC_GLOBAL, GR_NONE, {G_TXSRC, G_TXLVL, G_TXTONE, G_TXMOVE}},   /* 2.6: the bed under the mix */
    {"TEXTURE 2", FAM_GLO, SC_GLOBAL, GR_NONE, {G_TXRATE, G_TXWHEN, G_TXDUCK, 0xFF}},
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: PRESETS knob / KNOB 1 */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},   /* (KNOB 2: ERASE, 2.6: ui_input.c) */
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_NONE, {G_CLRSEQ, G_INITSND, 0xFF, G_NEWPRJ}},
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_NONE, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
    {"STEP", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},
    {"PLAY", FAM_SEQ, SC_GLOBAL, GR_NONE, {G_PLDIR, G_PLCHN, G_PLJMP, G_PLSTR}},   /* the order steps play in, chance */
    {"VARY", FAM_SEQ, SC_GLOBAL, GR_NONE, {G_VRGHO, G_VRSFT, G_VRHRD, G_VRAMT}},   /* chance by level; how much of it */
    {"VARY 2", FAM_SEQ, SC_GLOBAL, GR_NONE, {G_VRVEL, G_VRRAT, G_VRLOOK, G_VRSEED}},   /* velocity, ratchets, hold a look */
    {"ACID GEN", FAM_SEQ, SC_GLOBAL, GR_NONE, {G_AGDENS, G_AGACC, G_AGSLD, G_AGSEED}},     /* TB-3PO's lines into the */
    {"ACID GEN 2", FAM_SEQ, SC_GLOBAL, GR_NONE, {G_AGSCALE, G_AGOCT, G_AGROOT, G_AGMUT}},  /* track (acid_gen.c) */
    {"GEN", FAM_SEQ, SC_GLOBAL, GR_ROLL, {G_GNON, G_GNCHG, G_GNDENS, G_GNRANGE}},   /* a loop that changes as */
    {"GEN 2", FAM_SEQ, SC_GLOBAL, GR_ROLL, {G_GNOCT, G_GNACC, G_GNSLD, G_GNNEW}},    /* it plays (gen_seq.c) */
    {"HARMONY", FAM_SEQ, SC_GLOBAL, GR_ROLL, {G_GKROOT, G_GKMODE, G_GKPROG, G_GKBARS}},   /* the key, the chords */
    {"DRUM GEN", FAM_SEQ, SC_GLOBAL, GR_DGEN, {G_DGMODE, G_DGX, G_DGY, G_DGCHAOS}},   /* the drum track's */
    {"DRUM GEN 2", FAM_SEQ, SC_GLOBAL, GR_DGEN, {G_DGK, G_DGS, G_DGH, G_DGROT}},     /* KICK SNARE HAT */
    {"SONG", FAM_SEQ, SC_SONG, GR_NONE, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"TRACKS", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},   /* REC button; TRACK LEVEL LEN PAN */
    {"DRUMS", FAM_TRK, SC_DRUM, GR_NONE, {0xFF,0xFF,0xFF,0xFF}},
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))

/* the drum track has no sound of its own: it uses the global pages (not the preset
 * pages, nor TOOLS > INIT: page_desc), STEP, PATTERN, SLICER and TRACKS; every other page
 * shows "DRUM TRACK" */
static int page_for_drum(const page_t *pg)
{
    if (pg->scope == SC_STEP)                       /* (2.4) the synth steps' roll: the drums have their grid. Reached */
        return 0;                                   /* by ALGO from a synth track, it drew the drum steps as notes */
    if (pg->scope == SC_GLOBAL)
        return pg->graph != GR_BROWSE && pg->graph != GR_USER;
    return pg->scope != SC_ENGINE && (pg->scope != SC_TRACK || pg->fam == FAM_SEQ || pg->graph == GR_SLCR ||
                                      pg->graph == GR_INS);
}
/* a page the buttons and SELECT reach on the selected track: EDIT > FILTER (TYPE) on ANALOG only */
static int page_shown(const page_t *pg)
{
    if (pg->graph == GR_HEX)                         /* the HEX sound pages: the drum track's EDIT, and only its */
        return is_drum(TSEL);
    if (pg->fam == FAM_EDIT && is_drum(TSEL))
        return 0;
    if (pg->scope == SC_GLOBAL && (pg->id[0] == G_GNON || pg->id[0] == G_GNOCT || pg->id[0] == G_GKROOT || pg->id[0] == G_AGDENS ||
                                   pg->id[0] == G_AGSCALE))
        return !is_drum(TSEL);                       /* GEN, ACID GEN: the synth tracks' */
    if (pg->graph == GR_DGEN)
        return is_drum(TSEL);                        /* DRUM GEN: the drum track's */
    return pg->id[0] != P_FTYPE || (!is_drum(TSEL) && TSEL->eng_req % NENGINES == 0u);
}

/* a HEX sound's byte as its model reads it (hex_drums.c): what differs from GP's (the DRUM model's) */
static const param_desc_t *hex_desc(uint32_t id)
{
    static const param_desc_t SN[] = {PD("P.SLP", F_INT, -64, 62, 0), PD("P.AMT", F_INT, 0, 127, 0),
                                      PD("P.DEC", F_INT, 0, 127, 30), PD("N.FRQ", F_INT, 0, 127, 120),
                                      PD("MIX", F_INT, 0, 127, 90), PD("REPT", F_INT, 0, 127, 0)};
    static const param_desc_t CY[] = {PD("FINE", F_INT, -64, 62, 0), PD("MOD2", F_INT, 0, 127, 60),
                                      PD("PIT2", F_INT, 0, 127, 120), PD("PIT1", F_INT, 0, 127, 90),
                                      PD("MOD1", F_INT, 0, 127, 60), PE("WAVES", N_HXWAV2, 22)};
    static const param_desc_t AUX_DRUM = PE("MOD", N_HXMODW, 0);
    static const uint8_t AT[22] = {9, 9, 9, 0, 9, 9, 9, 9, 1, 2, 3, 4, 9, 9, 9, 9, 9, 9, 9, 5, 9, 9};
    uint32_t m = (uint32_t)song.g[G_HXMODEL], i = id - G_HXMODEL, a = i < 22u ? AT[i] : 9u;
    if (a == 9u)
        return &GP[id];
    if (m == HM_DRUM)
        return a == 5u ? &AUX_DRUM : &GP[id];
    return m <= HM_CLAP ? &SN[a] : &CY[a];
}
static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot];
    if (id == 0xFFu || (is_drum(TSEL) && (!page_for_drum(pg) || (pg->scope == SC_GLOBAL && id == G_INITSND)))) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return pg->graph == GR_HEX ? hex_desc(id) : &GP[id];
    }
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
