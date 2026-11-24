#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render settings-balance.png, the Settings pane icon: a slider track with
its knob at the centre on a 78 x 121 gray coverage mask in the style of the
stock Settings icons, lit from the top and mirrored in a fading reflection
below. Drawing uses ImageMagick's `magick`.

    python3 assets/render.py
    python3 assets/render.py --check
"""

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "shared"))
from settings_icon import icon, main, supersampled

OUT = HERE / "settings-balance.png"
# The track: a rounded bar across the art box, centred on TRACK_Y; the knob a
# disc of KNOB radius on it, with a ring of GAP black around it, and a short
# stop at each end of the track.
TRACK_LEFT, TRACK_RIGHT, TRACK_Y, TRACK_HALF = 13, 65, 40, 3
KNOB, GAP = 11, 3
STOP_HALF = 8
ART_TOP, ART_BOTTOM = TRACK_Y - KNOB, TRACK_Y + KNOB


def shapes(k):
    """magick -draw arguments for the track, the stops and the knob, white on
    black, with every coordinate scaled by k."""
    cx = (TRACK_LEFT + TRACK_RIGHT) / 2
    left, right, y, half = (
        v * k for v in (TRACK_LEFT, TRACK_RIGHT, TRACK_Y, TRACK_HALF)
    )
    knob, gap, stop = KNOB * k, GAP * k, STOP_HALF * k
    r = 1.5 * k  # the corner radius of the stops
    return [
        "-fill",
        "white",
        "-draw",
        f"roundrectangle {left},{y - half} {right},{y + half} {half},{half}",
        "-draw",
        f"roundrectangle {left - k},{y - stop} {left + 2 * k},{y + stop} {r},{r}",
        "-draw",
        f"roundrectangle {right - 2 * k},{y - stop} {right + k},{y + stop} {r},{r}",
        "-fill",
        "black",
        "-draw",
        f"circle {cx * k},{y} {cx * k + knob + gap},{y}",
        "-fill",
        "white",
        "-draw",
        f"circle {cx * k},{y} {cx * k + knob},{y}",
    ]


def render(out, work):
    icon(out, work, supersampled(work, shapes), ART_TOP, ART_BOTTOM)


if __name__ == "__main__":
    main(__doc__, {OUT: render})
