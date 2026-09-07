import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_themes_lib import hsl_hex, rgb_of, contrast, tone, hex_of
from seeds import GROUPS

# (base chroma, accent chroma). The accent keeps its own scale: a neutral
# graphite base still wants a confident accent, not a grey-blue one.
CHROMA = {'neutral': (0.05, 0.62), 'tinted': (0.12, 0.70),
          'colour': (0.22, 0.78), 'vivid': (0.34, 0.92)}


def signal_set(cvd, surface, dark, dark_text, accent_hue):
    """Accent plus positive/warning/danger, verified apart under `cvd`."""
    from cvd_sim import simulate, delta_e

    def usable(h, sat, light):
        rgb = rgb_of(h, sat, light)
        if contrast(rgb, surface) < (3.0 if cvd == 'achromatopsia' else 3.2):
            return None
        return hex_of(*rgb)

    achromat = (cvd == 'achromatopsia')
    hues = [0] if achromat else list(range(0, 360, 10))
    sats = [0.0] if achromat else [0.45, 0.62, 0.80]
    # An achromat has only lightness to work with, so that axis is opened as
    # wide as the contrast floor allows - four signals need the whole range.
    if achromat:
        lights = [l / 100.0 for l in (range(38, 99, 3) if dark else range(4, 68, 3))]
    else:
        lights = [l / 100.0 for l in (range(45, 92, 4) if dark else range(20, 62, 4))]
    pool = []
    for h in hues:
        for sat in sats:
            for light in lights:
                c = usable(h, sat, light)
                if c:
                    pool.append(c)
    if not pool:
        return ('#888888',) * 4

    seed = min(pool, key=lambda c: min(abs(_hue_of(c) - accent_hue),
                                       360 - abs(_hue_of(c) - accent_hue)))
    chosen = [seed]
    while len(chosen) < 4:
        best, best_score = None, -1
        for c in pool:
            if c in chosen:
                continue
            sims = [simulate(x, cvd) for x in chosen + [c]]
            worst = min(delta_e(sims[i], sims[j])
                        for i in range(len(sims)) for j in range(i + 1, len(sims)))
            if worst > best_score:
                best, best_score = c, worst
        chosen.append(best)
    return tuple(chosen)


def _hue_of(hexcolour):
    import colorsys
    r, g, b = (int(hexcolour[i:i + 2], 16) / 255.0 for i in (1, 3, 5))
    return colorsys.rgb_to_hls(r, g, b)[0] * 360.0


def build(name, group, mode, hue, character, accent_hue, high_contrast, cvd=''):
    s_bg, s_accent = CHROMA[character]
    dark = (mode == 'dark')

    # Achromatopsia sees no hue at all, so those themes are driven entirely by
    # lightness separation and are held to the high-contrast targets.
    if cvd == 'achromatopsia':
        high_contrast = True
        s_bg, s_accent = 0.0, 0.0

    if dark:
        bg_l, surf_l, alt_l = (0.045, 0.075, 0.105) if high_contrast else (0.065, 0.10, 0.135)
        border_l, strong_l = 0.22, 0.33
        ideal = (0.95, 0.78, 0.62, 0.48)
        targets = (14.0, 7.0, 4.6, 3.1) if not high_contrast else (17.0, 11.0, 7.0, 4.5)
        accent_l = 0.63
    else:
        bg_l, surf_l, alt_l = (0.985, 1.0, 0.955) if high_contrast else (0.965, 1.0, 0.935)
        border_l, strong_l = 0.855, 0.72
        ideal = (0.12, 0.30, 0.44, 0.60)
        targets = (14.0, 7.0, 4.6, 3.1) if not high_contrast else (17.0, 11.0, 7.0, 4.5)
        accent_l = 0.42

    surface = rgb_of(hue, s_bg * 0.9, surf_l)
    dark_text = not dark  # on a light surface the text walks darker

    t  = tone(hue, min(s_bg * 0.8, 0.18), ideal[0], surface, targets[0], dark_text)
    t2 = tone(hue, min(s_bg * 0.9, 0.22), ideal[1], surface, targets[1], dark_text)
    tm = tone(hue, min(s_bg,       0.24), ideal[2], surface, targets[2], dark_text)
    td = tone(hue, min(s_bg,       0.24), ideal[3], surface, targets[3], dark_text)

    # The accent must separate from the surface, and its own label must be
    # readable on it, so both directions are checked.
    acc = tone(accent_hue, s_accent, accent_l, surface, 3.0, dark_text)
    acc_rgb = tuple(int(acc[i:i + 2], 16) / 255.0 for i in (1, 3, 5))
    # The accent's own label: try near-black then near-white, and if neither
    # clears 4.5:1 the accent itself is the problem - darken or lighten it until
    # one of them does, rather than shipping an unreadable button.
    def label_for(colour_rgb):
        for sat, light in ((0.30, 0.05), (0.15, 0.98), (0.0, 0.0), (0.0, 1.0)):
            candidate = rgb_of(accent_hue, sat, light)
            if contrast(candidate, colour_rgb) >= 4.6:
                return hex_of(*candidate)
        return None

    on_accent = label_for(acc_rgb)
    if on_accent is None:
        step = 0.02 if dark else -0.02
        l = accent_l
        for _ in range(45):
            l += step
            if not (0.02 < l < 0.98):
                break
            candidate = rgb_of(accent_hue, s_accent, l)
            if contrast(candidate, surface) < 3.0:
                continue
            label = label_for(candidate)
            if label is not None:
                acc, acc_rgb, on_accent = hex_of(*candidate), candidate, label
                break
    if on_accent is None:
        on_accent = hex_of(*rgb_of(accent_hue, 0.0, 0.0))

    def status(h, sat):
        return tone(h, sat, 0.60 if dark else 0.38, surface, 3.5, dark_text)

    if cvd:
        # The accent and the three status colours are the four the user has to
        # tell apart, so for a colour vision theme they are chosen together:
        # every candidate is simulated through that deficiency and the set is
        # grown by farthest-point selection in CIELAB. Guessing hues by hand
        # produced accents that collided with "warning" once simulated.
        acc, positive, warning, danger = signal_set(cvd, surface, dark, dark_text, accent_hue)
        acc_rgb = tuple(int(acc[i:i + 2], 16) / 255.0 for i in (1, 3, 5))
        on_accent = label_for(acc_rgb) or hex_of(*rgb_of(0, 0.0, 0.0))
    else:
        positive, warning, danger = status(148, 0.55), status(38, 0.72), status(6, 0.68)

    # The colourblind themes carry it in their own name too, so a screenshot or
    # a settings row is self-explanatory without the group heading above it.
    display_name = name
    if cvd:
        display_name = name + " (colourblind)"

    return {
        'name': display_name, 'group': group, 'light': (not dark), 'cvd': cvd,
        'bg': hsl_hex(hue, s_bg * 0.85, bg_l),
        'surface': hsl_hex(hue, s_bg * 0.9, surf_l),
        'surfaceAlt': hsl_hex(hue, s_bg, alt_l),
        'border': hsl_hex(hue, s_bg * 0.9, border_l),
        'borderStrong': hsl_hex(hue, s_bg, strong_l),
        'text': t, 'text2': t2, 'muted': tm, 'disabled': td,
        'accent': acc, 'onAccent': on_accent,
        'positive': positive, 'warning': warning, 'danger': danger,
    }

themes = []
for group, mode, rows in GROUPS:
    high = group.startswith('High')
    for row in rows:
        name, hue, character, accent_hue = row[0], row[1], row[2], row[3]
        cvd = row[4] if len(row) > 4 else ''
        row_mode = mode
        if row_mode == 'mixed':
            row_mode = 'light' if 'Light' in name else 'dark'
        themes.append(build(name, group, row_mode, hue, character, accent_hue, high, cvd))

# ---- verification: no theme may ship an unreadable token.
def rgb(hexstr):
    return tuple(int(hexstr[i:i + 2], 16) / 255.0 for i in (1, 3, 5))

problems = []
for t in themes:
    s = rgb(t['surface'])
    checks = [('text', 13.0), ('text2', 6.5), ('muted', 4.4), ('disabled', 3.0), ('accent', 2.9)]
    for key, want in checks:
        got = contrast(rgb(t[key]), s)
        if got < want:
            problems.append((t['name'], key, round(got, 2), want))
    if contrast(rgb(t['onAccent']), rgb(t['accent'])) < 4.4:
        problems.append((t['name'], 'onAccent', round(contrast(rgb(t['onAccent']), rgb(t['accent'])), 2), 4.5))
    if contrast(rgb(t['border']), s) < 1.12:
        problems.append((t['name'], 'border', round(contrast(rgb(t['border']), s), 2), 1.15))

# A colour vision theme has to hold up under the deficiency it claims to serve:
# its accent and its three status colours must stay apart once simulated, or the
# label on it is a false promise.
from cvd_sim import worst_pair
cvd_failures = []
for t in themes:
    if not t['cvd']:
        continue
    signals = [t['accent'], t['positive'], t['warning'], t['danger']]
    worst, pair = worst_pair(signals, t['cvd'])
    if worst < 12.0:
        cvd_failures.append((t['name'], t['cvd'], round(worst, 1), pair))
print('colour-vision themes checked:', sum(1 for t in themes if t['cvd']))
print('colour-vision failures:', len(cvd_failures))
for f in cvd_failures:
    print('   ', f)

names = [t['name'] for t in themes]
assert len(names) == len(set(names)), 'duplicate theme name'
print('themes:', len(themes))
print('problems:', len(problems))
for p in problems[:25]:
    print('  ', p)

lines = []
for t in themes:
    lines.append('        { name: "%s", group: "%s", light: %s, cvd: "%s", bg: "%s", surface: "%s", surfaceAlt: "%s", '
                 'border: "%s", borderStrong: "%s", text: "%s", text2: "%s", muted: "%s", disabled: "%s", '
                 'accent: "%s", onAccent: "%s", positive: "%s", warning: "%s", danger: "%s" }' % (
                     t['name'], t['group'], 'true' if t['light'] else 'false', t['cvd'],
                     t['bg'], t['surface'], t['surfaceAlt'], t['border'], t['borderStrong'],
                     t['text'], t['text2'], t['muted'], t['disabled'],
                     t['accent'], t['onAccent'], t['positive'], t['warning'], t['danger']))
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'themes_rows.txt'), 'w',
     encoding='utf-8', newline='\n').write(',\n'.join(lines) + '\n')
print('rows written')
