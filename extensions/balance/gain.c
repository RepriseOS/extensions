/* SPDX-License-Identifier: GPL-3.0-only */

#include "balance.h"

/* 32768 / 10^(k / 20) for k decibels, rounded, then the mute. */
static const uint16_t attenuation[BALANCE_DECIBELS + 2] = {
    32768, 29205, 26029, 23198, 20675, 18427, 16423, 14637, 13045, 11627, 10362,
    9235,  8231,  7336,  6538,  5827,  5193,  4629,  4125,  3677,  3277,  0,
};

int32_t balance_gain(unsigned int steps)
{
    return attenuation[steps > BALANCE_DECIBELS + 1 ? BALANCE_DECIBELS + 1 : steps];
}
