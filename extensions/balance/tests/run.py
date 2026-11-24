#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the host tests with a staged SDK: python3 tests/run.py SDK."""

import ctypes
import sys
import tempfile
from itertools import pairwise
from pathlib import Path

root = Path(__file__).resolve().parent
dsp = root.parents[1] / "dsp"
sys.path.insert(0, str(dsp / "tests"))
from harness import build, strings  # noqa: E402
from harness import noise as stereo_noise  # noqa: E402

Q15 = 1 << 15
DECIBELS = 20
CENTRE = DECIBELS + 1
STEPS = 2 * CENTRE + 1
FADE = 128


def gain_tests(dll):
    gain = dll.balance_gain
    gain.argtypes = [ctypes.c_uint]
    gain.restype = ctypes.c_int32
    assert gain(0) == Q15
    previous = Q15
    for steps in range(1, DECIBELS + 1):
        expected = Q15 / 10 ** (steps / 20)
        assert abs(gain(steps) - expected) <= 0.5, (steps, gain(steps), expected)
        assert gain(steps) < previous
        previous = gain(steps)
    assert gain(DECIBELS + 1) == 0
    assert gain(DECIBELS + 2) == 0 and gain(1000) == 0


def label_tests(dll):
    labels = strings(dll, "balance_labels", STEPS)
    expected = (
        ["L only"]
        + [f"L {k} dB" for k in range(DECIBELS, 0, -1)]
        + ["Center"]
        + [f"R {k} dB" for k in range(1, DECIBELS + 1)]
        + ["R only"]
    )
    assert labels == expected
    assert labels[CENTRE] == "Center"


def noise(count, seed):
    """Independent noise on each channel, with a full-scale first frame."""
    frames = stereo_noise(count, seed)
    frames[0] = (32767, -32768)
    return frames


def model(index, frames, gain):
    """One state: left of centre the right ear is scaled, right of centre the
    left; centre passes through."""
    if index == CENTRE:
        return list(frames)
    quiet = 1 if index < CENTRE else 0
    out = []
    for frame in frames:
        pair = list(frame)
        pair[quiet] = (pair[quiet] * gain + 16384) >> 15
        out.append(tuple(pair))
    return out


def faded(old, new):
    """What rv_fade plays after a take: old blended into new over the first FADE
    frames at weights 1 to FADE, floor-shifted as C's arithmetic shift does,
    then new."""
    out = []
    for i, (a, b) in enumerate(zip(old, new, strict=True)):
        weight = min(i + 1, FADE)
        out.append(tuple((x * (FADE - weight) + y * weight) >> 7 for x, y in zip(a, b)))
    return out


def process_tests(dll):
    publish = dll.balance_publish
    publish.argtypes = [ctypes.c_uint]
    publish.restype = None
    process = dll.balance_process
    process.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_uint32]
    process.restype = None
    gain = dll.balance_gain
    dll.test_irq_calls.restype = ctypes.c_uint32
    count = 512

    def run(frames, *splits):
        """Processes the frames in one buffer, or in buffers cut at the splits."""
        buffer = (ctypes.c_int16 * (2 * len(frames)))(
            *[s for frame in frames for s in frame]
        )
        edges = [0, *splits, len(frames)]
        for first, last in pairwise(edges):
            process(
                ctypes.cast(ctypes.byref(buffer, 4 * first), process.argtypes[0]),
                last - first,
            )
        return [(buffer[2 * i], buffer[2 * i + 1]) for i in range(len(frames))]

    def designed(index):
        return gain(abs(index - CENTRE))

    # What plays and, after a take, what fades out: (index, gain).
    players = {"new": (CENTRE, Q15), "old": None}

    def switch(index):
        """Publishes an index; `expected` then fades from what played."""
        publish(index)
        players["old"] = players["new"]
        players["new"] = (index, designed(index))

    def expected(frames):
        old, players["old"] = players["old"], None
        new = players["new"]
        if old is None:
            return model(new[0], frames, new[1])
        return faded(model(old[0], frames, old[1]), model(new[0], frames, new[1]))

    def play(frames, *splits):
        got = run(frames, *splits)
        assert got == expected(frames), splits
        return got

    # Center leaves the stream alone; one publish masks interrupts once.
    switch(CENTRE)
    frames = noise(count, 1)
    assert play(frames) == frames
    assert dll.test_irq_calls() == 2

    # Left of centre the right ear drops by the step, the left stays bit for bit;
    # right of centre the reverse; the ends mute one ear.
    for index in range(STEPS):
        switch(index)
        frames = noise(count, 2 + index)
        got = play(frames)
        settled = got[FADE:]
        inputs = frames[FADE:]
        if index < CENTRE:
            assert all(a[0] == b[0] for a, b in zip(settled, inputs, strict=True))
            scaled = [b[1] for b in settled]
        elif index > CENTRE:
            assert all(a[1] == b[1] for a, b in zip(settled, inputs, strict=True))
            scaled = [b[0] for b in settled]
        else:
            assert settled == inputs
            continue
        if index in (0, STEPS - 1):
            assert all(s == 0 for s in scaled)
        else:
            assert any(s != 0 for s in scaled)
            assert all(-32768 <= s <= 32767 for s in scaled)

    # A one-decibel step from centre fades over FADE frames, across buffer
    # edges; the latest index wins when two are published before a buffer.
    switch(CENTRE)
    play(noise(count, 90))
    switch(CENTRE - 1)
    play(noise(count, 91), 17, 100)
    publish(0)
    switch(CENTRE + 5)
    play(noise(count, 92))

    # A set taken during a fade restarts the fade from the set that plays:
    # the second take lands FADE / 2 frames in.
    switch(CENTRE)
    play(noise(count, 93))
    switch(STEPS - 1)
    play(noise(FADE // 2, 94))
    switch(CENTRE)
    play(noise(count, 95))

    # An index past the end plays as centre.
    publish(STEPS + 7)
    players["old"], players["new"] = players["new"], (CENTRE, Q15)
    frames = noise(count, 96)
    assert run(frames)[FADE:] == frames[FADE:]


sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="balance-tests-") as name:
    work = Path(name)
    sources = [root / "host.c", sdk / "rv_libc.c"]
    sources += [root.parent / file for file in ("balance.c", "gain.c", "labels.c")]
    dll = build(work, "balance.so", sources, [sdk, root.parent])
    gain_tests(dll)
    label_tests(dll)
    process_tests(dll)
print("Balance tests passed")
