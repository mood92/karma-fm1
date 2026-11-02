/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4: the full-screen visualiser. On HOME (the TRACKS screen), HOME tapped opens it; HOME again or
 * any page closes it; SELECT steps through the twelve styles (the name shows a second); a layer held shows
 * its screen as ever, then the visualiser comes back. The keys, PLAY and REC work as always.
 *   1 OSCILLOSCOPE  2 SPECTRUM  3 SPECTROGRAM  4 LISSAJOUS  5 VU METERS  6 CIRCLE
 *   7 TAPE  8 LCD  9 BOUNCE  10 ORBIT  11 WIRES  12 DANCER
 * (KARMA 3.0: DANCER, a white silhouette dancing to the song on black, is first: HOME on HOME opens on her; the
 * oscilloscope moved to the last place, the SLOOP sail is gone)
 * It reads what the audio already leaves for the UI: the scope buffers (audio.c, the mix at 22 kHz, left and
 * right, as if MASTER were all the way up: the picture does not follow the volume knob, even at 0), the tracks' peaks (fx.c / drums.c, as the TRACKS meters read them), the notes that started
 * (voice.c vis_hit) and the transport clock. Everything is drawn in the main loop, every other frame, in two
 * bands of 120 rows (the canvas holds 124): no cost to the audio. */
#define VIS_N 11u
static const char *const VIS_NAME[VIS_N] = {"TAPE", "SPECTRUM", "SPECTROGRAM", "LISSAJOUS", "VU METERS",
                                            "CIRCLE", "LCD", "BOUNCE", "ORBIT", "WIRES", "OSCILLOSCOPE"};
/* vis_on, vis_shown_last, vis_name_t: ui_draw.c; vis_style: panel.c (kept with the settings) */
#define VIS_FFT 512u
#define VIS_SG_W 48u                                /* spectrogram: 48 columns of 5 px, 72 rows of 3 px */
#define VIS_SG_H 72u
static int16_t vis_re[VIS_FFT] __attribute__((section(".pool"))), vis_im[VIS_FFT] __attribute__((section(".pool")));
static int16_t vis_l[VIS_FFT], vis_r[VIS_FFT];      /* this frame's scope snapshot */
static uint8_t vis_sg[VIS_SG_H][VIS_SG_W] __attribute__((section(".pool")));
static uint8_t vis_lj[3][256][2];                   /* Lissajous: the last three frames' points */
static struct {
    int32_t scale, ljscale, peak;                   /* auto-scales (smoothed peaks), this frame's peak */
    int16_t bar[32], cap[32], capv[32];             /* spectrum: bars, caps and their fall speed (0..1000) */
    int16_t vu[5], pk[5], pkt[5];                   /* VU: tracks + master, peak holds */
    int16_t lvl[5];                                 /* the tracks' levels (0..1000) this frame, the master */
    int16_t bh[4], bv[4];                           /* BOUNCE: height and speed (1/256 units) */
    uint8_t flash[4], kick;                         /* frames since a note started (0 = none) / a kick, fading */
    int16_t wamp[4];                                /* WIRES: each string's swing (0..1000) */
    uint8_t wn[4];                                  /* its wave count (from the note) */
    uint32_t free_q8, last_ms, wph;                 /* beats (1/256) when stopped; the wires' phase */
    uint8_t lj_n;
} vs;
/* the spectrum's 32 bands: bins of the 512-point FFT at 22 050 Hz (43 Hz each), 40 Hz .. 10 kHz */
static const uint8_t VIS_EDGE[33] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 25, 29, 35,
                                     41, 49, 58, 69, 82, 98, 116, 138, 164, 195, 232};

static int vis_shown(void) { return vis_on && !ui.home && !ui.menu && cur_page()->scope == SC_TRK; }
static void vis_open(void)
{
    vis_on = 1;
    vis_name_t = 30;
    ui.force = 1;
}
static int32_t isin(uint32_t a) { return SINE[a & 1023u]; }          /* a: 1024 a turn; Q15 */
static int32_t icos(uint32_t a) { return SINE[(a + 256u) & 1023u]; }
static uint16_t mix565(uint16_t a, uint16_t b, int32_t k)              /* a -> b, k 0..256 */
{
    int32_t r = (a >> 11) + ((((b >> 11) - (a >> 11)) * k) >> 8);
    int32_t g = ((a >> 5) & 63) + (((((b >> 5) & 63) - ((a >> 5) & 63)) * k) >> 8);
    int32_t bl = (a & 31) + ((((b & 31) - (a & 31)) * k) >> 8);
    return (uint16_t)(r << 11 | g << 5 | bl);
}
/* |x| (Q15) -> 0..1000 over 48 dB (8 log2: 6 dB a step of 8) */
static int32_t vis_lvl(int32_t a)
{
    int32_t lg = 0, v;
    if (a < 0)
        a = -a;
    if (a < 16)
        return 0;
    while ((a >> lg) > 1)
        lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);           /* 120 = 0 dB */
    return clamp((v - 56) * 1000 / 64, 0, 1000);
}

/* ---- drawing helpers (canvas coordinates; cv_oy picks the band) ---- */
static void vis_ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)
{
    int32_t y;
    if (rx < 1 || ry < 1)
        return;
    for (y = -ry; y <= ry; y++) {
        int32_t w = 0;
        while (w <= rx && (w * w) * (ry * ry) + (y * y) * (rx * rx) <= (rx * rx) * (ry * ry))
            w++;
        if (w)
            cv_rect(cx - w + 1, cy + y, 2 * w - 1, 1, c);
    }
}
static void vis_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)  /* an outline */
{
    int32_t x = r, y = 0, e = 1 - r;
    while (x >= y) {
        cv_pset(cx + x, cy + y, c), cv_pset(cx - x, cy + y, c), cv_pset(cx + x, cy - y, c), cv_pset(cx - x, cy - y, c);
        cv_pset(cx + y, cy + x, c), cv_pset(cx - y, cy + x, c), cv_pset(cx + y, cy - x, c), cv_pset(cx - y, cy - x, c);
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else
            e += 2 * (y - --x) + 1;
    }
}
static void vis_thick(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* 2 px */
{
    cv_line(x0, y0, x1, y1, c);
    cv_line(x0, y0 + 1, x1, y1 + 1, c);
}
/* a polygon, points in 1/16 px (n <= 8), filled by scan lines (even-odd) inside the band */
static void vis_poly(const int32_t *px, const int32_t *py, uint32_t n, uint16_t c)
{
    int32_t y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF, y;
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (py[i] < y0) y0 = py[i];
        if (py[i] > y1) y1 = py[i];
    }
    y0 = y0 >> 4;
    y1 = (y1 + 15) >> 4;
    if (y0 < -cv_oy) y0 = -cv_oy;
    if (y1 > (int32_t)cv_h - cv_oy) y1 = (int32_t)cv_h - cv_oy;
    for (y = y0; y < y1; y++) {
        int32_t xs[8], m = 0, sy = y * 16 + 8, a, b;
        for (i = 0; i < n && m < 8; i++) {
            uint32_t j = (i + 1u) % n;
            if ((py[i] <= sy && py[j] > sy) || (py[j] <= sy && py[i] > sy))
                xs[m++] = px[i] + (sy - py[i]) * (px[j] - px[i]) / (py[j] - py[i]);
        }
        for (a = 1; a < m; a++)                     /* (a few points: insertion sort) */
            for (b = a; b > 0 && xs[b - 1] > xs[b]; b--) {
                int32_t t = xs[b]; xs[b] = xs[b - 1]; xs[b - 1] = t;
            }
        for (a = 0; a + 1 < m; a += 2)
            cv_rect((xs[a] + 8) >> 4, y, ((xs[a + 1] + 8) >> 4) - ((xs[a] + 8) >> 4), 1, c);
    }
}
/* a seven-segment digit, the unlit segments faintly there (an LCD) */
static void vis_seg(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, int ch, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = ch >= 0 && ch <= 9 ? SEG[ch] : 0u;
    int32_t hh = h / 2;
    uint16_t off = RGB(18, 18, 21);
    cv_rect(x + t, y, w - 2 * t, t, m & 0x01 ? c : off);
    cv_rect(x + w - t, y + t, t, hh - t, m & 0x02 ? c : off);
    cv_rect(x + w - t, y + hh, t, hh - t, m & 0x04 ? c : off);
    cv_rect(x + t, y + h - t, w - 2 * t, t, m & 0x08 ? c : off);
    cv_rect(x, y + hh, t, hh - t, m & 0x10 ? c : off);
    cv_rect(x, y + t, t, hh - t, m & 0x20 ? c : off);
    cv_rect(x + t, y + hh - t / 2, w - 2 * t, t, m & 0x40 ? c : off);
}

/* ---- the frame's data ---- */
/* beats since PLAY in 1/256 (stopped: a free count at the tempo, for what moves anyway) */
static uint32_t vis_beat_q8(void)
{
    if (song.playing)
        return clk_beat * 256u + clk_pos / (BEAT_U / 256u);
    return vs.free_q8;
}
static void vis_fft(void)
{
    uint32_t i, j = 0, bit, len;
    for (i = 0; i < VIS_FFT; i++) {                 /* Hann window */
        int32_t w = (32768 - icos(i * 2u)) >> 1;
        vis_re[i] = (int16_t)((vis_l[i] * w) >> 15);
        vis_im[i] = 0;
    }
    for (i = 1; i < VIS_FFT; i++) {                 /* bit reversal */
        for (bit = VIS_FFT >> 1; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            int16_t t = vis_re[i]; vis_re[i] = vis_re[j]; vis_re[j] = t;
        }
    }
    for (len = 2; len <= VIS_FFT; len <<= 1) {      /* radix 2, halved each stage (no overflow) */
        uint32_t half = len >> 1, k;
        for (i = 0; i < VIS_FFT; i += len)
            for (k = 0; k < half; k++) {
                uint32_t a = i + k, b = a + half, ix = k * (1024u / len);
                int32_t wr = icos(ix), wi = -isin(ix);
                int32_t tr = (vis_re[b] * wr - vis_im[b] * wi) >> 15, ti = (vis_re[b] * wi + vis_im[b] * wr) >> 15;
                int32_t ar = vis_re[a], ai = vis_im[a];
                vis_re[b] = (int16_t)((ar - tr) >> 1);
                vis_im[b] = (int16_t)((ai - ti) >> 1);
                vis_re[a] = (int16_t)((ar + tr) >> 1);
                vis_im[a] = (int16_t)((ai + ti) >> 1);
            }
    }
}
/* the 32 bands of the last FFT, 0..1000 (60 dB under a full-scale sine) */
static void vis_bands(int16_t *out)
{
    uint32_t k, i;
    for (k = 0; k < 32u; k++) {
        int32_t m = 0, lg = 0, v;
        for (i = VIS_EDGE[k]; i < VIS_EDGE[k + 1] && i < VIS_FFT / 2u; i++) {
            int32_t a = vis_re[i] < 0 ? -vis_re[i] : vis_re[i], b = vis_im[i] < 0 ? -vis_im[i] : vis_im[i];
            int32_t mag = a > b ? a + b / 2 : b + a / 2;
            if (mag > m)
                m = mag;
        }
        if (m < 2) {
            out[k] = 0;
            continue;
        }
        while ((m >> lg) > 1)
            lg++;
        v = lg * 8 + (((m << 3) >> lg) & 7);        /* a full-scale sine: ~104 */
        out[k] = (int16_t)clamp((v - 24) * 1000 / 80, 0, 1000);
    }
}

static void vis_update(void)
{
    uint32_t i, w, hit, now = fm1_ms;
    int32_t pk[4], mpk = 0;
    /* the scope: the last 512 frames (left, right) */
    w = scope_w;
    for (i = 0; i < VIS_FFT; i++) {
        vis_l[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        vis_r[i] = scope_bufr[(w + i) & (SCOPE_N - 1u)];
        if (vis_l[i] > mpk) mpk = vis_l[i];
        if (-vis_l[i] > mpk) mpk = -vis_l[i];
    }
    /* the tracks' peaks since the last look (as the TRACKS meters take them) and the notes that started */
    fm1_irq_off();
    for (i = 0; i < NPART; i++) {
        pk[i] = trk[i].peak;
        trk[i].peak = 0;
    }
    pk[3] = drums.peak;
    drums.peak = 0;
    hit = vis_hit;
    vis_hit = 0;
    if (vis_kick_hit) {
        vis_kick_hit = 0;
        vs.kick = 255;
    }
    fm1_irq_on();
    for (i = 0; i < 4u; i++) {
        int32_t v = trk_level(i) && !trk[i].p[P_MUTE] ? vis_lvl(pk[i]) : 0;   /* (as the TRACKS meters) */
        vs.lvl[i] = (int16_t)v;
        if ((hit >> i) & 1u) {
            vs.flash[i] = 8;
            vs.wn[i] = (uint8_t)(2u + vis_note[i] % 12u / 2u);
            if (vs.wamp[i] < 1000) vs.wamp[i] = 1000;
            if (vs.bh[i] < 8 * 256)                 /* BOUNCE: a kick, unless already high up */
                vs.bv[i] = (int16_t)(3 * 256 + 51 * (int32_t)i);
        } else if (vs.flash[i]) {
            vs.flash[i]--;
        }
        vs.wamp[i] = (int16_t)(vs.wamp[i] > v ? vs.wamp[i] - (vs.wamp[i] - v) / 6 - 4 : v);
        if (vs.wamp[i] < 0) vs.wamp[i] = 0;
        /* BOUNCE physics: gravity, a bounce off the floor */
        vs.bv[i] = (int16_t)(vs.bv[i] - 56);
        vs.bh[i] = (int16_t)clamp(vs.bh[i] + vs.bv[i], 0, 34 * 256);
        if (!vs.bh[i] && vs.bv[i] < 0)
            vs.bv[i] = (int16_t)(vs.bv[i] < -256 ? -vs.bv[i] * 35 / 100 : 0);
    }
    vs.lvl[4] = (int16_t)vis_lvl(mpk);
    vs.kick = (uint8_t)(vs.kick > 20 ? vs.kick - 20 : 0);
    /* the free beat count (stopped) */
    {
        uint32_t dt = now - vs.last_ms;
        vs.last_ms = now;
        if (dt > 200u) dt = 200u;
        vs.free_q8 += dt * (uint32_t)song.g[G_BPM] * 256u / 60000u;
    }
    vs.wph += 37u;
    vs.peak = mpk;
    {                                               /* the wave's scale follows the peak */
        int32_t target = mpk < 1600 ? 1600 : mpk;
        vs.scale += (target - vs.scale) / 5;
    }
    switch (vis_style) {
    case 1: case 2: {
        int16_t b[32];
        vis_fft();
        vis_bands(b);
        if (vis_style == 1) {
            for (i = 0; i < 32u; i++) {
                int32_t bar = vs.bar[i] - vs.bar[i] * 18 / 100;
                vs.bar[i] = (int16_t)(b[i] > bar ? b[i] : bar);
                if (vs.bar[i] >= vs.cap[i]) {
                    vs.cap[i] = vs.bar[i];
                    vs.capv[i] = 0;
                } else {
                    vs.capv[i] = (int16_t)(vs.capv[i] + 4);
                    vs.cap[i] = (int16_t)(vs.cap[i] > vs.capv[i] ? vs.cap[i] - vs.capv[i] : 0);
                }
            }
        } else {                                    /* spectrogram: the new row on top */
            for (i = VIS_SG_H - 1u; i; i--)            /* (no memmove here: row by row, from the bottom) */
                memcpy(vis_sg[i], vis_sg[i - 1u], VIS_SG_W);
            for (i = 0; i < VIS_SG_W; i++) {
                uint32_t p = i * 31u * 16u / (VIS_SG_W - 1u), k = p >> 4, f = p & 15u;
                int32_t v = k < 31u ? (b[k] * (int32_t)(16u - f) + b[k + 1u] * (int32_t)f) / 16 : b[31];
                vis_sg[0][i] = (uint8_t)(v * 255 / 1000);
            }
        }
        break;
    }
    case 3: {                                       /* Lissajous: L-R across, L+R up; three frames kept */
        int32_t m = 1;
        uint8_t (*p)[2];
        for (i = 0; i < VIS_FFT; i += 2u) {
            int32_t s = vis_l[i] + vis_r[i];
            if (s < 0) s = -s;
            if (s > m) m = s;
        }
        if (m < 3000) m = 3000;
        vs.ljscale += (m - vs.ljscale) / 5;
        if (vs.ljscale < 1) vs.ljscale = 1;
        vs.lj_n = (uint8_t)((vs.lj_n + 1u) % 3u);
        p = vis_lj[vs.lj_n];
        for (i = 0; i < 256u; i++) {
            int32_t l = vis_l[i * 2u], r = vis_r[i * 2u];
            p[i][0] = (uint8_t)clamp(120 + (l - r) * 170 / vs.ljscale, 0, 239);
            p[i][1] = (uint8_t)clamp(120 - (l + r) * 95 / vs.ljscale, 0, 239);
        }
        break;
    }
    case 4:                                         /* VU: fall 3 % a frame, peaks held a second */
        for (i = 0; i < 5u; i++) {
            int32_t v = vs.lvl[i];
            vs.vu[i] = (int16_t)(v > vs.vu[i] - 30 ? v : vs.vu[i] - 30);
            if (v >= vs.pk[i]) {
                vs.pk[i] = (int16_t)v;
                vs.pkt[i] = 0;
            } else if (++vs.pkt[i] > 30) {
                vs.pk[i] = (int16_t)(vs.pk[i] > 20 ? vs.pk[i] - 20 : 0);
            }
        }
        break;
    case 0:                                         /* DANCER: her energy, the mix's and each track's level */
        for (i = 0; i < 5u; i++) {                  /* (fast up, slow down) */
            vs.vu[i] = (int16_t)(vs.lvl[i] > vs.vu[i] ? vs.vu[i] + (vs.lvl[i] - vs.vu[i]) / 3 : vs.vu[i] - 8);
            if (vs.vu[i] < 0) vs.vu[i] = 0;
        }
        break;
    default:
        break;
    }
}

/* ---- the styles (each draws the whole screen; called once per band) ---- */
static void vis_scope(void)
{
    uint32_t i0 = 0, i;
    int32_t py = 0;
    for (i = 1; i + 240u < VIS_FFT; i++)            /* a rising zero crossing: the wave stands still */
        if (vis_l[i - 1u] < 0 && vis_l[i] >= 0) {
            i0 = i;
            break;
        }
    cv_rect(0, 120, 240, 1, C_LINE);
    for (i = 0; i < 240u; i += 40u)
        cv_rect((int32_t)i, 116, 1, 9, C_LINE);
    for (i = 0; i < 240u; i++) {
        int32_t y = 120 - vis_l[i0 + i] * 100 / (vs.scale ? vs.scale : 1);
        y = clamp(y, 2, 237);
        if (i) {
            cv_line((int32_t)i - 1, py - 1, (int32_t)i, y - 1, TE_DIM[0]);
            cv_line((int32_t)i - 1, py + 2, (int32_t)i, y + 2, TE_DIM[0]);
            vis_thick((int32_t)i - 1, py, (int32_t)i, y, TE_COL[0]);
        }
        py = y;
    }
}
static void vis_spectrum(void)
{
    uint32_t k;
    for (k = 0; k < 32u; k++) {
        int32_t x0 = 4 + (int32_t)k * 7, h = vs.bar[k] * 210 / 1000, y, cy = 232 - vs.cap[k] * 210 / 1000;
        for (y = 232; y > 232 - h; y -= 4)          /* LED segments */
            cv_rect(x0, y - 2, 5, 3, TE_COL[k / 8u]);
        cv_rect(x0, cy - 1, 5, 2, C_WHITE);
    }
}
static uint16_t vis_heat(uint32_t v)               /* 0..255: black, blue, green, yellow, white */
{
    static const uint16_t ST[5] = {RGB(0, 0, 0), RGB(40, 124, 255), RGB(30, 204, 112), RGB(255, 198, 24), RGB(255, 255, 255)};
    static const uint8_t AT[5] = {0, 77, 140, 204, 255};
    uint32_t k;
    v = (v + ((v * v) >> 8)) >> 1;                  /* (quiet parts a little darker: about v^1.4) */
    for (k = 1; k < 5u && v > AT[k]; k++)
        ;
    if (k >= 5u)
        return ST[4];
    return mix565(ST[k - 1u], ST[k], (int32_t)((v - AT[k - 1u]) * 256u / (AT[k] - AT[k - 1u])));
}
static void vis_spectrogram(void)
{
    uint32_t r, c;
    for (r = 0; r < VIS_SG_H; r++) {
        int32_t y = 24 + (int32_t)r * 3;
        if (y + 3 <= -cv_oy || y >= (int32_t)cv_h - cv_oy)
            continue;
        for (c = 0; c < VIS_SG_W; c++)
            if (vis_sg[r][c] > 8u)
                cv_rect((int32_t)c * 5, y, 5, 3, vis_heat(vis_sg[r][c]));
    }
    cv_text(2, 2, &FONT_S, "40", C_DIM);
    cv_text(30, 2, &FONT_S, "200", C_DIM);
    cv_text(132, 2, &FONT_S, "1k", C_DIM);
    cv_text(196, 2, &FONT_S, "4k", C_DIM);
}
static void vis_lissajous(void)
{
    uint32_t f, i;
    static const uint16_t COLS[3] = {RGB(10, 66, 38), RGB(20, 136, 76), RGB(30, 204, 112)};
    cv_rect(120, 30, 1, 180, C_LINE);
    cv_rect(30, 120, 180, 1, C_LINE);
    for (f = 0; f < 3u; f++) {                      /* the oldest first, dimmest */
        const uint8_t (*p)[2] = vis_lj[(vs.lj_n + 1u + f) % 3u];
        for (i = 0; i < 256u; i++)
            cv_pset(p[i][0], p[i][1], COLS[f]);
    }
    cv_text(116, 4, &FONT_S, "M", C_DIM);
    cv_text(6, 112, &FONT_S, "L", C_DIM);
    cv_text(226, 112, &FONT_S, "R", C_DIM);
}
static void vis_meters(void)
{
    uint32_t i, k;
    for (i = 0; i < 5u; i++) {
        int32_t x0 = 10 + (int32_t)i * 46, on = vs.vu[i] * 24 / 1000, pk = vs.pk[i] * 24 / 1000;
        uint16_t c = i < 4u ? TE_COL[i] : C_WHITE;
        char lab[2] = {(char)(i < 4u ? '1' + i : 'M'), 0};
        for (k = 0; k < 24u; k++) {
            int32_t y = 206 - (int32_t)k * 8;
            uint16_t col = (int32_t)k < on ? (k < 18u ? c : k < 22u ? TE_COL[2] : RGB(255, 44, 52)) : TE_G1;
            if ((int32_t)k == pk && pk > 0)
                col = C_WHITE;
            cv_rect(x0, y - 5, 35, 6, col);
        }
        cv_text(x0 + 13, 216, &FONT_S, lab, c);
    }
}
static void vis_ring(void)
{
    int32_t k = vs.kick, r0 = 52 + k * 18 / 255, rc = r0 * 6 / 10, sc = vs.peak > vs.scale ? vs.peak : vs.scale;
    int32_t px = 0, py = 0, i;
    vis_ellipse(120, 120, rc, rc, RGB(40 + 140 * k / 255, 14 + 50 * k / 255, 4));
    for (i = 0; i <= 180; i++) {                    /* the wave round the ring, 2 degrees a point */
        uint32_t a = (uint32_t)(i % 180) * 1024u / 180u, j = 20u + (uint32_t)(i % 180) * 2u;
        int32_t s = (vis_l[j - 2u] + vis_l[j - 1u] + vis_l[j] + vis_l[j + 1u] + vis_l[j + 2u]) / 5;
        int32_t rr = r0 + s * 20 / (sc > 1600 ? sc : 1600), x = 120 + ((icos(a) * rr) >> 15), y = 120 + ((isin(a) * rr) >> 15);
        if (i) {
            cv_line(px, py - 1, x, y - 1, RGB(120, 40, 10));
            cv_line(px, py + 2, x, y + 2, RGB(120, 40, 10));
            vis_thick(px, py, x, y, TE_COL[3]);
        }
        px = x, py = y;
    }
}
static void vis_lcd(void)
{
    uint32_t bpm = (uint32_t)song.g[G_BPM], beat = song.playing ? clk_beat : 0u, bar = beat / 4u + 1u, k, i;
    vis_seg(18, 30, 44, 76, 7, bpm >= 100u ? (int)(bpm / 100u) : -1, C_WHITE);
    vis_seg(74, 30, 44, 76, 7, (int)(bpm / 10u % 10u), C_WHITE);
    vis_seg(130, 30, 44, 76, 7, (int)(bpm % 10u), C_WHITE);
    cv_text(190, 90, &FONT_S, "bpm", C_DIM);
    vis_seg(18, 124, 26, 46, 5, bar >= 10u ? (int)(bar / 10u % 10u) : -1, TE_COL[3]);
    vis_seg(52, 124, 26, 46, 5, (int)(bar % 10u), TE_COL[3]);
    cv_rect(88, 164, 5, 6, TE_COL[3]);
    vis_seg(100, 124, 26, 46, 5, (int)(beat % 4u + 1u), TE_COL[3]);
    for (k = 0; k < 4u; k++)
        vis_ellipse(154 + (int32_t)k * 22, 144, 8, 8, song.playing && beat % 4u == k ? TE_COL[3] : RGB(40, 40, 46));
    for (i = 0; i < 4u; i++)
        for (k = 0; k < 14u; k++)
            cv_rect(18 + (int32_t)k * 15, 186 + (int32_t)i * 12, 13, 9,
                    (int32_t)k < vs.lvl[i] * 14 / 1000 + (vs.lvl[i] > 0) ? TE_COL[i] : RGB(30, 30, 34));
}
static void vis_bounce(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t x = 36 + (int32_t)i * 56, h = vs.bh[i], y = 196 - h * 45 / 2560, sh = 16 - h / 512;
        int32_t sq = h < 512 && vs.bv[i] < 0 ? 4 : 0;   /* squashed as it lands */
        vis_ellipse(x, 205, sh < 4 ? 4 : sh, 3, RGB(30, 30, 34));
        vis_ellipse(x, y, 16 + sq, 16 - sq, TE_COL[i]);
        {
            char lab[2] = {(char)('1' + i), 0};
            cv_text(x - 4, 220, &FONT_S, lab, TE_COL[i]);
        }
    }
    cv_rect(8, 212, 224, 2, C_LINE);
}
static void vis_orbit(void)
{
    static const uint8_t RAD[4] = {42, 62, 82, 102}, PER[4] = {1, 2, 4, 8};
    uint32_t bq = vis_beat_q8(), i, k;
    int32_t rs = 14 + vs.lvl[4] * 22 / 1000;
    for (i = 0; i < 4u; i++) {
        uint32_t a = (bq * 4u / PER[i]) - 256u;     /* a turn = 1024: per beats, from the top */
        int32_t pr = 4 + vs.lvl[i] * 8 / 1000;
        vis_circle(120, 120, RAD[i], RGB(36, 36, 42));
        for (k = 1; k < 7u; k++) {                  /* its trail */
            uint32_t ak = a - k * 13u;
            vis_ellipse(120 + ((icos(ak) * RAD[i]) >> 15), 120 + ((isin(ak) * RAD[i]) >> 15), 2, 2,
                        mix565(TE_COL[i], 0, (int32_t)(60u + k * 196u / 7u)));
        }
        vis_ellipse(120 + ((icos(a) * RAD[i]) >> 15), 120 + ((isin(a) * RAD[i]) >> 15), pr, pr, TE_COL[i]);
    }
    vis_ellipse(120, 120, rs, rs, C_WHITE);
}
static void vis_wires(void)
{
    uint32_t i, x;
    for (i = 0; i < 4u; i++) {
        int32_t y0 = 40 + (int32_t)i * 52, amp = vs.wamp[i] * 22 / 1000, py = y0;
        char lab[2] = {(char)('1' + i), 0};
        for (x = 0; x < 240u; x += 2u) {           /* a standing wave: the string's shape x its swing */
            uint32_t a = x * 512u / 240u, cyc = i == 3u ? 8u : vs.wn[i] ? vs.wn[i] : 2u;   /* sin(pi x / L) */
            int32_t env = isin(a), wv = isin(x * cyc * 1024u / 240u + vs.wph * (3u + i));
            int32_t y = y0 - (((env * wv) >> 15) * amp >> 15);
            if (x)
                vis_thick((int32_t)x - 2, py, (int32_t)x, y, TE_COL[i]);
            py = y;
        }
        vis_ellipse(4, y0, 4, 4, C_WHITE);
        vis_ellipse(236, y0, 4, 4, C_WHITE);
        cv_text(10, y0 - 22, &FONT_S, lab, TE_COL[i]);
    }
}
/* the logo (tools/gen_logo.py at 0.86, centred on 120, 112), alive: the dial is the master, the sail's four
 * bands the tracks (growing with their level, flashing on a note), the boat rocks on the master's wave */
/* TAPE (KARMA 3.0; 2026-10-27, Ariel: the deck on top of the home screen, big, instead of the dancer): the bar in
 * large digits, KARMA, the song's sections A..D (the one playing filled); the two reels turning while it plays, the
 * tape going from the left one to the right one over the selected track's loop, past the head (still when it
 * stops); under the deck two VU meters, the stereo out left and right (2026-10-27, Ariel): a needle on a VU scale
 * (-20 .. +3, red past 0; its marks where a VU meter has them, the needle following the level's amplitude, not
 * its dB), at 0 VU an RMS of 18000 (-5 dBFS: the mix runs hot, so loud parts touch the red), rising in a third of
 * a picture's way and falling slower; the level in dB under it; a lamp for the peaks (near full scale). Silence: the
 * needles rest at the left */
static const uint32_t KARMA_ROW[7] = {0x0E44F391u, 0x116D1449u, 0x11551445u, 0x1F54F7C3u, 0x11445445u, 0x11449449u,
                                      0x11451451u};             /* tools/gen_logo.py KARMA_CELLS, a bit a cell */
static const uint16_t KARMA_COL[5] = {RGB(40, 124, 255), RGB(30, 204, 112), RGB(228, 228, 236), RGB(255, 98, 26),
                                      RGB(141, 130, 236)};       /* (a letter each) */
#define VU_REF 18000                                            /* the RMS at 0 VU */
static int32_t vu_amp[2];                                       /* the needles: the RMS, with the meter's ballistics */
static uint8_t vu_lamp[2];                                      /* pictures the peak lamp stays lit */
static const uint16_t VU_DB[47] = {180, 202, 227, 254, 285, 320, 359, 403, 452, 507, 569, 639, 717, 804, 902, 1012, 1136, 1274, 1430, 1604, 1800, 2020, 2266, 2543, 2853, 3201, 3591, 4030, 4521, 5073, 5692, 6387, 7166, 8040, 9021, 10122, 11357, 12743, 14298, 16043, 18000, 20196, 22661, 25426, 28528, 32009, 35915};   /* VU_REF at -40 .. +6 dB */
static uint32_t vu_sqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b)
            v -= r + b, r = (r >> 1) + b;
        else
            r >>= 1;
        b >>= 2;
    }
    return r;
}
/* one meter: frame, scale, marks, the needle (pos 0..1000 along the scale), its dB, the lamp */
static void vu_meter(int32_t x0, const char *ch, int32_t amp, int lamp)
{
    static const int16_t MARK[11] = {0, 165, 264, 352, 463, 529, 603, 686, 779, 883, 1000};   /* -20 -10 -7 -5 -3 -2 -1 0 1 2 3 */
    static const char *const LAB[11] = {"20", "", "7", "", "3", "", "", "0", "", "", "3"};
    int32_t cx = x0 + 56, py = 262, k, pos, d;
    uint32_t a;
    char b[8];
    op_box(x0, 176, 112, 62, OP_PUR);
    for (a = 654u; a < 882u; a += 6u) {                         /* the scale: 768 is straight up, 40 degrees a side */
        uint32_t a1 = a + 6u > 882u ? 882u : a + 6u;
        uint16_t c = a >= 654u + 686u * 228u / 1000u ? TE_RED : OP_PUR;
        cv_line(cx + ((icos(a) * 70) >> 15), py + ((isin(a) * 70) >> 15), cx + ((icos(a1) * 70) >> 15), py + ((isin(a1) * 70) >> 15), c);
        cv_line(cx + ((icos(a) * 69) >> 15), py + ((isin(a) * 69) >> 15), cx + ((icos(a1) * 69) >> 15), py + ((isin(a1) * 69) >> 15), c);
    }
    for (k = 0; k < 11; k++) {
        uint32_t m = 654u + (uint32_t)MARK[k] * 228u / 1000u;
        int32_t r1 = LAB[k][0] ? 77 : 74;
        uint16_t c = k > 7 ? TE_RED : k == 7 ? C_WHITE : OP_PUR;
        cv_line(cx + ((icos(m) * 70) >> 15), py + ((isin(m) * 70) >> 15), cx + ((icos(m) * r1) >> 15), py + ((isin(m) * r1) >> 15), c);
        if (LAB[k][0])
            cv_text(cx + ((icos(m) * 58) >> 15) - text_w(&FONT_S, LAB[k]) / 2, py + ((isin(m) * 58) >> 15) - 6, &FONT_S, LAB[k], c);
    }
    cv_text(x0 + 5, 222, &FONT_S, ch, TE_G3);
    cv_text(cx - text_w(&FONT_S, "VU") / 2, 212, &FONT_S, "VU", TE_G3);
    for (d = 46; d > 0 && amp < VU_DB[d]; d--)                  /* the level, to the dB */
        ;
    if (amp < VU_DB[0])
        str_cpy(b, "--", sizeof b);
    else
        fmt_int(b, d - 40);
    str_cpy(b + str_len(b), "dB", 3);
    cv_text(x0 + 107 - text_w(&FONT_S, b), 222, &FONT_S, b, amp >= VU_REF ? TE_RED : TE_G4);
    if (lamp)
        vis_ellipse(x0 + 102, 186, 3, 3, TE_RED);
    else
        vis_circle(x0 + 102, 186, 3, OP_PUR_D);
    pos = clamp((amp * 1000 / VU_REF - 100) * 1000 / 1313, -20, 1040);   /* (the amplitude's own scale; a little past the ends) */
    a = (uint32_t)(654 + pos * 228 / 1000) & 1023u;
    cv_line(cx + ((icos(a) * 28) >> 15), py + ((isin(a) * 28) >> 15), cx + ((icos(a) * 74) >> 15), py + ((isin(a) * 74) >> 15), C_WHITE);
    cv_line(cx + 1 + ((icos(a) * 28) >> 15), py + ((isin(a) * 28) >> 15), cx + 1 + ((icos(a) * 74) >> 15), py + ((isin(a) * 74) >> 15), C_WHITE);
}
static void vis_reel(void)
{
    track_t *ts = TSEL;
    uint32_t s, x, y, i, playing = song.playing, slen = trk_len(ts), spos = ts->seq_idx % slen, ready = arrangement_ready();
    uint32_t bar = playing ? spos / 16u + 1u : 1u, a = playing ? (ui.frame * 12u) & 1023u : 0u;
    int32_t done = playing ? (int32_t)(spos * 1000u / slen) : 0, pack[2];
    uint16_t lit = playing ? C_WHITE : TE_G3;
    te_digit(8, 10, 20, 40, 4, bar / 10u, lit);                 /* the bar (a 1: a bar) */
    te_digit(34, 10, bar % 10u == 1u ? 8 : 20, 40, 4, bar % 10u, lit);
    cv_text(bar % 10u == 1u ? 48 : 60, 36, &FONT_S, "BAR", TE_G3);
    for (y = 0; y < 7u; y++)                                    /* KARMA */
        for (x = 0; x < 29u; x++)
            if ((KARMA_ROW[y] >> x) & 1u)
                cv_rect(83 + (int32_t)(x * 2u), 30 + (int32_t)(y * 2u), 2, 2, KARMA_COL[x / 6u]);
    for (i = 0; i < 4u; i++) {                                  /* the sections */
        char ch[2] = {(char)('A' + i), 0};
        int32_t sx = 144 + (int32_t)i * 24;
        int on = live_sec == (int8_t)i, used = (ready >> i) & 1u;
        if (on)
            cv_rect(sx, 10, 20, 40, C_WHITE);
        else
            op_box(sx, 10, 20, 40, used ? OP_PUR : OP_PUR_D), op_box(sx + 1, 11, 18, 38, used ? OP_PUR : OP_PUR_D);
        cv_text(sx + (20 - text_w(&FONT_L, ch)) / 2, 18, &FONT_L, ch, on ? C_BLACK : used ? OP_PUR : OP_PUR_D);
    }
    /* the reels: the tape from the left one to the right one over the loop */
    pack[0] = 36 - done * 22 / 1000, pack[1] = 14 + done * 22 / 1000;
    for (s = 0; s < 2u; s++) {
        int32_t cx = s ? 176 : 64, cy = 106, sp;
        op_ell(cx, cy, 46, 46, OP_PUR);
        op_ell(cx, cy, 45, 45, OP_PUR);
        op_ell(cx, cy, pack[s], pack[s], TE_COL[3]);            /* the tape on this reel */
        op_ell(cx, cy, pack[s] - 1, pack[s] - 1, TE_COL[3]);
        op_ell(cx, cy, 8, 8, OP_PUR);
        op_ell(cx, cy, 7, 7, OP_PUR);
        for (sp = 0; sp < 3; sp++) {                            /* three spokes */
            uint32_t an = (a + (uint32_t)sp * 341u) & 1023u;
            vis_thick(cx + ((icos(an) * 11) >> 15), cy + ((isin(an) * 11) >> 15),
                      cx + ((icos(an) * 41) >> 15), cy + ((isin(an) * 41) >> 15), OP_PUR);
        }
    }
    vis_thick(30, 140, 86, 162, TE_COL[3]);                     /* the tape, past the head */
    vis_thick(86, 162, 154, 162, TE_COL[3]);
    vis_thick(154, 162, 210, 140, TE_COL[3]);
    cv_rect(107, 154, 26, 16, C_BLACK);
    op_box(107, 154, 26, 16, lit);
    op_box(108, 155, 24, 14, lit);
    /* the VU meters (their needles moved once a picture, at the top band) */
    if (cv_oy == 0) {
        for (s = 0; s < 2u; s++) {
            const int16_t *w = s ? vis_r : vis_l;
            uint32_t q = 0, pk = 0, v;
            int32_t rms;
            for (i = 0; i < VIS_FFT; i++) {
                int32_t t = w[i] >> 4;
                q += (uint32_t)(t * t);
                v = (uint32_t)(w[i] < 0 ? -w[i] : w[i]);
                if (v > pk)
                    pk = v;
            }
            rms = (int32_t)vu_sqrt(q / VIS_FFT) * 16;
            vu_amp[s] += (rms - vu_amp[s]) * (rms > vu_amp[s] ? 3 : 1) / (rms > vu_amp[s] ? 10 : 6);
            vu_lamp[s] = pk >= 31000u ? 12u : vu_lamp[s] ? (uint8_t)(vu_lamp[s] - 1u) : 0u;
        }
    }
    vu_meter(4, "L", vu_amp[0], vu_lamp[0] != 0);
    vu_meter(124, "R", vu_amp[1], vu_lamp[1] != 0);
}

/* the visualiser, every other frame: the update, then both bands */
static void vis_draw(void)
{
    uint32_t pass;
    if (!vis_shown_last || ui.force) {
        vis_shown_last = 1;
        lcd_fill(0, 0, 240, 240, C_BLACK);
    } else if (ui.frame & 1u) {
        return;
    }
    vis_update();
    for (pass = 0; pass < 2u; pass++) {
        cv_begin(240, 120, C_BLACK);
        cv_oy = -(int32_t)(pass * 120u);
        switch (vis_style % VIS_N) {
        case 0: vis_reel(); break;
        case 1: vis_spectrum(); break;
        case 2: vis_spectrogram(); break;
        case 3: vis_lissajous(); break;
        case 4: vis_meters(); break;
        case 5: vis_ring(); break;
        case 6: vis_lcd(); break;
        case 7: vis_bounce(); break;
        case 8: vis_orbit(); break;
        case 9: vis_wires(); break;
        default: vis_scope(); break;
        }
        if (!pass && (vis_name_t || ui.msg_t)) {    /* the style's name a second (or a message) */
            char n[8];
            const char *s = ui.msg_t ? ui.msg : VIS_NAME[vis_style % VIS_N];
            cv_rect(0, 0, 240, 20, C_BLACK);
            cv_text(120 - text_w(&FONT_S, s) / 2, 2, &FONT_S, s, C_WHITE);
            if (!ui.msg_t) {
                fmt_int(n, (int32_t)(vis_style % VIS_N) + 1);
                str_cpy(n + str_len(n), "/11", 4);
                cv_text(4, 2, &FONT_S, n, C_DIM);
            }
        }
        cv_oy = 0;
        cv_blit(0, pass * 120u);
    }
    if (vis_name_t)
        vis_name_t--;
}
/* SELECT on the visualiser: the next / previous style (round) */
static void vis_select(int32_t s)
{
    vis_style = (uint8_t)((vis_style + (s > 0 ? 1u : VIS_N - 1u)) % VIS_N);
    vis_name_t = 30;
    ui.force = 1;
    settings_later = 1;                                 /* (kept with the settings, once stopped) */
}

/* ---- the boot animation (KARMA 3.0): the KARMA letters pop in a cell at a time in the tracks' colours, the four
 * tracks' bars sweep in, the version, then it fades into the UI; ~2 s at the UI's frame rate, a button or a key skips
 * it. boot_t: started at 0 by the boot (main.c, the browser simulator's web_boot); 0xFFFF: none (the host tests) */
static uint16_t boot_t = 0xFFFFu;
#define BOOT_END 124u                                   /* (~2.1 s; 2026-10-28: the logo screen gone; 2.5 s felt slow) */
static int boot_draw(void)
{
    const uint32_t *ROW = KARMA_ROW;
    const uint16_t *COL = KARMA_COL;
    uint32_t t = boot_t, pass, x, y;
    int32_t fade;
    if (t >= BOOT_END)
        return 0;
    if (fm1_in.buttons || fm1_in.notes || ++boot_t >= BOOT_END) {
        boot_t = 0xFFFFu;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
        return 0;
    }
    fade = t > BOOT_END - 20u ? (int32_t)(t - (BOOT_END - 20u)) * 256 / 20 : 0;   /* (the last 20 frames: to black) */
    for (pass = 0; pass < 2u; pass++) {
        cv_begin(240, 120, C_BLACK);
        cv_oy = -(int32_t)(pass * 120u);
        for (y = 0; y < (uint32_t)MM_LOGO_H; y++)               /* the Main Menu mark, each cell its own moment */
            for (x = 0; x < (uint32_t)MM_LOGO_W; x++) {
                uint32_t i = y * (uint32_t)MM_LOGO_W + x, v = (MM_LOGO[i >> 1] >> ((i & 1u) * 4u)) & 15u, d, s;
                uint16_t c;
                if (!v)
                    continue;
                d = ((x * 7u + y * 13u) * 2654435761u >> 26) % 44u;
                if (t < d)
                    continue;
                s = t - d >= 3u ? 4u : t - d + 1u;
                c = t - d < 4u ? C_WHITE : v == 3u ? C_WHITE : COL[(x + y) / 8u % 4u];
                c = mix565(c, C_BLACK, fade);
                cv_rect(80 + (int32_t)(x * 4u) + (4 - (int32_t)s) / 2, 56 + (int32_t)(y * 4u) + (4 - (int32_t)s) / 2,
                        (int32_t)s, (int32_t)s, c);
            }
        (void)ROW;
        for (x = 0; x < 4u; x++) {                              /* the tracks' bars */
            int32_t w = clamp(((int32_t)t - 50 - (int32_t)x * 4) * 5, 0, 44);
            if (w)
                cv_rect(26 + (int32_t)x * 48 + (44 - w) / 2, 150, w, 5, mix565(COL[x], C_BLACK, fade));
        }
        if (t >= 72u) {
            const char *v = "MAIN MENU  LIVE PLAYER";
            cv_text(120 - text_w(&FONT_S, v) / 2, 172, &FONT_S, v, mix565(RGB(196, 196, 204), C_BLACK, fade));
        }
        cv_oy = 0;
        cv_blit(0, pass * 120u);
    }
    return 1;
}
