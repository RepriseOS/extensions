/* SPDX-License-Identifier: GPL-3.0-only */

/* Night Light dims the whole picture through the display pipe's per-channel
 * gamma tables, the same mechanism RepriseOS's Night Shift uses for warmth.
 * The stock ramp is index * 4 in all three channels; scaling all three
 * equally lowers the light every pixel lets through on top of whatever the
 * backlight is set to, so it stacks below the lowest stock brightness.
 * The backlight itself is untouched: blacks stay as they are. */

#include "dim.h"
#include "ext.h"
#include "osos-2.0.5.h"
#include <stddef.h>

static unsigned current; /* percent the tables hold */
static uint32_t *tables; /* red, green, blue while current is above 0 */

static int wrap(int minutes)
{
    minutes %= 1440;
    return minutes < 0 ? minutes + 1440 : minutes;
}

unsigned dim_scheduled(int now, int start, int end, unsigned percent)
{
    int length = wrap(end - start);
    if (length == 0)
        return percent; /* all day */
    int since = wrap(now - start);
    if (since >= length)
        return 0;
    int left = length - since;
    int edge = since < left ? since : left;
    if (edge >= DIM_FADE_MINUTES)
        return percent;
    return percent * (unsigned)edge / DIM_FADE_MINUTES;
}

unsigned dim_fraction(int now, int start, int end)
{
    return dim_scheduled(now, start, end, 256);
}

int dim_backlight(int stock, int target, unsigned strength)
{
    if (target < 1 || stock <= target || strength == 0)
        return 0;
    int level = stock - (int)(((unsigned)(stock - target) * strength + 128) / 256);
    if (level >= stock)
        return 0;
    return level < 1 ? 1 : level;
}

uint32_t dim_gain(unsigned percent)
{
    if (percent > 100)
        percent = 100;
    return 256 - percent * 256 / 100;
}

unsigned dim_current(void)
{
    return current;
}

void dim_apply(unsigned percent)
{
    if (percent == current)
        return;
    if (!tables && !(tables = api->alloc(self, 3 * DisplayGamma_Entries * 4)))
        return;
    uint32_t gain = dim_gain(percent);
    for (uint32_t i = 0; i < DisplayGamma_Entries; i++)
        tables[i] = (i * 4 * gain) >> 8;
    /* Same curve in all three channels. */
    for (uint32_t i = 0; i < DisplayGamma_Entries; i++) {
        tables[DisplayGamma_Entries + i] = tables[i];
        tables[2 * DisplayGamma_Entries + i] = tables[i];
    }
    DisplayPipe_SetGammaTables(tables, tables + DisplayGamma_Entries,
                               tables + 2 * DisplayGamma_Entries);
    current = percent;
    if (!percent) {
        api->free(self, tables);
        tables = NULL;
    }
}

uint32_t dim_init_gamma(uint32_t red, uint32_t green, uint32_t blue, const void *next)
{
    if (tables) {
        red = (uint32_t)tables;
        green = (uint32_t)(tables + DisplayGamma_Entries);
        blue = (uint32_t)(tables + 2 * DisplayGamma_Entries);
    }
    return api->hook_next(next, red, green, blue, 0, 0, 0);
}
