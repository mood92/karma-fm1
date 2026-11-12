/* SPDX-License-Identifier: GPL-3.0-only */
/* ELEMENTS: the exciters of Elements (Emilie Gillet, MIT: strike_dsp.c, bow_dsp.c) on PHYS's modal bank, as an
 * engine of their own. The code is eng_phys.c's (its models from PM_ELEM on; they are described there); this
 * file is the engine's face: its MODEL list, its presets, its name.
 *   BOUNC   hit again and again while the key is held (LAST: how long the bounces go on)
 *   PICK    plucked by her plectrum (GAP: the time between the catch and the pluck)
 *   BOW     bowed: sings while the key is held (BOW: the pressure, TONE: the bow's hiss, ACC: how rough); one note
 *   BLOW    blown: breath, and above 2 / 3 of BLOW her reed pipe (TONE: the breath's colour); one note
 * STRC BRIT DAMP POS ACC as PHYS MODAL. (The name is the one Ariel wants on his build: Mutable Instruments'
 * guidelines ask derivatives not to use the module names, so rename it before any public release.) */
enum { EM_BOUNCE, EM_PICK, EM_BOW, EM_BLOW, EM_COUNT };
_Static_assert(EM_COUNT == PM_COUNT - PM_ELEM, "ELEMENTS' models are PHYS's from PM_ELEM on");
static const char *const N_ELEM_MODEL[EM_COUNT] = {"BOUNC", "PICK", "BOW", "BLOW"};

static void elem_note_on(track_t *t, voice_t *v)
{
    phys_note_on_e(t, v, 1);
}
static void elem_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    phys_render_e(t, v, out, n, m, 1);
}

/* (2026-10-28: OMIN played 11.1 semitones sharp before; OMINOUS and OMEN BELL +12, DARK DRONE 0 keep their register,
 * in tune now) */
static const preset_t ELEM_PRESETS[] = {
    /* name, {MODEL, STRC, BRIT, DAMP, POS, ACC, .., ..}, {A D S R}, fenv, mono */
    /* Rhodes Juno Sketch reference, LEADS bank: flute-tagged entries -> EM_BLOW (breath/blown character) */
    {"FLUTE LEAD", {EM_BLOW, 40, 75, 85, 14, 80, 100, 100}, {15, 80, 127, 50}, 0, 1, FX(0, 10, 14, 40), XP(P_GLIDE + 1, 15)},
    {"WHISTLE", {EM_BLOW, 20, 70, 70, 8, 50, 60, 100}, {5, 60, 127, 30}, 0, 1, FX(0, 8, 10, 30)},
    {"SINE GLIDE", {EM_BLOW, 10, 30, 110, 20, 90, 110, 20}, {20, 90, 127, 60}, 0, 1, FX(0, 5, 12, 35), XP(P_GLIDE + 1, 35)},
};

static const engine_t ENG_ELEM = {
    .name = "ELEMENTS",
    .page_title = {"BODY", "EXCT"},
    .edit = {
        {"MODEL", F_ENUM, 0, EM_COUNT - 1, 0, N_ELEM_MODEL, 0},
        {"STRC", F_PCT, 0, 127, 64, 0, 0},
        {"BRIT", F_PCT, 0, 127, 80, 0, 0},
        {"DAMP", F_PCT, 0, 127, 80, 0, 0},
        {"POS", F_PCT, 0, 127, 20, 0, 0},
        {"ACC", F_PCT, 0, 127, 90, 0, 0},
        {"BOW", F_PCT, 0, 127, 100, 0, 0},
        {"EXC", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = ELEM_PRESETS,
    .npresets = NELEM(ELEM_PRESETS),
    .note_on = elem_note_on,
    .render = elem_render,
    .fil_page = -1,
    .color = 0xFD8F,
    .macro = {P_E1, P_E2, P_E3, P_E4},
    .poly = PHYS_POLY,
    .desc = elem_desc,
};
