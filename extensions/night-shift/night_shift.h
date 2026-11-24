/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef NIGHT_SHIFT_H
#define NIGHT_SHIFT_H
#include <stdint.h>

struct schedule;

/* Night Shift: Auto by the schedule, warming over the first hour of the night
 * and cooling over the last; On; or Off. */
enum { NIGHT_SHIFT_AUTO, NIGHT_SHIFT_ON, NIGHT_SHIFT_OFF };

extern const char *const night_shift_labels[3];
void night_shift_set(int mode);
/* In Auto, the warmth for `s`; the main module calls it once a minute. */
void night_shift_follow(const struct schedule *s);
/* Back to the stock ramp, at unload. */
void night_shift_stop(void);
const char *night_shift_caption(void);
/* (red, green, blue): display init loading the identity ramp. */
uint32_t night_shift_init_gamma(uint32_t red, uint32_t green, uint32_t blue,
                                const void *next);
/* 0 (neutral) to 256 (full warmth) at local minute `now` for a night from
 * `set` to `rise`, all in minutes after midnight. */
unsigned night_shift_ramp(int now, int set, int rise);

#endif
