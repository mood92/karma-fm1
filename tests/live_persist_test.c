/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the LIVE setlist's flash persistence (REVIEW-PLAN.md 0.4) and its STORE/RECALL semantics
 * (1.2): the real panel.c + ui.c + storage.c + upreset.c + project.c (PROJ_HOST off, so the genuine
 * settings_save()/dsu_boot() flash path runs, on a simulated NOR flash). Covers: two overridden slots on
 * two different lanes survive a settings_save() + dsu_boot() round trip; an untouched slot still reads as
 * setlist_default()'s own value; an old-format (1 byte/slot, before 1.1) settings object is not misread
 * as the new (id, lane) format; STORE snapshots a lane's live tweaks into that slot's own reserved user
 * preset slot (not just a bare preset reference) and RECALL restores them exactly; and a manual SAVE /
 * ERASE on a slot reserved for the setlist is refused. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ uint32_t i, j; assert(x+w<=240 && y+h<=240); for(j=0;j<h;j++) for(i=0;i<w;i++) screen[(y+j)*240+x+i]=p[j*w+i]; }
#include "../firmware/src/gfx.c"
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c)
{ uint32_t i,j; assert(x+w<=240&&y+h<=240); for(j=0;j<h;j++)for(i=0;i<w;i++)screen[(y+j)*240+x+i]=swap16(c); }
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
static uint8_t fm1_led[16], fm1_led_dim[16], fm1_led_bg[16];
static volatile uint16_t fm1_led_bg_ns;
#ifndef FM1_NCOL
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
#endif
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static uint32_t wdt_tick_ms;
static void fm1_wdt_feed(void) { fm1_ms += wdt_tick_ms; }
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define FELUCCA_ICONS 1
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];
static uint32_t scope_w;
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"

/* --- flash sim for storage.c / upreset.c --- */
static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#define FELUCCA_FLASH 1
static uint8_t flash_ok;
#define FL_FAR(x) x
static uint32_t jedec = 0x856014u;
static uint32_t fl_jedec_ram(void) { return jedec; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static void fl_plain_window_init(void) {}
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static int fails;
static void check(int ok, const char *what) { printf("live persist: %-60s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

int main(void)
{
    uint32_t k, id0, lane5, id5, def_ok;

    flash_ok = 1;
    panel = PANEL_DEFAULT;
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();

    /* as felucca_init leaves it before any STORE: every slot at its setlist_default(), then two slots
     * (on two different lanes) overridden, so a round trip that silently fell back to defaults would
     * be caught */
    setlist_default();
    setlist_id[0] = (uint8_t)pl_entry(&trk[1], 2);
    setlist_lane[0] = 1;
    id0 = setlist_id[0];
    setlist_id[5] = (uint8_t)pl_entry(&trk[2], 0);
    setlist_lane[5] = 2;
    id5 = setlist_id[5];
    lane5 = setlist_lane[5];
    setlist_ready = 1;

    settings_save();                               /* PROJECT.C: persist_fill + settings_write -> flash */

    /* "reboot": dsu_boot() resets the setlist to setlist_default() first, then should overwrite it from
     * the flash object it just wrote -- a stale RAM copy proves nothing */
    setlist_id[0] = 0;
    setlist_lane[0] = 0;
    setlist_id[5] = 0;
    setlist_lane[5] = 0;
    dsu_boot();

    check(setlist_id[0] == id0 && setlist_lane[0] == 1, "slot 0 (lane 2, overridden) survives settings_save()+dsu_boot()");
    check(setlist_id[5] == id5 && setlist_lane[5] == lane5, "slot 5 (lane 3, overridden) survives settings_save()+dsu_boot()");
    {
        uint32_t lane = 1u % NPART;                /* slot 1: never touched above, should still be the plain default */
        check(setlist_lane[1] == (uint8_t)lane && setlist_id[1] == (uint8_t)pl_entry(&trk[lane], 1u / NPART),
              "slot 1 (never overridden) is still setlist_default()'s own value");
    }

    /* an old-format object (1 byte/slot, SLOOP: before 1.1) must not be reinterpreted as the new one
     * (2 bytes/slot): dsu_boot() should fall back to setlist_default() instead of reading garbage */
    {
        uint8_t mp[SET_PART];
        midi_part_fill(mp);                        /* real channel bytes, then the (new-format) setlist bytes */
        check(st_save3(OBJ_SETTINGS, &persist_saved, sizeof persist_saved, &dsu, sizeof dsu, mp,
                        MIDI_PART + LIVE_PART_OLD) == 0,
              "(setup) an old-length (MIDI_PART + LIVE_PART_OLD) settings object writes");
    }
    setlist_id[0] = 0xAAu;
    setlist_lane[0] = 0xAAu;
    dsu_boot();
    def_ok = 1;
    for (k = 0; k < SETLIST_N; k++) {
        uint32_t lane = k % NPART;
        if (setlist_lane[k] != (uint8_t)lane || setlist_id[k] != (uint8_t)pl_entry(&trk[lane], k / NPART))
            def_ok = 0;
    }
    check(def_ok, "an old-format (1 byte/slot) object is not reinterpreted: setlist falls back to setlist_default()");

    {   /* REVIEW-PLAN.md 1.2: STORE keeps the lane's live tweaks (not just a preset reference), by
         * snapshotting them into the setlist slot's own reserved user preset (upreset.c) */
        uint32_t s2 = 2, up_k = LIVE_UP_BASE + s2;
        track_select(0);
        trk[0].eng_req = ENGI_ACID;
        apply_preset_to(&trk[0], 0);
        trk[0].p[P_DIST] = 77;                      /* a tweak param_kept() does not protect: must round-trip */
        live_store = 1;
        live_key(s2);
        check(!live_store && setlist_id[s2] == (uint8_t)(NBANK + up_k) && setlist_lane[s2] == 0 &&
              up_used(up_k) && up_rec(up_k)->p[P_DIST] == 77,
              "STORE snapshots the lane's tweaked sound into its slot's own reserved user preset");

        trk[0].p[P_DIST] = 0;                       /* a different sound now on the lane */
        live_key(s2);                                /* recall (live_store is 0 now) */
        check(trk[0].p[P_DIST] == 77, "RECALL restores the stored tweak exactly (not just the bare preset)");
    }
    {   /* 1.2: a manual SAVE / ERASE on a slot reserved for the setlist is refused, not allowed to pull
         * the sound out from under it */
        uint32_t up_k = LIVE_UP_BASE + 2u;
        up_rec_t before = *up_rec(up_k);
        ui.msg[0] = 0;
        up_ui(2u, up_k);                             /* 2 = save */
        check(!strcmp(ui.msg, "RESERVED FOR LIVE") && !memcmp(up_rec(up_k), &before, sizeof before),
              "a manual SAVE on a slot reserved for the LIVE setlist is refused, the slot unchanged");
        ui.msg[0] = 0;
        up_ui(1u, up_k);                             /* 1 = erase */
        check(!strcmp(ui.msg, "RESERVED FOR LIVE") && !memcmp(up_rec(up_k), &before, sizeof before),
              "  same for ERASE");
    }

    printf(fails ? "LIVE PERSIST: %d FAILED\n" : "live persist: all ok\n", fails);
    return fails != 0;
}
