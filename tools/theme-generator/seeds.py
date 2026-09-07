# (name, group, mode, base hue, chroma character, accent hue)
# chroma: 'neutral' | 'tinted' | 'colour' | 'vivid'
D_CORE = [
    ("Obsidian",      210, 'neutral', 212), ("Graphite",     220, 'neutral', 200),
    ("Slate",         205, 'tinted',  198), ("Charcoal",      30, 'neutral',  32),
    ("Basalt",        250, 'neutral', 262), ("Anthracite",    195,'neutral', 186),
    ("Ironwood",       25, 'tinted',   18), ("Gunmetal",     215, 'tinted',  225),
    ("Onyx",          270, 'neutral', 288), ("Steel",        207, 'tinted',  205),
    ("Pitch",           0, 'neutral', 350), ("Umber",         28, 'tinted',   40),
    ("Deep Space",    235, 'tinted',  248), ("Carbon",       160, 'neutral', 168),
    ("Shale",         185, 'tinted',  178), ("Tungsten",      45, 'neutral',  50),
    ("Nightshade",    285, 'tinted',  300), ("Flint",        150, 'neutral', 140),
    ("Cinder",         12, 'neutral',   6), ("Void",         240, 'neutral', 232),
]
D_COLOUR = [
    ("Deep Sea",      192, 'colour',  178), ("Abyss",        205, 'colour',  190),
    ("Kelp Forest",   162, 'colour',  150), ("Midnight Reef",200, 'colour',  320),
    ("Plum",          290, 'colour',  312), ("Mulberry",     320, 'colour',  335),
    ("Pine",          150, 'colour',  138), ("Moss",         100, 'colour',   88),
    ("Ember",          22, 'colour',   34), ("Rust",          14, 'colour',   26),
    ("Indigo",        248, 'colour',  258), ("Cobalt",       222, 'colour',  212),
    ("Aubergine",     282, 'colour',  296), ("Teal Depths",  182, 'colour',  170),
    ("Blackcurrant",  310, 'colour',  330), ("Juniper",      168, 'colour',  156),
    ("Bordeaux",      348, 'colour',    8), ("Petrol",       196, 'colour',  204),
    ("Olive Night",    75, 'colour',   62), ("Sapphire",     228, 'colour',  218),
    ("Tidepool",      176, 'colour',  188), ("Cocoa",         20, 'colour',   38),
]
D_VIVID = [
    ("Neon Reef",     186, 'vivid',   172), ("Synthwave",    295, 'vivid',   322),
    ("Cyberlime",     140, 'vivid',   100), ("Hot Coral",    345, 'vivid',    14),
    ("Electric Iris", 258, 'vivid',   276), ("Acid Rain",     88, 'vivid',    72),
    ("Vapour",        320, 'vivid',   192), ("Laser Grape",  272, 'vivid',   300),
    ("Magma",          10, 'vivid',    32), ("Ultramarine",  232, 'vivid',   205),
    ("Toxic Teal",    174, 'vivid',   160), ("Fuchsia Dusk", 328, 'vivid',   348),
    ("Signal Amber",   40, 'vivid',    46), ("Deep Neon",    210, 'vivid',   288),
]
L_CORE = [
    ("Paper",         210, 'neutral', 212), ("Snow",         220, 'neutral', 205),
    ("Chalk",         200, 'neutral', 196), ("Porcelain",    195, 'tinted',  200),
    ("Ash",           215, 'neutral', 222), ("Cotton",        30, 'neutral',  28),
    ("Bone",           40, 'neutral',  36), ("Marble",       230, 'tinted',  238),
    ("Quartz",        270, 'neutral', 282), ("Cloud",        205, 'tinted',  190),
    ("Frost",         190, 'tinted',  182), ("Linen",         35, 'tinted',   26),
    ("Pearl",         250, 'neutral', 258), ("Mist",         165, 'tinted',  158),
    ("Limestone",      50, 'neutral',  44), ("Silver",       208, 'neutral', 214),
]
L_COLOUR = [
    ("Lagoon",        188, 'colour',  180), ("Meadow",       130, 'colour',  118),
    ("Blossom",       335, 'colour',  345), ("Wisteria",     280, 'colour',  292),
    ("Sandbar",        38, 'colour',   30), ("Cornflower",   222, 'colour',  214),
    ("Seafoam",       165, 'colour',  152), ("Apricot",       24, 'colour',   36),
    ("Lilac",         290, 'colour',  302), ("Harbour",      205, 'colour',  196),
    ("Sage",          105, 'colour',   94), ("Coral Reef",   350, 'colour',   10),
    ("Periwinkle",    240, 'colour',  250), ("Citrus",        58, 'colour',   46),
]
L_PAPER = [
    ("Parchment",      42, 'tinted',   30), ("Manuscript",    38, 'colour',   22),
    ("Sepia",          28, 'colour',   18), ("Ivory",         48, 'tinted',   40),
    ("Papyrus",        36, 'colour',  180), ("Vellum",        44, 'tinted',  200),
    ("Newsprint",      45, 'neutral',   0), ("Tea Stain",     26, 'colour',  150),
]
HIGH = [
    ("Contrast Dark",       210, 'neutral', 200), ("Contrast Light",   210, 'neutral', 214),
    ("Contrast Amber Dark",  40, 'neutral',  44), ("Contrast Sea Dark",190, 'neutral', 184),
    ("Contrast Green Dark", 140, 'neutral', 132), ("Contrast Warm Light",30,'neutral',  20),
]

# Colour vision themes.
#
# The accent hue is the point of these: an interface accent has to stay
# distinguishable from the surface AND from the positive/warning/danger states
# for the deficiency in question. Protans and deutans confuse the red-green
# axis, so their accents sit on the blue-orange axis; tritans confuse
# blue-yellow, so theirs sit on red-teal. Achromatopsia gets no usable hue at
# all, so those two themes carry maximum lightness separation instead and the
# generator's contrast targets are raised.
#
# These are interface themes. The colours inside a graph are a separate,
# persisted setting - see native/plot2d/include/ColourVision.h - because someone
# needs their figures safe regardless of which theme they happen to like.
CVD = [
    ("Protanopia Dark",    210, 'tinted',  212, 'protanopia'),
    ("Protanopia Light",   210, 'tinted',  216, 'protanopia'),
    ("Deuteranopia Dark",  220, 'tinted',   38, 'deuteranopia'),
    ("Deuteranopia Light", 220, 'tinted',   32, 'deuteranopia'),
    ("Tritanopia Dark",    350, 'tinted',  352, 'tritanopia'),
    ("Tritanopia Light",   350, 'tinted',  348, 'tritanopia'),
    ("Achromatopsia Dark",   0, 'neutral',   0, 'achromatopsia'),
    ("Achromatopsia Light",  0, 'neutral',   0, 'achromatopsia'),
]

# Four groups, not eight.
#
# The catalogue was first split by character - core, colour, vivid, paper -
# which is how a designer thinks about palettes and not how anyone chooses one.
# With a hundred entries it read as a single long list with headings in it. Dark
# or light is the question people actually start from, so that is the split, and
# the picker collapses everything except the group you are in.
GROUPS = [
    ("Dark",         'dark',  D_CORE + D_COLOUR + D_VIVID),
    ("Light",        'light', L_CORE + L_COLOUR + L_PAPER),
    ("High contrast",'mixed', HIGH),
    # Named for what it is. "Colour vision" was accurate and told nobody
    # anything; someone looking for these is looking for the word colourblind.
    ("Colourblind",  'mixed', CVD),
]
