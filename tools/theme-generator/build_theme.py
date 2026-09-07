# Regenerates app/qml/Theme.qml from the seeds in this folder.
#
# head/rows/tail all live HERE now. They used to be read from a scratch folder
# outside the repository, which meant this script could not run on any machine
# but the one it was written on, and would silently stop working as soon as that
# scratch folder was cleaned - taking the only way to regenerate 108 verified
# themes with it. Everything it needs is version-controlled beside it.
import os
gen = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(gen, '..', '..'))
head = open(os.path.join(gen, 'theme_head.txt'), encoding='utf-8').read()
rows = open(os.path.join(gen, 'themes_rows.txt'), encoding='utf-8').read().rstrip('\n')
tail = open(os.path.join(gen, 'theme_tail.txt'), encoding='utf-8').read()

NEW_HEADER = '''// 108 themes in four groups: Dark, Light, High contrast and Colourblind. The
// generator lives in tools/theme-generator and the seeds are hue + chroma
// character + accent hue per theme, so a new palette is three numbers rather
// than seventeen hex values guessed by eye.
//
// Four groups, not the eight it started with. Splitting by palette character -
// core, colour, vivid, paper - is how a designer thinks about colour and not how
// anyone chooses a theme; with a hundred entries it read as one long list with
// headings in it. Dark or light is the question people actually start from, and
// the picker keeps every group but the current one collapsed.
//
// The Colourblind group is named for what it is rather than for the technical
// term, and each of its themes carries "(colourblind)" in its own name so a
// settings row is self-explanatory without the heading above it. Those eight are
// not named optimistically: each one's accent and its positive/warning/danger
// colours are simulated through that deficiency (Vienot / Brettel-Mollon) and
// chosen by farthest-point selection in CIELAB, so the four signals a user has
// to tell apart provably stay apart. Hand-picked hues failed this - a
// "deuteranopia" accent collided with its own warning colour once simulated.
//
// The colours inside a graph are a SEPARATE, persisted setting - see
// native/plot2d/include/ColourVision.h. Someone needs their figures safe
// whichever theme they happen to like, so the two do not move together.'''

OLD_HEADER = '''// 100 themes in seven groups. The generator lives in tools/ and the seeds are
// hue + chroma character + accent hue per theme, so a new palette is three
// numbers rather than seventeen hex values guessed by eye.'''

if OLD_HEADER in head:
    head = head.replace(OLD_HEADER, NEW_HEADER)

out = head + rows + '\n' + tail
target = os.path.join(repo, 'app', 'qml', 'Theme.qml')
open(target, 'w', encoding='utf-8', newline='\n').write(out)

# The accessors the UI binds to must survive every regeneration. Losing them is
# how ThemePicker ended up calling substring on undefined.
required = ['themeCvdAt', 'cvdLabel', 'groupAt', 'indicesInGroup',
            'readonly property string cvd:', 'themeAccentAt', 'themeSurfaceAt']
missing = [r for r in required if r not in out]
print('Theme.qml written:', len(out), 'bytes')
print('missing accessors:', missing if missing else 'none')
assert not missing, missing
