/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test: cc -std=c11 -I$SDK test.c -o test && ./test */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osos-2.0.5.h"
#include "ext.h"

static uint32_t loaded[3][DisplayGamma_Entries];
static int loads;

int DisplayPipe_SetGammaTables(const uint32_t *r, const uint32_t *g, const uint32_t *b)
{
    memcpy(loaded[0], r, sizeof loaded[0]);
    memcpy(loaded[1], g, sizeof loaded[1]);
    memcpy(loaded[2], b, sizeof loaded[2]);
    loads++;
    return 0;
}

#include "dim.c"

static int live;

static void *allocate(rv_extension_handle e, uint32_t n)
{
    (void)e;
    live++;
    return calloc(1, n);
}

static void release(rv_extension_handle e, void *p)
{
    (void)e;
    live--;
    free(p);
}
static struct rv_api host;
const struct rv_api *api = &host;
rv_extension_handle self;

int main(void)
{
    host.alloc = allocate;
    host.free = release;

    /* Window 21:00 to 07:00, 30 %, 10-minute fades. */
    int s = 21 * 60, e = 7 * 60;
    assert(dim_scheduled(20 * 60 + 59, s, e, 30) == 0);
    assert(dim_scheduled(21 * 60, s, e, 30) == 0);
    assert(dim_scheduled(21 * 60 + 5, s, e, 30) == 15);
    assert(dim_scheduled(21 * 60 + 10, s, e, 30) == 30);
    assert(dim_scheduled(0, s, e, 30) == 30); /* across midnight */
    assert(dim_scheduled(6 * 60 + 55, s, e, 30) == 15);
    assert(dim_scheduled(7 * 60, s, e, 30) == 0);
    assert(dim_scheduled(12 * 60, s, e, 30) == 0);
    /* Same-day window and all-day. */
    assert(dim_scheduled(14 * 60, 13 * 60, 15 * 60, 50) == 50);
    assert(dim_scheduled(16 * 60, 13 * 60, 15 * 60, 50) == 0);
    assert(dim_scheduled(3 * 60, 9 * 60, 9 * 60, 40) == 40);

    /* Backlight: Apple at 26, target 3; full strength holds 3, half is between. */
    assert(dim_backlight(26, 3, 256) == 3);
    assert(dim_backlight(26, 3, 128) == 14 || dim_backlight(26, 3, 128) == 15);
    assert(dim_backlight(26, 3, 0) == 0);
    assert(dim_backlight(6, 1, 256) == 1);
    assert(dim_backlight(3, 5, 256) == 0); /* already below the target */
    assert(dim_backlight(6, 1, 10) == 0);  /* rounds to stock: release */
    assert(dim_fraction(22 * 60, 21 * 60, 7 * 60) == 256);
    assert(dim_fraction(12 * 60, 21 * 60, 7 * 60) == 0);

    assert(dim_gain(0) == 256);
    assert(dim_gain(50) == 128);
    assert(dim_gain(80) == 52);

    dim_apply(50);
    assert(loads == 1 && live == 1);
    assert(loaded[0][255] == (255 * 4 * 128) >> 8);
    assert(loaded[1][100] == loaded[0][100] && loaded[2][100] == loaded[0][100]);
    dim_apply(50);
    assert(loads == 1); /* unchanged: no reload */
    dim_apply(0);
    assert(loads == 2 && live == 0 &&
           loaded[0][255] == 255 * 4); /* stock ramp, freed */

    puts("night-light: all tests passed");
    return 0;
}
