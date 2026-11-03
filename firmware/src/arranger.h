/* SPDX-License-Identifier: GPL-3.0-only */
/* Song order, independent of UI/flash/audio drivers. Durations are 4/4 bars.
 * Audio calls next BEFORE rendering a block, then elapse AFTER it. The clock
 * accumulates samples * BPM, keeping the remainder at every bar boundary. */
#ifndef FM1_ARRANGER_H
#define FM1_ARRANGER_H
#include <stdint.h>
#define ARR_STEPS 16u
#define ARR_SCENES 4u
#define ARR_NONE (-1)
#define ARR_DONE (-2)
#define ARR_INVALID (-3)
/* the scene byte: bits 0..1 the section A..D, bits 2..5 the tracks muted for this entry only (SEQ-SONG) */
#define ARR_SCN(b) ((uint32_t)(b) & 3u)
#define ARR_MUTE(b) (((uint32_t)(b) >> 2) & 15u)
typedef struct { uint8_t scene, bars; } arr_entry_t;   /* bars 0 = LOOP: the section's own length, once (KARMA 3.0) */
typedef struct {
    uint8_t count, loop, reserved[2];
    arr_entry_t entry[ARR_STEPS];
} arr_config_t;
typedef struct {
    uint32_t phase;
    uint8_t running, index, bar, error;
} arr_clock_t;
/* the bars a section's loop takes (its longest pattern), for an entry of 0 bars: the includer defines it */
static uint32_t arr_scene_bars(uint32_t scene);
static uint32_t arr_entry_bars(const arr_config_t *c, uint32_t i)
{
    uint32_t b = c->entry[i].bars;
    if (!b)
        b = arr_scene_bars(ARR_SCN(c->entry[i].scene));
    return b < 1u ? 1u : b > 64u ? 64u : b;
}

static void arr_defaults(arr_config_t *c)
{
    uint32_t i;
    c->count = 4; c->loop = 0; c->reserved[0] = c->reserved[1] = 0;
    for (i = 0; i < ARR_STEPS; i++) {
        c->entry[i].scene = (uint8_t)(i % ARR_SCENES);
        c->entry[i].bars = 4;
    }
}
static int arr_valid(const arr_config_t *c, uint32_t ready)
{
    uint32_t i;
    if (!c->count || c->count > ARR_STEPS || c->loop > 1u) return 0;
    for (i = 0; i < c->count; i++) {
        const arr_entry_t *e = &c->entry[i];
        if ((e->scene >> 6) || e->bars > 64u ||
            !(ready & (1u << ARR_SCN(e->scene)))) return 0;
    }
    return 1;
}
static int arr_begin(arr_clock_t *r, const arr_config_t *c, uint32_t ready)
{
    r->phase = 0; r->index = 0; r->bar = 0; r->running = 0;
    r->error = (uint8_t)!arr_valid(c, ready);
    if (r->error) return ARR_INVALID;
    r->running = 1;
    return (int)ARR_SCN(c->entry[0].scene);
}
static int arr_next(arr_clock_t *r, const arr_config_t *c, uint32_t sample_rate)
{
    int result = ARR_NONE;
    uint32_t period = sample_rate * 240u;
    if (!r->running || !period) return ARR_NONE;
    while (r->phase >= period) {
        r->phase -= period;
        if (++r->bar < arr_entry_bars(c, r->index)) continue;
        r->bar = 0;
        if (++r->index >= c->count) {
            if (!c->loop) { r->running = 0; return ARR_DONE; }
            r->index = 0;
        }
        result = (int)ARR_SCN(c->entry[r->index].scene);
    }
    return result;
}
/* (SLOOP 2.6) a section erased: the song's steps that played it taken out, the others kept in order; none left: the
 * default song. 1 if the song changed */
static int arr_drop_scene(arr_config_t *c, uint32_t scene)
{
    uint32_t i, n = 0, count = c->count > ARR_STEPS ? ARR_STEPS : c->count;
    for (i = 0; i < count; i++)
        if (ARR_SCN(c->entry[i].scene) != scene)
            c->entry[n++] = c->entry[i];
    if (n == count)
        return 0;
    for (i = n; i < ARR_STEPS; i++) {
        c->entry[i].scene = (uint8_t)(i % ARR_SCENES);
        c->entry[i].bars = 4;
    }
    c->count = (uint8_t)n;
    if (!n)
        arr_defaults(c);
    return 1;
}
static void arr_elapse(arr_clock_t *r, uint32_t samples, uint32_t bpm)
{
    if (r->running) r->phase += samples * bpm;
}
#endif
