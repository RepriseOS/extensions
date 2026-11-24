/* SPDX-License-Identifier: GPL-3.0-only */

/* Fixed-point steps of the filter designs Crossfeed and Surround make on the
 * pump task for whatever rate plays: no floating point and no 64-bit division
 * from the library. Static inline, so each design inlines them. */

#ifndef SHARED_Q31_H
#define SHARED_Q31_H
#include <stdint.h>

enum {
    Q31_TRIG_SHIFT = 30,        /* s1.30: sine and cosine, so 1.0 has room */
    Q31_QUARTER_PI = 421657428, /* pi / 4 in Q29 */
    /* 0.45 pi / 4 in Q31: a cutoff stays below 0.45 of the sample rate. */
    Q31_QUARTER_LIMIT = 758983371,
};

/* numerator / rate as Q31, for numerator < rate <= 65535, in 32-bit steps. */
static inline uint32_t q31_ratio(uint32_t numerator, uint32_t rate)
{
    uint32_t high = (numerator << 15) / rate;
    uint32_t remainder = (numerator << 15) % rate;
    return (high << 16) + (remainder << 16) / rate;
}

/* dividend / divisor by shift and subtract, for a quotient below 2^32. */
static inline uint32_t q31_divide(uint64_t dividend, uint32_t divisor)
{
    uint64_t remainder = dividend;
    uint32_t quotient = 0;
    for (int bit = 31; bit >= 0; bit--) {
        uint64_t term = (uint64_t)divisor << bit;
        if (remainder >= term) {
            remainder -= term;
            quotient |= 1u << bit;
        }
    }
    return quotient;
}

/* Sine and cosine of four times x, x in Q31 below 0.7 rad, as s1.30: the
 * series to the thirteenth power for x, then the angle doubled twice. */
static inline void q31_sin_cos(uint32_t x, int32_t *sine, int32_t *cosine)
{
    int32_t x2 = (int32_t)((int64_t)(int32_t)x * (int32_t)x >> 31); /* Q31 */
    int32_t s = (int32_t)(x >> 1), term = s;                        /* Q30 */
    int32_t c = 1 << Q31_TRIG_SHIFT, cterm = c;
    for (int n = 1; n <= 6; n++) {
        cterm = (int32_t)(-((int64_t)cterm * x2) >> 31) / ((2 * n - 1) * (2 * n));
        term = (int32_t)(-((int64_t)term * x2) >> 31) / ((2 * n) * (2 * n + 1));
        c += cterm;
        s += term;
    }
    for (int twice = 0; twice < 2; twice++) {
        int32_t s2 = (int32_t)((int64_t)s * c >> (Q31_TRIG_SHIFT - 1));
        c = (int32_t)(((int64_t)c * c - (int64_t)s * s) >> Q31_TRIG_SHIFT);
        s = s2;
    }
    *sine = s;
    *cosine = c;
}

#endif
