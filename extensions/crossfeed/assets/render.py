#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render settings-crossfeed.png, the Settings pane icon: a pair of headphones
on a 78 x 121 gray coverage mask in the style of the stock Settings icons, lit
from the top and mirrored in a fading reflection below. Drawing uses
ImageMagick's `magick`.

    python3 assets/render.py
    python3 assets/render.py --check
"""

import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "shared"))
from settings_icon import HEIGHT, WIDTH, canvas, icon, main

OUT = HERE / "settings-crossfeed.png"
# The headband: the upper half of a superellipse (exponent BAND_SHAPE, between an
# ellipse and a rounded box) centred on BAND_CENTRE, outer and inner half-axes
# as given, with straight legs from the centre line down to LEG_BOTTOM.
BAND_CENTRE = (39, 32)
BAND_OUTER, BAND_INNER = (26.5, 20), (21.5, 15.5)
BAND_SHAPE = 2.3
LEG_BOTTOM = 51
# The cups: 11 x 21 with rounded corners, hanging inside the legs.
CUPS = (22, 46)
CUP_TOP, CUP_WIDTH, CUP_HEIGHT, CUP_RADIUS = 41, 10.5, 20.5, 3


def arch(a, b):
    """Points along the upper half of the superellipse, left to right."""
    cx, cy = BAND_CENTRE
    points = []
    for step in range(0, 181, 2):
        t = math.radians(step)
        c, s = math.cos(t), math.sin(t)
        x = cx - math.copysign(abs(c) ** (2 / BAND_SHAPE), c) * a
        y = cy - abs(s) ** (2 / BAND_SHAPE) * b
        points.append(f"{x:.2f},{y:.2f}")
    return points


def shapes():
    """magick -draw arguments for the headphones, white on black."""
    cx = BAND_CENTRE[0]
    (oa, ob), (ia, ib) = BAND_OUTER, BAND_INNER
    outer = [f"{cx - oa},{LEG_BOTTOM}", *arch(oa, ob), f"{cx + oa},{LEG_BOTTOM}"]
    inner = [
        f"{cx + ia},{LEG_BOTTOM + 1}",
        *reversed(arch(ia, ib)),
        f"{cx - ia},{LEG_BOTTOM + 1}",
    ]
    band = "M " + " L ".join(outer + inner) + " Z"
    draw = [
        "-fill",
        "white",
        "-draw",
        f"path '{band}'",
        "-fill",
        "black",
        "-draw",
        f"rectangle 0,{LEG_BOTTOM} {WIDTH},{HEIGHT}",
        "-fill",
        "white",
    ]
    for left in CUPS:
        draw += [
            "-draw",
            (
                f"roundrectangle {left},{CUP_TOP} "
                f"{left + CUP_WIDTH},{CUP_TOP + CUP_HEIGHT} {CUP_RADIUS},{CUP_RADIUS}"
            ),
        ]
    return draw


def render(out, work):
    icon(
        out,
        work,
        canvas(*shapes()),
        BAND_CENTRE[1] - BAND_OUTER[1],
        CUP_TOP + CUP_HEIGHT,
    )


if __name__ == "__main__":
    main(__doc__, {OUT: render})
