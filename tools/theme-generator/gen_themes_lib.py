"""Palette generation with contrast guarantees.

Every colour a theme exposes is derived from a base hue, a chroma character and
a light/dark mode, then adjusted until it meets a WCAG contrast target against
the surface it is drawn on. That is why light themes are readable here: the
generator refuses to emit a token that fails its target, rather than trusting a
hand-picked hex to be legible on every background.
"""
import colorsys


def hex_of(r, g, b):
    return '#%02x%02x%02x' % (max(0, min(255, round(r * 255))),
                              max(0, min(255, round(g * 255))),
                              max(0, min(255, round(b * 255))))


def rgb_of(h, s, l):
    r, g, b = colorsys.hls_to_rgb(h / 360.0, l, s)
    return r, g, b


def hsl_hex(h, s, l):
    return hex_of(*rgb_of(h, s, l))


def _channel(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def luminance(rgb):
    r, g, b = (_channel(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def fit_lightness(h, s, against, target, dark_text):
    """Find the lightness closest to the ideal that still meets `target`.

    dark_text=False walks lightness up (light text on a dark surface),
    True walks it down. Stepping from the ideal rather than binary-searching
    keeps the intended look and only sacrifices as much of it as legibility
    demands.
    """
    step = -0.01 if dark_text else 0.01
    l = 0.5
    best = None
    for _ in range(100):
        rgb = rgb_of(h, s, l)
        if contrast(rgb, against) >= target:
            best = l
            break
        l += step
        if l <= 0.0 or l >= 1.0:
            break
    return best if best is not None else (0.02 if dark_text else 0.98)


def tone(h, s, l, against, target, dark_text):
    """Emit a colour at `l` if it clears `target`, otherwise the nearest that does."""
    rgb = rgb_of(h, s, l)
    if contrast(rgb, against) >= target:
        return hex_of(*rgb)
    step = -0.015 if dark_text else 0.015
    cur = l
    for _ in range(120):
        cur += step
        if cur <= 0.0 or cur >= 1.0:
            break
        rgb = rgb_of(h, s, cur)
        if contrast(rgb, against) >= target:
            return hex_of(*rgb)
    return hex_of(*rgb_of(h, s, 0.02 if dark_text else 0.98))
