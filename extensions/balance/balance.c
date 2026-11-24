/* SPDX-License-Identifier: GPL-3.0-only */

/* Balance on the native interleaved S16 output, in place: the quieter ear is
 * turned down by a whole number of decibels, or muted at the end of the
 * range; the other ear is left alone. Nothing runs per sample at centre.
 *
 * The UI task leaves the position with interrupts masked and moves the
 * generation; the pump task takes it over when it sees the generation move and
 * fades the gain that played into the new one. Only the pump task touches what
 * plays. A gain of at most 32768 times an S16 sample fits int32, so Thumb
 * multiplies suffice, and the output cannot leave S16. */

#include "balance.h"
#include "rv_audio.h"

static const struct rv_api *api;

static uint8_t next;
static struct rv_handoff handoff;

/* Pump task only: which channel is turned down, by how much. */
struct balance_state {
    uint8_t quiet; /* 0 left, 1 right */
    int32_t gain;  /* Q15, 32768 at centre */
};
static struct balance_state playing = {0, 32768}, fading;
static struct rv_fade fade;

void balance_start(const struct rv_api *core)
{
    api = core;
}

void balance_publish(unsigned int index)
{
    uint8_t set = (uint8_t)(index >= BALANCE_STEPS ? BALANCE_CENTRE : index);
    rv_handoff_publish(api, &handoff, &next, &set, sizeof set);
}

/* Takes over the index the UI task left. Left of centre the right ear is
 * turned down, right of centre the left. */
static void take(void)
{
    uint8_t index;
    fading = playing;
    rv_handoff_take(&handoff, &index, &next, sizeof index);
    unsigned int left_of_centre = index < BALANCE_CENTRE;
    playing.quiet = (uint8_t)left_of_centre;
    playing.gain =
        balance_gain(left_of_centre ? BALANCE_CENTRE - index : index - BALANCE_CENTRE);
    rv_fade_start(&fade, 0);
}

static void run(void *state, int16_t *samples, uint32_t frames)
{
    const struct balance_state *s = state;
    const int32_t g = s->gain;
    if (g == 32768)
        return;
    for (int16_t *sample = samples + s->quiet, *end = samples + frames * 2;
         sample < end; sample += 2)
        *sample = (int16_t)((*sample * g + 16384) >> 15);
}

void balance_process(int16_t *samples, uint32_t frames)
{
    if (rv_handoff_pending(&handoff))
        take();
    rv_fade_run(&fade, run, &playing, &fading, samples, frames);
}
