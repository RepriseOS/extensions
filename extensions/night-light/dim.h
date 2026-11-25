/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef NIGHT_LIGHT_DIM_H
#define NIGHT_LIGHT_DIM_H
#include <stdint.h>

/* Minutes over which a scheduled window fades in and out. */
enum { DIM_FADE_MINUTES = 10 };

/* Dimming in percent (0 = stock, 80 = 20 % of stock light) that a scheduled
 * window from `start` to `end` gives at local minute `now`, all in minutes
 * after midnight. A window may cross midnight; start == end means all day.
 * Fades linearly over DIM_FADE_MINUTES at each edge. */
unsigned dim_scheduled(int now, int start, int end, unsigned percent);
/* The same window as a strength from 0 (outside) to 256 (fully in). */
unsigned dim_fraction(int now, int start, int end);
/* Backlight level for `strength` (0..256) between Apple's `stock` level and
 * `target` (1..5); 0 when nothing below stock is needed. */
int dim_backlight(int stock, int target, unsigned strength);

/* Channel gain /256 for `percent` dimming. */
uint32_t dim_gain(unsigned percent);

/* Loads gamma tables for `percent` dimming; 0 restores stock and frees them. */
void dim_apply(unsigned percent);
unsigned dim_current(void);

/* Filter for the display init that reloads the identity ramp after a wake. */
uint32_t dim_init_gamma(uint32_t red, uint32_t green, uint32_t blue, const void *next);

#endif
