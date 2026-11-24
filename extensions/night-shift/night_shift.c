/* SPDX-License-Identifier: GPL-3.0-only */

/* Adapted from RepriseOS payload/night_shift.c (bf04e75) by kayboll.
 *
 * Night Shift warms the whole screen through the display pipe's per-channel
 * gamma tables, which the hardware applies to every pixel at no cost per frame;
 * the stock ramp is index * 4 in all three. The tables live on the heap only
 * while the screen is warm. Display init loads the identity ramp again after
 * each wake from a requested sleep; the hook hands it the current tables. */

#include "night_shift.h"
#include "ext.h"
#include "osos-2.0.5.h"
#include "schedule.h"
#include <stddef.h>

const char *const night_shift_labels[3] = {"Auto", "On", "Off"};

/* Per-channel gain /256 at full warmth: about the middle of iOS's range. */
static const uint16_t warm_gain[3] = {256, 216, 150};

enum { RAMP_MINUTES = 60, WARMEST = 256 };

static int mode = NIGHT_SHIFT_OFF;
static unsigned strength; /* what the tables hold, 0 to WARMEST */
static uint32_t *tables;  /* red, green, blue while strength is above 0 */

static void load(unsigned next)
{
    if (next == strength)
        return;
    if (!tables && !(tables = api->alloc(self, 3 * DisplayGamma_Entries * 4)))
        return;
    for (int channel = 0; channel < 3; channel++) {
        uint32_t gain = WARMEST - (WARMEST - warm_gain[channel]) * next / WARMEST;
        uint32_t *table = tables + channel * DisplayGamma_Entries;
        for (uint32_t i = 0; i < DisplayGamma_Entries; i++)
            table[i] = (i * 4 * gain) >> 8;
    }
    DisplayPipe_SetGammaTables(tables, tables + DisplayGamma_Entries,
                               tables + 2 * DisplayGamma_Entries);
    strength = next;
    if (!next) {
        api->free(self, tables);
        tables = NULL;
    }
}

unsigned night_shift_ramp(int now, int set, int rise)
{
    int night = schedule_minute(rise - set), since = schedule_minute(now - set),
        left = schedule_minute(rise - now);
    if (since >= night)
        return 0;
    int edge = since < left ? since : left;
    return edge >= RAMP_MINUTES ? WARMEST : (unsigned)(edge * WARMEST / RAMP_MINUTES);
}

void night_shift_follow(const struct schedule *s)
{
    if (mode != NIGHT_SHIFT_AUTO)
        return;
    switch (s->state) {
    case SCHEDULE_RISES:
        load(night_shift_ramp(s->now, s->set, s->rise));
        break;
    case SCHEDULE_NEVER_RISES:
        load(WARMEST);
        break;
    default:
        load(0);
        break;
    }
}

void night_shift_set(int value)
{
    struct schedule s;
    mode = value;
    if (mode == NIGHT_SHIFT_AUTO) {
        schedule_read(&s);
        night_shift_follow(&s);
    } else {
        load(mode == NIGHT_SHIFT_ON ? WARMEST : 0);
    }
}

void night_shift_stop(void)
{
    mode = NIGHT_SHIFT_OFF;
    load(0);
}

const char *night_shift_caption(void)
{
    if (mode == NIGHT_SHIFT_AUTO)
        return schedule_caption();
    return mode == NIGHT_SHIFT_ON ? "Warm colors" : "Standard colors";
}

uint32_t night_shift_init_gamma(uint32_t red, uint32_t green, uint32_t blue,
                                const void *next)
{
    if (tables) {
        red = (uint32_t)tables;
        green = (uint32_t)(tables + DisplayGamma_Entries);
        blue = (uint32_t)(tables + 2 * DisplayGamma_Entries);
    }
    return api->hook_next(next, red, green, blue, 0, 0, 0);
}
