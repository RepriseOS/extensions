/* SPDX-License-Identifier: GPL-3.0-only */

/* What the dsp library exports, and the state its limiter and fade run on.
 * An effect lists `requires = ["dsp"]` in its manifest; rv.mk finds the
 * library and the core fills in the addresses when it loads. No
 * RV_MODULE_CALL here: a call into a library is a long call. */

#ifndef RV_DSP_H
#define RV_DSP_H
#include "rv_audio.h"
#include <stdint.h>

/* Peaks over full scale, pulled down before they play instead of clipped. An
 * effect that can go over keeps its output wide, int32 at the S16 scale, and
 * the limiter plays it RV_LIMITER_FRAMES frames late. A sample over full scale
 * sets the gain that brings it to full scale; the gain ramps there linearly
 * over the frames before that sample plays, holds until the last over has
 * played, and returns to unity with a time constant of
 * 1 << RV_LIMITER_RELEASE_SHIFT frames. One gain serves both channels. At unity
 * the samples pass unchanged. A zeroed limiter is at rest. */
enum {
    RV_LIMITER_SHIFT = 5,
    RV_LIMITER_FRAMES = 1 << RV_LIMITER_SHIFT, /* about 0.7 ms at 44.1 kHz */
    RV_LIMITER_RELEASE_SHIFT = 12,             /* about 90 ms at 44.1 kHz */
    RV_LIMITER_INPUT = (1 << 19) - 1,          /* the widest sample taken: +24 dB */
    RV_LIMITER_CHUNK_FRAMES = 16,
};

#define RV_LIMITER_UNITY (1 << 30)

struct rv_limiter {
    int32_t delay[RV_LIMITER_FRAMES][2];
    uint32_t index;
    int32_t cut, target, slope; /* the gain below unity, Q30 */
    uint32_t hold;              /* frames until the last over has played */
};

static inline void rv_limiter_rest(struct rv_limiter *limiter)
{
    memset(limiter, 0, sizeof *limiter);
}

static inline int32_t rv_limiter_input(int32_t sample)
{
    return sample > RV_LIMITER_INPUT    ? RV_LIMITER_INPUT
           : sample < -RV_LIMITER_INPUT ? -RV_LIMITER_INPUT
                                        : sample;
}

/* Runs `state` on `frames` S16 frames from `in`, at most
 * RV_LIMITER_CHUNK_FRAMES, into wide frames in `out`. Returns 0, with `out`
 * untouched, for a state that passes the audio through unchanged (Off). */
typedef int (*rv_fade_wide_fn)(void *state, const int16_t *in, int32_t *out,
                               uint32_t frames);

/* The limiter and the fade behind it, on the pump task only: the scratch
 * buffers are the library's. Takes `frames` wide stereo frames and writes as
 * many S16 frames, delayed. The gain multiply stays 32-bit, a Q12 gain times
 * at most 2^19, and only an over divides. */
void rv_limiter_run(struct rv_limiter *limiter, const int32_t *in, int16_t *out,
                    uint32_t frames);

/* rv_fade_run for an effect behind a limiter. While both states run, their
 * wide outputs blend before the limiter, so the fade carries no delay of its
 * own. While one passes the audio through, the limited output of the other
 * blends with the undelayed buffer: an effect that turns on rests the limiter
 * and starts the fade with a lead of at least RV_LIMITER_FRAMES, so the blend
 * starts after the limiter has filled. Nothing runs per sample while
 * `playing` passes through and no fade lasts. */
void rv_fade_run_limited(struct rv_fade *fade, struct rv_limiter *limiter,
                         rv_fade_wide_fn run, void *playing, void *fading,
                         int16_t *samples, uint32_t frames);

/* Double precision without a math library, for filter designs and the curves
 * the editors draw: series that hold to double precision inside the stated
 * ranges. Soft float: for the UI task, never for a sample loop. */
double rv_exp(double x);                                 /* |x| below 1.5 */
double rv_log(double value);                             /* value above 0 */
void rv_sin_cos(double x, double *sine, double *cosine); /* 0 <= x < pi */
double rv_sqrt(double value); /* 0 for a value at or below 0 */
/* 10^(tenths / divisor): 200 turns tenths of a decibel into an amplitude,
 * 400 into the RBJ gain A. */
double rv_decibels(int tenths, unsigned int divisor);

/* Biquads as b0, b1, b2, a1, a2, signed with 24 fractional bits, for a sample
 * rate of 44100 or 48000 Hz. A cascade holds at most RV_DSP_STAGES of them,
 * the Equalizer's five bands; the response and peak searches keep that many
 * stages on the stack. */
enum { RV_DSP_STAGES = 5 };

enum rv_dsp_filter { RV_DSP_FLAT, RV_DSP_PEAKING, RV_DSP_LOW_SHELF, RV_DSP_HIGH_SHELF };

/* One RBJ biquad of `type` at `hz` with Q and gain in tenths, scaled by a precut
 * of 0..100 tenths of a decibel; a flat filter is the precut alone. Returns 0
 * when a value is out of range or the quantized filter is not stable. */
int rv_dsp_biquad(int32_t out[5], unsigned int type, unsigned int hz,
                  unsigned int q_tenths, int gain_tenths_db,
                  unsigned int precut_tenths_db, unsigned int rate);
/* Scales b0, b1, b2 by a precut of 0..100 tenths of a decibel. */
void rv_dsp_attenuate(int32_t coefficients[3], unsigned int precut_tenths_db);
/* The response of a quantized cascade of `count` biquads in tenths of a decibel
 * at `points` (at least 7) frequencies from 20 Hz to 20 kHz, evenly spaced in
 * octaves; all 0 for more than RV_DSP_STAGES. */
void rv_dsp_response(const int32_t (*coefficients)[5], unsigned int count,
                     unsigned int rate, int16_t *tenths, unsigned int points);
/* The peak of a quantized cascade of `count` biquads over 20 Hz to 20 kHz in
 * tenths of a decibel, rounded up; 0 for more than RV_DSP_STAGES. */
int rv_dsp_peak(const int32_t (*coefficients)[5], unsigned int count,
                unsigned int rate);

/* Rockbox's bishelf: a first-order low shelf of `low_gain` below `low_hz` and
 * a first-order high shelf of `high_gain` above `high_hz`, each at half its
 * gain at the cutoff, multiplied into one biquad and scaled by `gain`; all in
 * tenths of a decibel within +/-24 dB, same format and sample rates as
 * rv_dsp_biquad; two shelves of 0 dB are the gain alone. Returns 0 when a
 * value is out of range, a coefficient does not fit or the quantized filter
 * is not stable. */
int rv_dsp_bishelf(int32_t out[5], unsigned int low_hz, unsigned int high_hz,
                   int low_gain_tenths, int high_gain_tenths, int gain_tenths,
                   unsigned int rate);

/* One biquad of a cascade: coefficients with 24 fraction bits, a1 and a2
 * negated, and per channel x1, x2, y1, y2 and the error fed into the next
 * output. */
struct rv_dsp_stage {
    int32_t b0, b1, b2, a1, a2;
    int32_t history[2][4];
    int32_t error[2];
    uint32_t bypass;
};

/* A stage's coefficients from a design's b0, b1, b2, a1, a2. */
static inline void rv_dsp_stage_set(struct rv_dsp_stage *stage, const int32_t c[5])
{
    stage->b0 = c[0];
    stage->b1 = c[1];
    stage->b2 = c[2];
    stage->a1 = -c[3];
    stage->a2 = -c[4];
}

/* Runs the stages that are not bypassed over interleaved stereo S16 `in` into
 * `out` at the S16 scale, rounded and unsaturated, for the limiter. Samples
 * carry nine fraction bits between stages, 42 dB of headroom, so a boost does
 * not clip before a later cut. Pump task; ARM. */
void rv_dsp_cascade_run(struct rv_dsp_stage *stages, unsigned int count,
                        const int16_t *in, int32_t *out, uint32_t frames);
/* Histories at rest. */
void rv_dsp_cascade_rest(struct rv_dsp_stage *stages, unsigned int count);

#endif
