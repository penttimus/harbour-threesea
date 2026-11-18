#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Render the launcher icon as PNGs for the sizes Sailfish needs (icons/<size>/harbour-threesea.png).

The shape of Jolla's launcher icons (a disc with one nearly square corner, here the bottom left),
filling the canvas like theirs: a speech bubble with three dots, leaning, afloat in a sea of
three waves that washes over its lower edge. Lime to green like the gradients on Jolla's icons,
with light from the top left; layered like Jolla's icons with gradients and translucent waves, no blurred shadows. Our own
mark, on purpose not Threema's (a dark disc with a white bubble and bright green dots), which we
may not use (ADR 0015). Kept simple so it reads in a large app grid. The cover draws the same
picture in one colour (qml/components/CoverSea.qml): the bubble's size, lean and depth there are
the ones here. Needs pycairo."""
import math
import sys
from pathlib import Path

import cairo

SIZES = (86, 108, 128, 172)
# Drawn on a 100-unit canvas.


def rgba(colour, alpha=1.0):
    return tuple(int(colour[i : i + 2], 16) / 255 for i in (1, 3, 5)) + (alpha,)


def gradient(x0, y0, x1, y1, *stops):
    """Evenly spaced (colour, alpha) stops."""
    result = cairo.LinearGradient(x0, y0, x1, y1)
    for i, (colour, alpha) in enumerate(stops):
        result.add_color_stop_rgba(i / (len(stops) - 1), *rgba(colour, alpha))
    return result


def shape(c, x, y, w, h, r, corner):
    """A rectangle with corners of radius r, the bottom left one nearly square (radius `corner`)."""
    c.new_sub_path()
    c.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    c.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    c.arc(x + corner, y + h - corner, corner, math.pi / 2, math.pi)
    c.arc(x + r, y + r, r, math.pi, 1.5 * math.pi)
    c.close_path()


def wave(c, y, amplitude, phase, crest, deep, periods=1.25):
    """Everything under a gentle wave (cut by the shape): bright at the crest on the left, deeper
    towards the bottom right. `crest` and `deep` are (colour, alpha): the sea is translucent."""
    c.move_to(-5, 105)
    for x in range(-5, 106):
        c.line_to(x, y + amplitude * math.sin(x / 100 * periods * 2 * math.pi + phase))
    c.line_to(105, 105)
    c.close_path()
    c.set_source(gradient(0, y - amplitude, 100, y + amplitude + 16, crest, deep))
    c.fill()


def draw(c):
    shape(c, 0, 0, 100, 100, 50, 2.5)
    c.save()
    c.clip()
    # Lime to green, with a soft light from the top left.
    c.set_source(gradient(10, 0, 90, 85, ("#B5E07A", 1), ("#66C272", 1), ("#1F9A72", 1)))
    c.paint()
    light = cairo.RadialGradient(22, 10, 0, 22, 10, 70)
    light.add_color_stop_rgba(0, *rgba("#FFFFD0", 0.38))
    light.add_color_stop_rgba(1, *rgba("#FFFFD0", 0))
    c.set_source(light)
    c.paint()
    # A far swell behind the bubble.
    wave(c, 52, 5, 3.6, ("#9BE08F", 0.30), ("#3FB07A", 0.05), periods=0.9)

    # The bubble, its nearly square corner on the same side as the shape's, leaning into the sea.
    w, h = 67.2, 54.6
    c.save()
    c.translate(50, 45)
    c.rotate(math.radians(-7))
    c.translate(-w / 2, -h / 2)
    shape(c, 0, 0, w, h, 22.4, 2.24)
    c.set_source(gradient(0, 0, w, h, ("#FBFFF4", 1), ("#E6F8D6", 0.97), ("#B9E6A6", 0.92)))
    c.fill()
    c.set_source_rgba(*rgba("#1B6B47", 0.9))
    for dot in (0.27, 0.5, 0.73):
        c.arc(w * dot, h / 2, 5.18, 0, 2 * math.pi)
        c.fill()
    c.restore()

    # The sea: translucent layers, so the bubble's lower edge shines through the first.
    wave(c, 65, 4.0, 0.6, ("#62D48A", 0.80), ("#1FA277", 0.90))
    wave(c, 76.5, 4.0, 2.5, ("#34B578", 0.85), ("#0F8068", 0.95))
    wave(c, 88, 3.6, 4.4, ("#1B9469", 0.9), ("#085E52", 1))
    c.restore()


def render(size):
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
    c = cairo.Context(surface)
    c.scale(size / 100, size / 100)
    draw(c)
    return surface


out = Path(sys.argv[1] if len(sys.argv) > 1 else "icons")
for s in SIZES:
    d = out / f"{s}x{s}"
    d.mkdir(parents=True, exist_ok=True)
    render(s).write_to_png(str(d / "harbour-threesea.png"))
