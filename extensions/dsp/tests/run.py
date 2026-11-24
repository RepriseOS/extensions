#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the host tests with a staged SDK: python3 tests/run.py SDK."""

import ctypes
import math
import subprocess
import sys
import tempfile
from itertools import pairwise
from pathlib import Path

root = Path(__file__).resolve().parent
shared = root.parents[1] / "shared"
sys.path.insert(0, str(root))
from harness import FLAGS, build, noise  # noqa: E402
from model import (  # noqa: E402
    HALF_STEP,
    INT32_MAX,
    INT32_MIN,
    SCALE,
    Limiter,
    Stage,
    bishelf,
    cascade_model,
    cookbook,
    peaking,
    power,
)

RATES = (44100, 48000)
Biquad = ctypes.c_int32 * 5


def run_audio_test(work):
    """The limiter and the fade behind it, in tests/audio_test.c."""
    program = work / "audio_test"
    subprocess.run(
        ["cc", *FLAGS, "-fsanitize=address,undefined", f"-I{sdk}", f"-I{root.parent}"]
        + [str(root / "audio_test.c"), str(root.parent / "limiter.c")]
        + ["-o", str(program)],
        check=True,
    )
    subprocess.run([str(program)], check=True)


def close(actual, expected, delta):
    assert abs(actual - expected) <= delta, (actual, expected, delta)


def bishelf_tests(dll):
    fn = dll.rv_dsp_bishelf
    fn.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_uint, ctypes.c_uint]
    fn.argtypes += [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
    # Rockbox's fatigue reduction and bass enhancement shelves, and the limits.
    cases = [
        (8000, 20000, 0, -14, 0),
        (8000, 20000, 0, -36, 0),
        (200, 1280, -5, 0, 0),
        (20, 16000, 0, 53, -5),
        (20, 16000, 0, 53, -50),
        (20, 20000, 240, 240, 240),
        (20, 20000, -240, -240, -240),
        (1000, 1000, 120, -120, 0),
        (60, 100, 0, 0, 0),
    ]
    for rate in RATES:
        for case in cases:
            out = Biquad()
            assert fn(out, *case, rate) == 1, case
            expected = bishelf(*case, rate)
            for a, b in zip(out, expected, strict=True):
                close(a / SCALE, b, 2 / SCALE)
            _, _, low, high, gain = case
            # The low shelf's full gain at DC, the high shelf's at Nyquist, of
            # the design: quantized, a 20 Hz shelf's poles sit too close to 1
            # for the ratio to hold there.
            close(100 * math.log10(power([expected], 1e-6, rate)), low + gain, 0.01)
            close(
                100 * math.log10(power([expected], rate / 2, rate)), high + gain, 0.01
            )
    out = Biquad()
    for bad in (
        (19, 20000, 0, 0, 0, 44100),
        (20, 20001, 0, 0, 0, 44100),
        (20, 20000, -241, 0, 0, 44100),
        (20, 20000, 0, 241, 0, 44100),
        (20, 20000, 0, 0, -241, 44100),
        (20, 20000, 0, 0, 0, 32000),
    ):
        assert fn(out, *bad) == 0, bad


def cascade_tests(dll):
    run = dll.rv_dsp_cascade_run
    run.argtypes = [
        ctypes.POINTER(Stage),
        ctypes.c_uint,
        ctypes.POINTER(ctypes.c_int16),
    ]
    run.argtypes += [ctypes.POINTER(ctypes.c_int32), ctypes.c_uint]
    run.restype = None
    rest = dll.rv_dsp_cascade_rest
    rest.argtypes = [ctypes.POINTER(Stage), ctypes.c_uint]
    rest.restype = None
    biquad = dll.rv_dsp_biquad
    assert ctypes.sizeof(Stage) == 64

    def stage(coefficients, bypass=0):
        b0, b1, b2, a1, a2 = coefficients
        return Stage(b0, b1, b2, -a1, -a2, bypass=bypass)

    def copy(stages):
        copied = (Stage * len(stages))()
        for i, s in enumerate(stages):
            ctypes.memmove(
                ctypes.byref(copied[i]), ctypes.byref(s), ctypes.sizeof(Stage)
            )
        return copied

    def signal(frames, seed, amplitude=32767):
        values, state = [], seed
        for _ in range(frames * 2):
            state = (state * 1103515245 + 12345) & 0x7FFFFFFF
            values.append(state % (2 * amplitude + 1) - amplitude)
        return values

    rested = (Stage * 2)()
    rest(rested, 2)
    for s in rested:
        assert list(s.error) == [HALF_STEP, HALF_STEP]
        assert all(v == 0 for row in s.history for v in row)

    # A +12 dB low shelf, a -12 dB bell and a +12 dB high shelf against the
    # model across blocks, with a flat stage bypassed and one not: the
    # history carries from block to block either way.
    bank = (Biquad * 4)()
    assert biquad(bank[0], 2, 80, 7, 120, 0, 44100) == 1
    assert biquad(bank[1], 1, 1000, 20, -120, 0, 44100) == 1
    assert biquad(bank[2], 3, 8000, 7, 120, 0, 44100) == 1
    assert biquad(bank[3], 0, 80, 7, 0, 0, 44100) == 1
    for bypass_flat in (0, 1):
        stages = (Stage * 4)(*[stage(list(bank[i])) for i in range(4)])
        stages[3].bypass = bypass_flat
        rest(stages, 4)
        model = copy(stages)
        for block, frames in enumerate((1, 16, 128, 33)):
            samples = signal(frames, block + 1)
            data = (ctypes.c_int16 * len(samples))(*samples)
            out = (ctypes.c_int32 * len(samples))()
            run(stages, 4, data, out, frames)
            assert list(out) == cascade_model(model, samples), (bypass_flat, block)
        for i in range(4):
            assert list(stages[i].error) == list(model[i].error)
            assert [list(r) for r in stages[i].history] == [
                list(r) for r in model[i].history
            ]

    # A unity cascade is the input, and a bypassed boost leaves it alone.
    samples = signal(64, 7)
    data = (ctypes.c_int16 * len(samples))(*samples)
    out = (ctypes.c_int32 * len(samples))()
    stages = (Stage * 2)(stage([SCALE, 0, 0, 0, 0]), stage(list(bank[0]), bypass=1))
    rest(stages, 2)
    run(stages, 2, data, out, 64)
    assert list(out) == samples

    # An integrator (y = x + y1) on full-scale DC overflows a stage after 128
    # frames: the output saturates and the error restarts, as the model says.
    stages = (Stage * 1)(stage([SCALE, 0, 0, -SCALE, 0]))
    rest(stages, 1)
    model = copy(stages)
    samples = [32767, -32768] * 256
    data = (ctypes.c_int16 * len(samples))(*samples)
    out = (ctypes.c_int32 * len(samples))()
    run(stages, 1, data, out, 256)
    assert list(out) == cascade_model(model, samples)
    assert max(out) == (INT32_MAX >> 9) + 1 and min(out) == INT32_MIN >> 9
    assert list(stages[0].error) == [HALF_STEP, HALF_STEP]


def math_tests(dll):
    exp, log, sqrt = dll.rv_exp, dll.rv_log, dll.rv_sqrt
    for fn in (exp, log, sqrt):
        fn.argtypes = [ctypes.c_double]
        fn.restype = ctypes.c_double
    sin_cos = dll.rv_sin_cos
    sin_cos.argtypes = [
        ctypes.c_double,
        ctypes.POINTER(ctypes.c_double),
        ctypes.POINTER(ctypes.c_double),
    ]
    sin_cos.restype = None
    decibels = dll.rv_decibels
    decibels.argtypes = [ctypes.c_int, ctypes.c_uint]
    decibels.restype = ctypes.c_double
    for x in (-1.4, -0.5, 0, 0.3, 1.4):
        close(exp(x), math.exp(x), 1e-12)
    for v in (1e-6, 0.5, 1, 2.5, 1000, 6e4):
        close(log(v), math.log(v), 1e-12)
        close(sqrt(v), math.sqrt(v), 1e-9 * math.sqrt(v))
    assert sqrt(0) == 0 and sqrt(-1) == 0
    s, c = ctypes.c_double(), ctypes.c_double()
    for x in (0, 0.1, 1, 2, 3.1):
        sin_cos(x, s, c)
        close(s.value, math.sin(x), 1e-12)
        close(c.value, math.cos(x), 1e-12)
    for tenths, divisor in ((60, 200), (-120, 200), (120, 400), (37, 800)):
        close(decibels(tenths, divisor), 10 ** (tenths / divisor), 1e-12)


def biquad_tests(dll):
    biquad = dll.rv_dsp_biquad
    biquad.argtypes = [ctypes.POINTER(ctypes.c_int32)] + [ctypes.c_uint] * 3
    biquad.argtypes += [ctypes.c_int, ctypes.c_uint, ctypes.c_uint]
    response = dll.rv_dsp_response
    response.argtypes = [ctypes.POINTER(Biquad), ctypes.c_uint, ctypes.c_uint]
    response.argtypes += [ctypes.POINTER(ctypes.c_int16), ctypes.c_uint]
    response.restype = None
    peak = dll.rv_dsp_peak
    peak.argtypes = [ctypes.POINTER(Biquad), ctypes.c_uint, ctypes.c_uint]
    attenuate = dll.rv_dsp_attenuate
    attenuate.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_uint]
    attenuate.restype = None

    for rate in RATES:
        # A flat band is the precut alone, in steps of a tenth of a decibel.
        previous = SCALE + 1
        for precut in range(101):
            out = Biquad()
            assert biquad(out, 0, 60, 7, 0, precut, rate) == 1
            assert out[0] < previous
            close(out[0], SCALE * 10 ** (-precut / 200), 0.51)
            assert list(out)[1:] == [0, 0, 0, 0]
            previous = out[0]
        for kind in (1, 2, 3):
            for hz in (20, 1000, 20000):
                for gain in (-120, 0, 120):
                    for precut in (0, 1, 37, 100):
                        out = Biquad()
                        assert biquad(out, kind, hz, 7, gain, precut, rate) == 1
                        expected = cookbook(kind, hz, 7, gain, precut, rate)
                        for a, b in zip(out, expected, strict=True):
                            close(a / SCALE, b, 1 / SCALE)
    out = Biquad()
    assert biquad(out, 1, 7500, 50, -82, 0, 44100) == 1
    for a, b in zip(out, cookbook(1, 7500, 50, -82, 0, 44100), strict=True):
        close(a / SCALE, b, 1 / SCALE)
    for bad in (
        (4, 60, 7, 0, 0, 44100),
        (0, 19, 7, 0, 0, 44100),
        (0, 20001, 7, 0, 0, 44100),
        (0, 60, 2, 0, 0, 44100),
        (0, 60, 51, 0, 0, 44100),
        (0, 60, 7, -121, 0, 44100),
        (0, 60, 7, 121, 0, 44100),
        (0, 60, 7, 0, 101, 44100),
        (0, 60, 7, 0, 0, 32000),
    ):
        assert biquad(out, *bad) == 0, bad

    bands = ((2, 80), (1, 1000), (3, 16000), (1, 250), (1, 4000))
    for rate in RATES:
        for q in (3, 7, 40):
            for gain in (-120, 0, 120):
                bank = (Biquad * 5)()
                for band, (kind, hz) in enumerate(bands):
                    precut = 37 if band == 0 else 0
                    assert biquad(bank[band], kind, hz, q, gain, precut, rate) == 1
                for points in (7, 81, 261):
                    curve = (ctypes.c_int16 * points)()
                    response(bank, 5, rate, curve, points)
                    for i in range(points):
                        hz = 20 * 1000 ** (i / (points - 1))
                        expected = 100 * math.log10(power(bank, hz, rate, SCALE))
                        expected = max(-480, min(480, expected))
                        close(curve[i], expected, 0.52)
                # The response of the first `count` stages alone.
                curve = (ctypes.c_int16 * 7)()
                response(bank, 2, rate, curve, 7)
                for i in range(7):
                    hz = 20 * 1000 ** (i / 6)
                    expected = 100 * math.log10(power(list(bank)[:2], hz, rate, SCALE))
                    close(curve[i], max(-480, min(480, expected)), 0.52)

    def dense(bank, count, rate):
        stages = [list(bank[i]) for i in range(count)]
        points = (20 * 1000 ** (i / 6000) for i in range(6001))
        return 100 * math.log10(max(power(stages, hz, rate, SCALE) for hz in points))

    # Narrow bells near Nyquist, overlapping peaks, resonant shelves and cuts.
    cases = [
        [(1, 19000, 40, 120)],
        [(1, 16000, 40, 120), (1, 12500, 30, 60)],
        [(1, 1000, 7, 60), (1, 1250, 40, 40), (2, 80, 7, 30)],
        [(2, 1000, 40, 90), (3, 1000, 26, -60)],
        [(3, 8000, 7, 75), (1, 4000, 12, 50), (2, 60, 7, 75)],
        [(1, 250, 10, -120), (1, 4000, 10, -60)],
        [(1, 100 * (i + 1), 10, 30) for i in range(5)],
    ]
    for rate in RATES:
        for case in cases:
            bank = (Biquad * 5)()
            for i, (kind, hz, q, gain) in enumerate(case):
                assert biquad(bank[i], kind, hz, q, gain, 0, rate) == 1
            expected = dense(bank, len(case), rate)
            actual = peak(bank, len(case), rate)
            assert expected - 0.01 <= actual <= math.ceil(expected) + 1, (rate, case)
            # Precut rounds up, leaving the peak within 0.1 dB below 0 dB.
            if actual > 0:
                attenuate(bank[0], actual)
                assert peak(bank, len(case), rate) <= 0, (rate, case)
                assert dense(bank, len(case), rate) > -1.01, (rate, case)
    assert peak((Biquad * 6)(), 6, 44100) == 0
    curve = (ctypes.c_int16 * 7)(*([1] * 7))
    response((Biquad * 6)(), 6, 44100, curve, 7)
    assert list(curve) == [0] * 7

    coefficients = Biquad(SCALE, -SCALE // 2, 1000, 7, 9)
    attenuate(coefficients, 60)
    gain = 10 ** (-60 / 200)
    expected = [round(SCALE * gain), round(-SCALE // 2 * gain), round(1000 * gain)]
    assert list(coefficients) == expected + [7, 9]


FADE = 128


def designs(rate):
    """The cascades the bank tests play, five filters each, b0, b1, b2, a1, a2
    over a0; a bank of four stages takes the first four. The cuts deepen from
    Light to Deep at the same frequencies; Light leaves the fourth stage flat,
    so the bank bypasses it."""

    def cuts(high, bell, presence, low, air):
        return [
            bishelf(8000, 20000, 0, high, 0, rate),
            peaking(8000, 28, bell, rate),
            peaking(3150, 28, presence, rate),
            bishelf(200, 1280, low, 0, 0, rate),
            peaking(12000, 7, air, rate),
        ]

    return {
        "light": cuts(-14, -29, -13, 0, -10),
        "medium": cuts(-29, -58, -29, -5, -15),
        "deep": cuts(-36, -72, -48, -5, -30),
    }


def bank_tests(dll, stages):
    """shared/bank.c with `stages` stages against the cascade and limiter
    models, integer for integer."""

    class Set(ctypes.Structure):
        """struct bank_set."""

        _fields_ = [("on", ctypes.c_uint8), ("coefficients", Biquad * stages * 2)]

    publish = dll.test_publish
    publish.argtypes = [ctypes.POINTER(Set)]
    publish.restype = None
    process = dll.test_process
    process.argtypes = [ctypes.POINTER(ctypes.c_int16)] + [ctypes.c_uint32] * 2
    process.restype = None
    dll.test_irq_calls.restype = ctypes.c_uint32
    count = 512

    def make_set(name):
        """A set of the named design quantized to Q24; None is off, all stages
        unity."""
        chosen = Set(on=name is not None)
        chosen.name = name
        for bank, rate in enumerate(RATES):
            for i in range(stages):
                design = designs(rate)[name][i] if name else [1, 0, 0, 0, 0]
                chosen.coefficients[bank][i] = Biquad(
                    *(round(c * SCALE) for c in design)
                )
        return chosen

    def stages_of(chosen, rate):
        bank = chosen.coefficients[RATES.index(rate)]
        result = []
        for c in bank:
            b0, b1, b2, a1, a2 = c
            unity = b0 == SCALE and not (b1 or b2 or a1 or a2)
            result.append(Stage(b0, b1, b2, -a1, -a2, bypass=int(unity)))
        for s in result:
            s.error[:] = [HALF_STEP, HALF_STEP]
        return result

    class Player:
        """One instance as the engine keeps it: a set at a rate over the cascade."""

        def __init__(self, rate, chosen):
            self.rate, self.chosen = rate, chosen
            self.on = bool(chosen.on) and rate in RATES
            self.stages = stages_of(chosen, rate) if self.on else None

        def play(self, frames):
            flat = [s for frame in frames for s in frame]
            out = cascade_model(self.stages, flat)
            return [(out[2 * i], out[2 * i + 1]) for i in range(len(frames))]

    engine = {"new": Player(44100, make_set(None)), "old": None, "remaining": 0}
    engine["pending"] = None
    limiter = Limiter()

    def switching():
        old = engine["old"]
        return bool(
            engine["remaining"] and old is not None and old.on != engine["new"].on
        )

    def take(chosen, rate):
        """At the same rate the engine fades from what played; a set while on
        keeps the histories except in a stage that was bypassed; turning on
        rests the limiter and leads the fade by its delay; a new rate starts
        over without a fade."""
        old, new = engine["new"], Player(rate, chosen)
        if rate == old.rate and old.on and new.on:
            for mine, theirs in zip(new.stages, old.stages, strict=True):
                if theirs.bypass and not mine.bypass:
                    continue  # a stage that sat out starts over
                for ch in range(2):
                    mine.history[ch][:] = list(theirs.history[ch])
                    mine.error[ch] = theirs.error[ch]
        turns_on = new.on and not old.on
        if turns_on:
            limiter.rest()
        engine["new"] = new
        if rate == old.rate:
            engine["old"] = old
            engine["remaining"] = FADE + (Limiter.FRAMES if turns_on else 0)
        else:
            engine["old"], engine["remaining"] = None, 0

    def switch(chosen):
        publish(chosen)
        engine["pending"] = chosen

    def expected(frames, rate):
        pending = engine["pending"]
        if (pending is not None and not switching()) or rate != engine["new"].rate:
            take(pending if pending is not None else engine["new"].chosen, rate)
            engine["pending"] = None
        new, old = engine["new"], engine["old"]
        new_out = new.play(frames) if new.on else None
        running = old is not None and engine["remaining"] and old.on
        old_out = old.play(frames) if running else None
        out = []
        for i, frame in enumerate(frames):
            remaining = engine["remaining"]
            if not remaining:
                out.append(frame if new_out is None else limiter.run(new_out[i]))
                continue
            weight = 0 if remaining > FADE else FADE + 1 - remaining
            if old_out is not None and new_out is not None:
                mixed = tuple(
                    (Limiter.clamp(a) * (FADE - weight) + Limiter.clamp(b) * weight)
                    >> 7
                    for a, b in zip(old_out[i], new_out[i], strict=True)
                )
                out.append(limiter.run(mixed))
            elif old_out is not None or new_out is not None:
                alone = limiter.run(old_out[i] if old_out is not None else new_out[i])
                if old_out is not None:
                    weight = FADE - weight
                out.append(
                    tuple(
                        (x * (FADE - weight) + y * weight) >> 7
                        for x, y in zip(frame, alone, strict=True)
                    )
                )
            else:
                out.append(frame)
            engine["remaining"] -= 1
        if not engine["remaining"]:
            engine["old"] = None
        return out

    def run(frames, rate, *splits):
        buffer = (ctypes.c_int16 * (2 * len(frames)))(
            *[s for frame in frames for s in frame]
        )
        edges = [0, *splits, len(frames)]
        for first, last in pairwise(edges):
            process(
                ctypes.cast(ctypes.byref(buffer, 4 * first), process.argtypes[0]),
                last - first,
                rate,
            )
        return [(buffer[2 * i], buffer[2 * i + 1]) for i in range(len(frames))]

    def play(frames, rate, *splits):
        got = run(frames, rate, *splits)
        edges = [0, *splits, len(frames)]
        model = [f for a, b in pairwise(edges) for f in expected(frames[a:b], rate)]
        assert got == model, (stages, rate, splits)
        return got

    # Off leaves the stream alone.
    switch(make_set(None))
    frames = noise(count, 1)
    assert play(frames, 44100) == frames
    assert dll.test_irq_calls() == 2

    # Each set, bit for bit against the model, histories carried across blocks
    # and buffer edges, restarted by a rate change; a set changed while on
    # keeps them and fades.
    for seed, name in enumerate(("light", "medium", "deep"), 1):
        switch(make_set(name))
        for rate in RATES:
            for block in range(3):
                play(noise(count, block + rate + seed), rate, 17, 100)

    # A stage that sat out a set starts over when it comes back: Light
    # bypasses the fourth stage, Deep brings it back.
    for name, seed in (("deep", 73), ("light", 71), ("deep", 73)):
        switch(make_set(name))
        play(noise(count, seed), 44100)
    assert make_set("light").coefficients[0][3][:] == [SCALE, 0, 0, 0, 0]

    # At a rate without a design the audio passes through, even while on.
    switch(make_set("deep"))
    play(noise(count, 5), 44100)
    frames = noise(count, 6)
    assert play(frames, 32000) == frames
    frames = noise(count, 7)
    assert run(frames, 22050) == frames
    assert play(noise(count, 8), 44100) != noise(count, 8)

    # A set taken during a fade restarts the fade from the set that plays; a
    # set left while an on/off fade lasts waits for its end.
    switch(make_set(None))
    play(noise(count, 60), 44100)
    switch(make_set("light"))
    play(noise(50, 61), 44100)
    assert switching()
    switch(make_set("deep"))
    play(noise(count, 62), 44100, 40)
    assert engine["pending"] is not None and not switching()
    play(noise(count, 63), 44100)
    assert engine["pending"] is None and engine["new"].chosen.name == "deep"
    switch(make_set("medium"))
    play(noise(64, 64), 44100)
    switch(make_set("light"))
    play(noise(count, 65), 44100)

    # Deep on a full-scale square: the cuts ring above full scale, which the
    # limiter holds at 32767 instead of wrapping.
    switch(make_set("deep"))
    square = [(32767, 32767) if i // 40 % 2 else (-32767, -32767) for i in range(count)]
    wide = Player(44100, make_set("deep")).play(square)
    assert max(abs(s) for frame in wide for s in frame) > 32767
    got = play(square, 44100)
    assert 32000 < max(abs(s) for frame in got for s in frame) <= 32767


sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="dsp-tests-") as name:
    work = Path(name)
    includes = [sdk, root.parent]
    sources = [root.parent / file for file in ("filter.c", "math.c", "cascade.c")]
    dll = build(work, "dsp.so", sources, includes)
    math_tests(dll)
    biquad_tests(dll)
    bishelf_tests(dll)
    cascade_tests(dll)
    run_audio_test(work)
    # The stage counts of Fatigue Reduction and Bass Enhancement.
    for stages in (4, 5):
        sources = [root / "bank_host.c", sdk / "rv_libc.c", shared / "bank.c"]
        sources += [root.parent / file for file in ("limiter.c", "cascade.c")]
        flags = f"-DBANK_STAGES={stages}"
        bank_tests(
            build(work, f"bank{stages}.so", sources, [*includes, shared], flags),
            stages,
        )
print("DSP tests passed")
