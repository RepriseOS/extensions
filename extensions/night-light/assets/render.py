#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render settings-night-light.png, the Settings pane icon: a dimmed sun, a
ring with short rays drawn as dots, on a 78 x 121 gray coverage mask in the
style of the stock Settings icons, lit from the top and mirrored in a fading
reflection below. Drawing uses ImageMagick's `magick`.

    python3 assets/render.py
    python3 assets/render.py --check
"""

import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "shared"))
from settings_icon import ART_CENTRE, icon, main, supersampled

OUT = HERE / "settings-night-light.png"
# The ring: outer radius RING, stroke WIDTH; eight dots of radius DOT at RAYS
# from the centre stand for the sun's rays, short and set apart for dimness.
RING, STROKE = 11, 3.5
RAYS, DOT = 20, 2.6
CX, CY = ART_CENTRE
ART_TOP, ART_BOTTOM = CY - RAYS - 3, CY + RAYS + 3


def shapes(k):
    """magick -draw arguments for the ring and the dots, white on black, with
    every coordinate scaled by k."""
    cx, cy = CX * k, CY * k
    draws = [
        "-fill",
        "white",
        "-draw",
        f"circle {cx},{cy} {cx + RING * k},{cy}",
        "-fill",
        "black",
        "-draw",
        f"circle {cx},{cy} {cx + (RING - STROKE) * k},{cy}",
        "-fill",
        "white",
    ]
    for i in range(8):
        a = i * math.pi / 4
        x, y = cx + RAYS * k * math.cos(a), cy + RAYS * k * math.sin(a)
        draws += ["-draw", f"circle {x:.1f},{y:.1f} {x + DOT * k:.1f},{y:.1f}"]
    return draws


def render(out, work):
    icon(out, work, supersampled(work, shapes), ART_TOP, ART_BOTTOM)


if __name__ == "__main__":
    main(__doc__, {OUT: render})
