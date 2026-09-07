"""Dichromat simulation (Vienot, Brettel & Mollon 1999).

Used to check, rather than assert, that a palette or a theme's status colours
stay apart for a given colour vision deficiency: simulate each colour through
the relevant projection, then compare in CIELAB. Shared by the theme generator
and by the series palettes in native/plot2d/include/ColourVision.h.
"""
import itertools, math


def s2l(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def l2s(c):
    c = max(0.0, min(1.0, c))
    v = 12.92 * c if c <= 0.0031308 else 1.055 * (c ** (1 / 2.4)) - 0.055
    return max(0, min(255, round(v * 255)))


def hex2rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def rgb2hex(r, g, b):
    return '#%02x%02x%02x' % (r, g, b)


def _mul(m, v):
    return [sum(m[i][j] * v[j] for j in range(3)) for i in range(3)]


RGB2LMS = [[0.31399022, 0.63951294, 0.04649755],
           [0.15537241, 0.75789446, 0.08670142],
           [0.01775239, 0.10944209, 0.87256922]]
LMS2RGB = [[5.47221206, -4.6419601, 0.16963708],
           [-1.1252419, 2.29317094, -0.1678952],
           [0.02980165, -0.19318073, 1.16364789]]

SIM = {
    'protanopia':   [[0.0, 1.05118294, -0.05116099], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
    'deuteranopia': [[1.0, 0.0, 0.0], [0.9513092, 0.0, 0.04866992], [0.0, 0.0, 1.0]],
    'tritanopia':   [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [-0.86744736, 1.86727089, 0.0]],
}


def simulate(hexcolour, kind):
    if kind == 'achromatopsia':
        r, g, b = hex2rgb(hexcolour)
        y = 0.2126 * s2l(r) + 0.7152 * s2l(g) + 0.0722 * s2l(b)
        v = l2s(y)
        return rgb2hex(v, v, v)
    if kind not in SIM:
        return hexcolour
    lms = _mul(SIM[kind], _mul(RGB2LMS, [s2l(c) for c in hex2rgb(hexcolour)]))
    return rgb2hex(*[l2s(c) for c in _mul(LMS2RGB, lms)])


def lab(hexcolour):
    r, g, b = [s2l(c) for c in hex2rgb(hexcolour)]
    x = 0.4124 * r + 0.3576 * g + 0.1805 * b
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = 0.0193 * r + 0.1192 * g + 0.9505 * b

    def f(t):
        return t ** (1 / 3) if t > 0.008856 else (7.787 * t + 16 / 116)

    fx, fy, fz = f(x / 0.95047), f(y / 1.0), f(z / 1.08883)
    return (116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz))


def delta_e(a, b):
    la, aa, ba = lab(a)
    lb, ab, bb = lab(b)
    return math.sqrt((la - lb) ** 2 + (aa - ab) ** 2 + (ba - bb) ** 2)


def worst_pair(colours, kind):
    sims = [simulate(c, kind) for c in colours]
    worst, pair = 1e9, None
    for i, j in itertools.combinations(range(len(sims)), 2):
        d = delta_e(sims[i], sims[j])
        if d < worst:
            worst, pair = d, (colours[i], colours[j])
    return worst, pair
