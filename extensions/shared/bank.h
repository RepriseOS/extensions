/* SPDX-License-Identifier: GPL-3.0-only */

/* An audio effect that is a cascade of BANK_STAGES biquads behind the dsp
 * library's limiter, shared as source by the effects that are one: their main
 * modules compile shared/bank.c with -DBANK_STAGES=n and require dsp. */

#ifndef SHARED_BANK_H
#define SHARED_BANK_H
#include "rv_dsp.h"

#ifndef BANK_STAGES
#error "set BANK_STAGES"
#endif
_Static_assert(BANK_STAGES <= RV_DSP_STAGES, "the cascade holds at most RV_DSP_STAGES");

/* The filters for both sample rates an effect designs for, as the UI task
 * leaves them for the pump task: bank 0 is 44.1 kHz, bank 1 48 kHz. */
enum { BANKS = 2 };

static inline unsigned int bank_rate(unsigned int bank)
{
    return bank ? 48000 : 44100;
}

/* What the settings say, designed on the UI task: the stages of each bank in
 * rv_dsp_biquad's format, a unity stage bypassed; `on` 0 passes the audio
 * through. */
struct bank_set {
    uint8_t on;
    int32_t coefficients[BANKS][BANK_STAGES][5];
};

/* An effect that is a cascade of the bank of the rate that plays, behind the
 * limiter: the extension keeps one, with `effect.api` set to the core's
 * table, publishes sets from any task and processes on the pump task. At
 * another rate the audio passes through. A new set at the same rate fades
 * from the old one and keeps the histories, except in a stage that was
 * bypassed; turning on or a new rate starts them all over. */
struct bank_state {
    uint32_t on;
    struct rv_dsp_stage stages[BANK_STAGES];
};

struct bank {
    struct rv_effect effect;
    struct rv_limiter limiter;
    struct bank_set next;
    struct bank_state playing, fading;
};

static inline void bank_publish(struct bank *bank, const struct bank_set *set)
{
    rv_effect_publish(&bank->effect, &bank->next, set, sizeof *set);
}

/* Runs `frames` interleaved stereo S16 frames at `rate` in place. Pump task. */
void bank_process(struct bank *bank, int16_t *samples, uint32_t frames, uint32_t rate);

#endif
