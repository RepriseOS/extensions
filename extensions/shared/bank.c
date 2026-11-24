/* SPDX-License-Identifier: GPL-3.0-only */

/* A cascade effect on the native interleaved S16 output, behind the limiter.
 *
 * The UI task leaves a set with interrupts masked and moves the generation;
 * the pump task takes it over when it sees the generation move and fades the
 * state that played into the new one. Only the pump task touches what plays. */

#include "bank.h"

static int unity(const int32_t c[5])
{
    return c[0] == 1 << 24 && !(c[1] | c[2] | c[3] | c[4]);
}

/* A set left meanwhile moves the generation again, and the next call takes
 * that one; a set left while an on/off fade lasts waits for its end. */
static void take(struct bank *bank, uint32_t rate)
{
    struct bank_state *playing = &bank->playing;
    struct bank_set set;
    unsigned int was = playing->on, same_rate = rate == bank->effect.rate;
    if (same_rate)
        bank->fading = *playing;
    rv_handoff_take(&bank->effect.handoff, &set, &bank->next, sizeof set);
    playing->on = set.on && (rate == bank_rate(0) || rate == bank_rate(1));
    for (unsigned int i = 0; i < BANK_STAGES; i++) {
        const int32_t *c = set.coefficients[rate == bank_rate(1)][i];
        struct rv_dsp_stage *stage = &playing->stages[i];
        unsigned int bypassed = stage->bypass;
        rv_dsp_stage_set(stage, c);
        stage->bypass = unity(c);
        /* A stage that sat out the last set holds its history from the one
         * before; it starts over instead. */
        if (bypassed && !stage->bypass)
            rv_dsp_cascade_rest(stage, 1);
    }
    if (!was || !same_rate)
        rv_dsp_cascade_rest(playing->stages, BANK_STAGES);
    /* Turning on, the limiter starts empty and fills while the old state plays
     * alone; turning on with a new rate has no fade, so its first 32 frames are
     * silence. A new rate alone keeps what the limiter holds, which plays out. */
    int turns_on = playing->on && !was;
    if (turns_on)
        rv_limiter_rest(&bank->limiter);
    rv_effect_taken(&bank->effect, rate, turns_on ? RV_LIMITER_FRAMES : 0);
}

static int run(void *state, const int16_t *in, int32_t *out, uint32_t frames)
{
    struct bank_state *s = state;
    if (!s->on)
        return 0;
    rv_dsp_cascade_run(s->stages, BANK_STAGES, in, out, frames);
    return 1;
}

void bank_process(struct bank *bank, int16_t *samples, uint32_t frames, uint32_t rate)
{
    if (rv_effect_pending(&bank->effect, rate))
        take(bank, rate);
    rv_fade_run_limited(&bank->effect.fade, &bank->limiter, run, &bank->playing,
                        &bank->fading, samples, frames);
}
