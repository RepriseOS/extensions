# SPDX-License-Identifier: GPL-3.0-only
"""The cascade and the limiter in Python integers, and the filters in floating
point, for the host tests of the library and of the effects that run through
them.

cookbook() and power() are adapted from RepriseOS
patching/tests/test_eq_coefficients.py (93f6a45) by lgreitis. bishelf()
follows Rockbox lib/rbcodec/dsp/dsp_filter.c filter_shelf_coefs and
filter_bishelf_coefs (Copyright (C) 2006-2007 Thom Johansen). Modified by the
RevolaOS authors, 2026. Rockbox is GPL-2.0-or-later; this version is
GPL-3.0-only.
"""

import cmath
import ctypes
import math

SCALE = 1 << 24
INT32_MIN, INT32_MAX = -(1 << 31), (1 << 31) - 1
HALF_STEP = 1 << 23


class Stage(ctypes.Structure):
    """struct rv_dsp_stage."""

    _fields_ = [
        ("b0", ctypes.c_int32),
        ("b1", ctypes.c_int32),
        ("b2", ctypes.c_int32),
        ("a1", ctypes.c_int32),
        ("a2", ctypes.c_int32),
        ("history", (ctypes.c_int32 * 4) * 2),
        ("error", ctypes.c_int32 * 2),
        ("bypass", ctypes.c_uint32),
    ]


def cascade_model(stages, samples):
    """rv_dsp_cascade_run in Python integers: nine fraction bits between stages,
    24 bits of error fed into the next output, saturation on overflow."""
    wide = [s * 512 for s in samples]
    for stage in stages:
        if stage.bypass:
            continue
        for channel in range(2):
            x1, x2, y1, y2 = stage.history[channel]
            e = stage.error[channel]
            for i in range(channel, len(wide), 2):
                x = wide[i]
                total = (
                    stage.b0 * x
                    + stage.b1 * x1
                    + stage.b2 * x2
                    + stage.a1 * y1
                    + stage.a2 * y2
                    + e
                )
                y = total >> 24
                e = total & 0xFFFFFF
                if y < INT32_MIN or y > INT32_MAX:
                    y = INT32_MIN if y < 0 else INT32_MAX
                    e = HALF_STEP
                x2, x1, y2, y1 = x1, x, y1, y
                wide[i] = y
            stage.history[channel][:] = [x1, x2, y1, y2]
            stage.error[channel] = e
    return [(w >> 9) + ((w >> 8) & 1) for w in wide]


def saturate(value):
    return max(-32768, min(32767, value))


class Limiter:
    """rv_limiter, integer for integer."""

    FRAMES, SHIFT, RELEASE, INPUT, UNITY = 32, 5, 12, (1 << 19) - 1, 1 << 30

    def __init__(self):
        self.rest()

    def rest(self):
        self.delay = [(0, 0)] * self.FRAMES
        self.index = self.cut = self.target = self.slope = self.hold = 0

    @classmethod
    def clamp(cls, value):
        return max(-cls.INPUT, min(cls.INPUT, value))

    def run(self, frame):
        left, right = (self.clamp(v) for v in frame)
        peak = max(abs(left), abs(right))
        if peak > 32767:
            need = self.UNITY - (((32767 << 15) // peak) << 15)
            if need > self.target:
                self.target = need
                steep = (need - self.cut + self.FRAMES - 1) >> self.SHIFT
                self.slope = max(self.slope, steep)
            self.hold = self.FRAMES + 1
        if self.hold:
            self.hold -= 1
            if self.cut < self.target:
                self.cut = min(self.cut + self.slope, self.target)
            else:
                self.slope = 0
        elif self.cut:
            self.target = self.slope = 0
            self.cut = max(0, self.cut - (self.cut >> self.RELEASE) - 1)
        delayed = self.delay[self.index]
        self.delay[self.index] = (left, right)
        self.index = (self.index + 1) % self.FRAMES
        if self.cut:
            gain = (self.UNITY - self.cut) >> 18
            delayed = tuple((v * gain + (1 << 11)) >> 12 for v in delayed)
        return tuple(saturate(v) for v in delayed)


def power(bank, hz, rate, unity=1):
    """The power gain at `hz` of a cascade of b0, b1, b2, a1, a2 rows over an a0
    of `unity`."""
    z = cmath.exp(-2j * math.pi * hz / rate)
    result = 1.0
    for b0, b1, b2, a1, a2 in bank:
        result *= abs((b0 + b1 * z + b2 * z * z) / (unity + a1 * z + a2 * z * z)) ** 2
    return result


def cookbook(kind, hz, q_tenths, gain, precut, rate):
    """rv_dsp_biquad's RBJ cookbook biquad, a bell (1), low shelf (2) or high
    shelf (3), as b0, b1, b2, a1, a2 over a0."""
    if gain == 0:
        return [10 ** (-precut / 200), 0, 0, 0, 0]
    w = 2 * math.pi * hz / rate
    c, alpha = math.cos(w), math.sin(w) * 5 / q_tenths
    a = 10 ** (gain / 400)
    beta = 2 * math.sqrt(a) * alpha
    if kind == 1:
        b = [1 + alpha * a, -2 * c, 1 - alpha * a]
        d = [1 + alpha / a, -2 * c, 1 - alpha / a]
    elif kind == 2:
        b = [
            a * (a + 1 - (a - 1) * c + beta),
            2 * a * (a - 1 - (a + 1) * c),
            a * (a + 1 - (a - 1) * c - beta),
        ]
        d = [
            a + 1 + (a - 1) * c + beta,
            -2 * (a - 1 + (a + 1) * c),
            a + 1 + (a - 1) * c - beta,
        ]
    else:
        b = [
            a * (a + 1 + (a - 1) * c + beta),
            -2 * a * (a - 1 + (a + 1) * c),
            a * (a + 1 + (a - 1) * c - beta),
        ]
        d = [
            a + 1 - (a - 1) * c + beta,
            2 * (a - 1 - (a + 1) * c),
            a + 1 - (a - 1) * c - beta,
        ]
    return [n * 10 ** (-precut / 200) / d[0] for n in b] + [n / d[0] for n in d[1:]]


def peaking(hz, q_tenths, gain, rate):
    """The cookbook bell without a precut."""
    return cookbook(1, hz, q_tenths, gain, 0, rate)


def bishelf(low_hz, high_hz, low, high, gain, rate):
    """Two bilinear first-order shelves multiplied into one biquad, as
    b0, b1, b2, a1, a2 over a0."""

    def shelf(hz, tenths, is_low):
        s, c = math.sin(math.pi * hz / rate), math.cos(math.pi * hz / rate)
        g = 10 ** (tenths / 400)
        if is_low:
            return [s * g + c, s * g - c], [s / g + c, s / g - c]
        return [s + c * g, s - c * g], [s + c / g, s - c / g]

    ln, ld = shelf(low_hz, low, True)
    hn, hd = shelf(high_hz, high, False)
    scale = 10 ** (gain / 200)
    if low == 0 and high == 0:
        return [scale, 0, 0, 0, 0]
    b = [ln[0] * hn[0], ln[0] * hn[1] + ln[1] * hn[0], ln[1] * hn[1]]
    d = [ld[0] * hd[0], ld[0] * hd[1] + ld[1] * hd[0], ld[1] * hd[1]]
    return [n * scale / d[0] for n in b] + [n / d[0] for n in d[1:]]
