/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "../firmware/src/arranger.h"
static uint32_t loop_bars[4] = {2, 1, 3, 1};
static uint32_t arr_scene_bars(uint32_t scene) { return loop_bars[scene & 3u]; }   /* (a LOOP entry: the section's length) */

int main(void)
{
    arr_config_t c;
    arr_clock_t r;
    unsigned long samples;
    unsigned transitions = 0;
    int event;
    arr_defaults(&c);
    assert(!arr_valid(&c, 7));                 /* missing drum/scene slot cannot silently play */
    assert(arr_begin(&r, &c, 7) == ARR_INVALID && !r.running);
    c.count = 3;
    c.entry[0] = (arr_entry_t){0, 1};
    c.entry[1] = (arr_entry_t){2, 2};
    c.entry[2] = (arr_entry_t){0, 1};          /* repeated scenes are valid */
    assert(arr_begin(&r, &c, 5) == 0);
    /* 137 BPM has a fractional samples-per-bar: transitions must never drift
     * by more than one 32-sample audio block, even over multiple sections. */
    for (samples = 0; r.running; samples += 32) {
        event = arr_next(&r, &c, 44118);
        if (event != ARR_NONE) {
            unsigned bars = transitions == 0 ? 1 : transitions == 1 ? 3 : 4;
            double expected = 44118.0 * 240.0 * bars / 137.0;
            assert(samples >= expected && samples - expected < 32.0);
            assert(event == (transitions == 0 ? 2 : transitions == 1 ? 0 : ARR_DONE));
            transitions++;
        }
        arr_elapse(&r, 32, 137);
    }
    assert(transitions == 3);
    c.count = 1; c.entry[0].bars = 1; c.loop = 1;
    assert(arr_begin(&r, &c, 1) == 0);
    arr_elapse(&r, 44118, 120);                /* half bar at 120 BPM */
    assert(arr_next(&r, &c, 44118) == ARR_NONE);
    arr_elapse(&r, 88236, 60);                 /* tempo change preserves musical position */
    assert(arr_next(&r, &c, 44118) == 0 && r.running);
    c.entry[0].bars = 0; assert(arr_valid(&c, 15));   /* KARMA 3.0: 0 = LOOP (the section's own length) */
    c.entry[0].bars = 65; assert(!arr_valid(&c, 15));
    c.entry[0].bars = 1; c.entry[0].scene = 64; assert(!arr_valid(&c, 15));
    c.entry[0].scene = (uint8_t)(2u | (5u << 2)); assert(arr_valid(&c, 15));   /* SEQ-SONG: section C, tracks 0 and 2 muted */
    assert(ARR_SCN(c.entry[0].scene) == 2u && ARR_MUTE(c.entry[0].scene) == 5u);
    assert(arr_begin(&r, &c, 15) == 2);               /* the scene reported is the section only */
    assert(!arr_valid(&c, 3));                         /* section C not ready: still invalid with mutes */
    c.entry[0].scene = 0; c.count = 17; assert(!arr_valid(&c, 15));
    {   /* 2.6: a section erased (SAVE > PROJECT > ERASE): the song's steps that played it out, the rest in order */
        arr_config_t d;
        arr_defaults(&d);
        d.count = 5;
        d.entry[0] = (arr_entry_t){0, 2}; d.entry[1] = (arr_entry_t){1, 4}; d.entry[2] = (arr_entry_t){0, 1};
        d.entry[3] = (arr_entry_t){2, 8}; d.entry[4] = (arr_entry_t){1, 3};
        assert(arr_drop_scene(&d, 1) == 1 && d.count == 3);
        assert(d.entry[0].scene == 0 && d.entry[0].bars == 2 && d.entry[1].scene == 0 && d.entry[1].bars == 1 &&
               d.entry[2].scene == 2 && d.entry[2].bars == 8);
        assert(arr_valid(&d, 1u | 4u));            /* plays without the erased B */
        assert(arr_drop_scene(&d, 3) == 0 && d.count == 3);   /* not in the song: unchanged */
        d.count = 2; d.entry[0] = (arr_entry_t){3, 2}; d.entry[1] = (arr_entry_t){3, 4};
        assert(arr_drop_scene(&d, 3) == 1 && d.count == 4 && d.entry[0].scene == 0 && d.entry[3].scene == 3);   /* none left: the default song */
    }
    {   /* KARMA 3.0: LOOP entries (bars 0) play their section's own length */
        arr_config_t d;
        unsigned long smp;
        unsigned at[8], nt = 0;
        arr_defaults(&d);
        assert(d.count == 4 && d.entry[0].bars == 4 && d.entry[3].scene == 3);   /* the default: 4 bars each */
        for (unsigned i = 0; i < ARR_STEPS; i++) d.entry[i].bars = 0;
        assert(arr_entry_bars(&d, 0) == 2 && arr_entry_bars(&d, 1) == 1 && arr_entry_bars(&d, 2) == 3);
        assert(arr_begin(&r, &d, 15) == 0);
        for (smp = 0; r.running && smp < 44100ul * 60; smp += 32) {   /* 120 BPM: a bar is 2 s */
            int ev = arr_next(&r, &d, 44100);
            if (ev != ARR_NONE)
                at[nt++] = (unsigned)(smp / 44100ul);
            arr_elapse(&r, 32, 120);
        }
        assert(nt == 4 && at[0] == 4 && at[1] == 6 && at[2] == 12 && at[3] == 14);   /* A 2 bars, B 1, C 3, D 1: then the end */
        d.entry[0].bars = 5; assert(arr_entry_bars(&d, 0) == 5);
    }
    puts("arranger: order, repeats, stop, loop, tempo change, fractional timing, invalid scenes, erased section, LOOP lengths PASS");
    return 0;
}
