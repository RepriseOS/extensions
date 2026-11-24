/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef BALANCE_H
#define BALANCE_H

#include <stdint.h>

struct rv_api;

/* The setting is a position: BALANCE_CENTRE in the middle, BALANCE_DECIBELS
 * steps of a decibel to each side and one more that mutes the ear. */
enum {
    BALANCE_DECIBELS = 20,
    BALANCE_CENTRE = BALANCE_DECIBELS + 1,
    BALANCE_STEPS = 2 * BALANCE_CENTRE + 1,
};

/* The Q15 gain of the quieter ear `steps` from centre: unity at 0, a decibel
 * less per step, 0 past BALANCE_DECIBELS. */
int32_t balance_gain(unsigned int steps);

/* `publish` leaves the position for the pump task from any task; `process`
 * runs what plays on `frames` interleaved stereo S16 frames in place, on the
 * pump task only. */
void balance_start(const struct rv_api *core);
void balance_publish(unsigned int index);
void balance_process(int16_t *samples, uint32_t frames);

#endif
