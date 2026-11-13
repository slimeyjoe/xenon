#include <stdlib.h>
#include <string.h>
#include "ptest.h"

#define W XE_SCREEN_W
#define H XE_SCREEN_H
#define BPR XE_PLANE_STRIDE

static const struct pt_io *g_io;
static struct xe_planes g_pg[2];
static struct xe_rect g_dirty[2];
static int g_disp, g_switched, g_scroll, g_on, g_nsnap;
static uint8_t g_pal[48];
static uint32_t g_rng;

/* palettes: A distinct colours with black 0; B another set; R colour 0 red; white = fade white */
static const uint8_t PAL_A[48] = {
    0, 0, 0,   15, 15, 15,  15, 0, 0,   0, 15, 0,   0, 0, 15,   15, 15, 0,  0, 15, 15,  15, 0, 15,
    8, 8, 8,   8, 0, 0,     0, 8, 0,    0, 0, 8,    8, 8, 0,    0, 8, 8,    8, 0, 8,    4, 9, 14
};
static const uint8_t PAL_B[48] = {
    0, 0, 0,   1, 2, 3,     4, 5, 6,    7, 8, 9,    10, 11, 12, 13, 14, 15, 15, 7, 3,   3, 7, 15,
    12, 4, 1,  1, 12, 4,    4, 1, 12,   9, 9, 2,    2, 9, 9,    9, 2, 9,    14, 14, 6,  6, 6, 14
};

static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void add_dirty(int page, int x0, int y0, int x1, int y1)
{
    struct xe_rect *r = &g_dirty[page];
    if (x0 < r->x0) r->x0 = x0;
    if (y0 < r->y0) r->y0 = y0;
    if (x1 > r->x1) r->x1 = x1;
    if (y1 > r->y1) r->y1 = y1;
}

/* pseudo-random bytes in byte columns [x0,x1) x rows [y0,y1) of all planes */
static void draw(int page, int x0, int y0, int x1, int y1, uint32_t seed)
{
    int p, y, x;
    g_rng = seed * 2654435761u + 1u;
    for (p = 0; p < 4; p++)
        for (y = y0; y < y1; y++)
            for (x = x0; x < x1; x++)
                g_pg[page].p[p][y * BPR + x] = (uint8_t)(rnd() >> 11);
    add_dirty(page, x0, y0, x1, y1);
}

/* colour index c at pixel (x, y) */
static void put(int page, int x, int y, int c)
{
    int p, off = y * BPR + x / 8;
    uint8_t bit = (uint8_t)(0x80u >> (x & 7));
    for (p = 0; p < 4; p++) {
        if (c & (1 << p)) g_pg[page].p[p][off] |= bit;
        else g_pg[page].p[p][off] &= (uint8_t)~bit;
    }
}

/* registration image: black, a one-pixel white frame on the outermost pixels, and corner
 * blocks of colours 2..5 (top left, top right, bottom left, bottom right) */
static void draw_registration(int page)
{
    int p, x, y;
    for (p = 0; p < 4; p++)
        memset(g_pg[page].p[p], 0, XE_PLANE_BYTES);
    for (x = 0; x < W; x++) {
        put(page, x, 0, 1);
        put(page, x, H - 1, 1);
    }
    for (y = 0; y < H; y++) {
        put(page, 0, y, 1);
        put(page, W - 1, y, 1);
    }
    for (y = 4; y < 20; y++)
        for (x = 4; x < 20; x++) {
            put(page, x, y, 2);
            put(page, W - 1 - x, y, 3);
            put(page, x, H - 1 - y, 4);
            put(page, W - 1 - x, H - 1 - y, 5);
        }
    add_dirty(page, 0, 0, BPR, H);
}

static void present(void)
{
    g_io->present(&g_pg[g_disp], &g_dirty[g_disp], g_switched, g_scroll, g_pal, g_on);
    xe_rect_clear(&g_dirty[g_disp]);
    g_switched = 0;
}

static void flip(int page)
{
    if (page != g_disp) {
        g_disp = page;
        g_switched = 1;
    }
}

static void snap(const char *name)
{
    g_io->snap(g_nsnap, name, &g_pg[g_disp], g_scroll, g_pal, g_on,
               xe_frame_hash32(&g_pg[g_disp], g_scroll, g_pal, g_on));
    g_nsnap++;
}

uint32_t xe_frame_hash32(const struct xe_planes *page, int scroll, const uint8_t rgb4[48], int on)
{
    uint32_t h = 2166136261u;
    int p, y, x;
    for (y = 0; y < H; y++) {
        int sy = (y + scroll) % H;
        for (p = 0; p < 4; p++) {
            const uint8_t *row = page->p[p] + sy * BPR;
            for (x = 0; x < BPR; x++)
                h = (h ^ row[x]) * 16777619u;
        }
    }
    for (x = 0; x < 48; x++)
        h = (h ^ rgb4[x]) * 16777619u;
    h = (h ^ (uint32_t)(on ? 1 : 0)) * 16777619u;
    return h;
}

int pt_run(const struct pt_io *io)
{
    int i, p;
    uint8_t *mem = malloc(2 * 4 * XE_PLANE_BYTES);
    if (!mem)
        return -1;
    memset(mem, 0, 2 * 4 * XE_PLANE_BYTES);
    for (i = 0; i < 2; i++)
        for (p = 0; p < 4; p++)
            g_pg[i].p[p] = mem + (i * 4 + p) * XE_PLANE_BYTES;
    g_io = io;
    g_nsnap = 0;
    g_disp = 0;
    g_switched = 0;
    g_scroll = 0;
    g_on = 1;
    xe_rect_clear(&g_dirty[0]);
    xe_rect_clear(&g_dirty[1]);
    memcpy(g_pal, PAL_A, 48);

    /* first upload */
    draw_registration(0);
    present();
    snap("registration");

    draw(0, 0, 0, BPR, H, 1);
    present();
    snap("full");

    draw(0, 3, 13, 8, 58, 2);                   /* odd byte columns and rows */
    present();
    snap("dirty-odd");

    draw(0, 79, 0, 80, 1, 3);                   /* the four edges, one present each */
    present();
    draw(0, 0, 399, 1, 400, 4);
    present();
    draw(0, 0, 0, 80, 2, 5);
    present();
    draw(0, 77, 150, 80, 400, 6);
    present();
    snap("dirty-edges");

    memcpy(g_pal, PAL_B, 48);                   /* palette only, empty dirty rectangle */
    present();
    snap("palette");

    g_on = 0;
    present();
    snap("graphics-off");

    g_on = 1;
    draw(0, 10, 100, 30, 140, 7);
    present();
    snap("graphics-on-dirty");

    draw(1, 0, 0, BPR, H, 8);                   /* page 1 drawn while hidden, first flip */
    flip(1);
    present();
    snap("flip-1");

    draw(1, 20, 200, 60, 260, 9);
    present();
    snap("flip-1-dirty");

    draw(0, 40, 0, 80, 100, 10);                /* page 0 changed while hidden */
    flip(0);
    present();
    snap("flip-0");

    g_scroll = 40;
    present();
    snap("scroll-40");

    draw(0, 5, 100, 25, 140, 11);               /* redraws while scrolled, scroll unchanged */
    present();
    draw(0, 50, 380, 70, 400, 12);              /* rows that wrap to the top of the screen */
    present();
    draw(0, 60, 0, 70, 10, 13);
    present();
    draw(0, 30, 30, 34, 50, 14);                /* rows that straddle the wrap (display 390..) */
    present();
    snap("scroll-40-dirty");

    g_scroll = 360;
    present();
    snap("scroll-360");

    draw(0, 0, 350, 80, 370, 15);               /* straddles display row 0 */
    present();
    draw(0, 33, 395, 34, 400, 16);
    present();
    snap("scroll-360-dirty");

    draw(1, 0, 0, BPR, H, 17);                  /* flip while scrolled */
    flip(1);
    present();
    snap("flip-scrolled");

    draw(1, 70, 300, 80, 399, 18);              /* redraw in the other buffer, still scrolled */
    present();
    snap("flip-scrolled-dirty");

    g_scroll = 0;
    draw(1, 1, 1, 2, 2, 19);
    present();
    snap("unscroll");

    memcpy(g_pal, PAL_A, 48);                   /* colour 0 non-black: border must stay black */
    g_pal[0] = 15;
    g_pal[1] = 0;
    g_pal[2] = 0;
    present();
    snap("border-red");

    memset(g_pal, 15, 48);                      /* fade to white */
    present();
    snap("fade-white");

    g_on = 0;
    present();
    snap("fade-white-off");

    g_on = 1;                                   /* three flips in a row, one snapshot */
    memcpy(g_pal, PAL_A, 48);
    draw(0, 0, 0, BPR, H, 20);
    flip(0);
    present();
    g_scroll = 123;
    draw(1, 10, 10, 20, 20, 21);
    flip(1);
    present();
    flip(0);
    present();
    snap("flip-flip-flip");

    /* soak: random redraws, scrolls and flips */
    g_rng = 0x5eed;
    for (i = 0; i < 200; i++) {
        uint32_t r = rnd();
        int page = (r & 0x30) == 0x30 ? 1 - g_disp : g_disp;
        int x0 = (int)((r >> 8) % BPR), y0 = (int)((r >> 16) % H);
        int x1 = x0 + 1 + (int)((r >> 4) % (uint32_t)(BPR - x0));
        int y1 = y0 + 1 + (int)((r >> 20) % (uint32_t)(H - y0) / 4u);
        uint32_t saved = g_rng;
        draw(page, x0, y0, x1, y1, r);
        g_rng = saved;
        if ((r & 0x700) == 0x700)
            g_scroll = (int)((r >> 12) % H);
        else if ((r & 0x700) == 0x600)
            g_scroll = 0;
        if ((r & 0x3000) == 0x3000)
            flip(1 - g_disp);
        if ((r & 0xC000) == 0xC000)
            g_pal[(r >> 24) % 48] = (uint8_t)((r >> 28) & 15);
        present();
        if (i % 50 == 49) {
            static const char *const names[] = { "soak-1", "soak-2", "soak-3", "soak-4" };
            snap(names[i / 50]);
        }
    }

    free(mem);
    return g_nsnap;
}
