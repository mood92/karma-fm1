/* SPDX-License-Identifier: GPL-3.0-only */
/* MAIN MENU: screenshots of the four screens (LIVE, SOUND, BROWSE, SETLIST) and SOUND's knob pages, driven
 * through the real buttons, for review: DIR/shot-*.ppm. Uses the ui_pages_test harness.
 *   cc ... -o build/host/shots_test tests/shots_test.c ...; build/host/shots_test DIR */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"

static void boot(void)
{
    uint32_t i;
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    bank_resolve();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    go_home();
    for (i = 0; i < NPAGES; i++) if (PAGES[i].graph == GR_BROWSE) { ui.home = 0; ui.page = (uint8_t)i; ui.fam_last[FAM_SAVE] = (uint8_t)i; break; }
    ui.force = 1; frames(3);
}
static void shot(const char *name) { ui.force = 1; frames(2); ppm(name); }
static void hold(uint32_t b, uint32_t n) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frames(n); }
static void let_go(uint32_t b) { fm1_in.buttons &= ~BT(b); frames(2); }

int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : "build/host";
    boot();
    shot("shot-00-boot");
    tap(B_HOME); shot("shot-01-home");
    tap(B_HOME); shot("shot-01b-home-again");
    tap(B_SAVE); shot("shot-02-save");
    tap(B_SEQ); shot("shot-03-seq");
    tap(B_LFO); shot("shot-04-lfo-tap");
    hold(B_LFO, 4); ppm("shot-05-lfo-held"); let_go(B_LFO);
    tap(B_ENV); shot("shot-06-env");
    tap(B_FX); shot("shot-07-fx");
    tap(B_EDIT); shot("shot-08-edit");
    tap(B_ARP); shot("shot-09-arp");
    tap(B_SCL); shot("shot-10-sel");
    tap(B_GLO); shot("shot-11-glo");
    key(10); shot("shot-12-after-key");
    go_home(); key(14); shot("shot-13-sound-note");
    tap(B_SEQ); encs[panel.enc[EN_K1]] = 3; frames(2); shot("shot-14-setlist-k1");
    encs[panel.enc[EN_K2]] = 1; frames(2); shot("shot-15-setlist-move");
    encs[panel.enc[EN_K4]] = 1; frames(2); shot("shot-16-setlist-clear-arm");
    encs[panel.enc[EN_K4]] = 1; frames(2); shot("shot-17-setlist-cleared");
    tap(B_SAVE); encs[panel.enc[EN_K1]] = 5; frames(2); shot("shot-18-browse-scroll");
    encs[panel.enc[EN_K4]] = 1; frames(2); shot("shot-19-browse-set");
    tap(B_SEQ); shot("shot-20-setlist-after-set");
    tap(B_LFO); shot("shot-21-live");
    printf("shots: %s\n", outdir);
    return 0;
}
