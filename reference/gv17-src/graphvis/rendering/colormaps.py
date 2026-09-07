# =========================================================================
# colormaps.py — categorized GraphVis scientific colourmap palette.
# =========================================================================
from __future__ import annotations

import matplotlib
from matplotlib.colors import LinearSegmentedColormap, ListedColormap

COLOURMAP_CATEGORIES = {
    "Perceptually Uniform": [
        "Parula", "Viridis", "Plasma", "Inferno", "Magma", "Cividis", "Turbo",
    ],
    "Scientific Sequential": [
        "Hot", "Afmhot", "Gist Heat", "Copper", "Bone", "Pink", "Gray", "Greys",
        "Purples", "Blues", "Greens", "Oranges", "Reds", "YlOrBr", "YlOrRd", "OrRd",
        "PuRd", "RdPu", "BuPu", "GnBu", "PuBu", "YlGnBu", "PuBuGn", "BuGn", "YlGn",
    ],
    "Diverging": [
        "Coolwarm", "Bwr", "Seismic", "Spectral", "RdBu", "RdGy", "RdYlBu", "RdYlGn",
        "PuOr", "BrBG", "PRGn", "PiYG",
    ],
    "Cyclic & Angular": [
        "Twilight", "Twilight Shifted", "HSV", "Phase", "Cubehelix",
    ],
    "Seasonal & Shading": [
        "Cool", "Spring", "Summer", "Autumn", "Winter", "Binary", "Gist Gray", "Gist Yarg",
    ],
    "Terrain, Ocean & Field": [
        "Terrain", "Ocean", "Gist Earth", "Gist Ncar", "Gnuplot", "Gnuplot2", "Nipy Spectral",
        "Rainbow", "Gist Rainbow", "Jet",
    ],
    "Categorical": [
        "Lines", "Tab10", "Tab20", "Tab20b", "Tab20c", "Set1", "Set2", "Set3",
        "Paired", "Accent", "Dark2", "Pastel1", "Pastel2",
    ],
    "Specialized & Utility": ["Prism", "Flag", "Colorcube", "White"],
}
COLOURMAPS = [name for group in COLOURMAP_CATEGORIES.values() for name in group]

_PARULA_64 = [
    (0.2081, 0.1663, 0.5292), (0.2116, 0.1898, 0.5777), (0.2123, 0.2138, 0.6270),
    (0.2081, 0.2386, 0.6771), (0.1959, 0.2645, 0.7279), (0.1707, 0.2919, 0.7792),
    (0.1253, 0.3242, 0.8303), (0.0591, 0.3598, 0.8683), (0.0117, 0.3875, 0.8820),
    (0.0060, 0.4086, 0.8828), (0.0165, 0.4266, 0.8786), (0.0329, 0.4430, 0.8720),
    (0.0498, 0.4586, 0.8641), (0.0629, 0.4737, 0.8554), (0.0723, 0.4887, 0.8467),
    (0.0779, 0.5040, 0.8384), (0.0793, 0.5200, 0.8312), (0.0749, 0.5375, 0.8263),
    (0.0641, 0.5570, 0.8240), (0.0488, 0.5772, 0.8228), (0.0343, 0.5966, 0.8199),
    (0.0265, 0.6137, 0.8135), (0.0239, 0.6287, 0.8038), (0.0231, 0.6418, 0.7913),
    (0.0228, 0.6535, 0.7768), (0.0267, 0.6642, 0.7607), (0.0384, 0.6743, 0.7436),
    (0.0590, 0.6838, 0.7254), (0.0843, 0.6928, 0.7062), (0.1133, 0.7015, 0.6859),
    (0.1453, 0.7098, 0.6646), (0.1801, 0.7177, 0.6424), (0.2178, 0.7250, 0.6193),
    (0.2586, 0.7317, 0.5954), (0.3022, 0.7376, 0.5712), (0.3482, 0.7424, 0.5473),
    (0.3953, 0.7459, 0.5244), (0.4420, 0.7481, 0.5033), (0.4871, 0.7491, 0.4840),
    (0.5300, 0.7491, 0.4661), (0.5709, 0.7485, 0.4494), (0.6099, 0.7473, 0.4337),
    (0.6473, 0.7456, 0.4188), (0.6834, 0.7435, 0.4044), (0.7184, 0.7411, 0.3905),
    (0.7525, 0.7384, 0.3768), (0.7858, 0.7356, 0.3633), (0.8185, 0.7327, 0.3498),
    (0.8507, 0.7299, 0.3360), (0.8824, 0.7274, 0.3217), (0.9139, 0.7258, 0.3063),
    (0.9450, 0.7261, 0.2886), (0.9739, 0.7314, 0.2666), (0.9938, 0.7455, 0.2403),
    (0.9990, 0.7653, 0.2164), (0.9955, 0.7861, 0.1967), (0.9880, 0.8066, 0.1794),
    (0.9789, 0.8271, 0.1633), (0.9697, 0.8481, 0.1475), (0.9626, 0.8705, 0.1309),
    (0.9589, 0.8949, 0.1132), (0.9598, 0.9218, 0.0948), (0.9661, 0.9514, 0.0755),
    (0.9763, 0.9831, 0.0538),
]

# Display label -> actual matplotlib registry name.  This avoids the old
# lower-casing bug for names such as RdBu, Set2 and tab20b.
_MPL_NAMES = {
    "Turbo": "turbo", "Jet": "jet",
    "Afmhot": "afmhot", "Gist Heat": "gist_heat", "Gray": "gray", "Greys": "Greys",
    "Purples": "Purples", "Blues": "Blues", "Greens": "Greens", "Oranges": "Oranges", "Reds": "Reds",
    "YlOrBr": "YlOrBr", "YlOrRd": "YlOrRd", "OrRd": "OrRd", "PuRd": "PuRd", "RdPu": "RdPu",
    "BuPu": "BuPu", "GnBu": "GnBu", "PuBu": "PuBu", "YlGnBu": "YlGnBu", "PuBuGn": "PuBuGn",
    "BuGn": "BuGn", "YlGn": "YlGn", "Coolwarm": "coolwarm", "Bwr": "bwr", "Seismic": "seismic",
    "Spectral": "Spectral", "RdBu": "RdBu", "RdGy": "RdGy", "RdYlBu": "RdYlBu", "RdYlGn": "RdYlGn",
    "PuOr": "PuOr", "BrBG": "BrBG", "PRGn": "PRGn", "PiYG": "PiYG", "Twilight Shifted": "twilight_shifted",
    "Phase": "twilight_shifted", "Cubehelix": "cubehelix", "Binary": "binary", "Gist Gray": "gist_gray",
    "Gist Yarg": "gist_yarg", "Terrain": "terrain", "Ocean": "ocean", "Gist Earth": "gist_earth",
    "Gist Ncar": "gist_ncar", "Gnuplot": "gnuplot", "Gnuplot2": "gnuplot2", "Nipy Spectral": "nipy_spectral",
    "Rainbow": "rainbow", "Gist Rainbow": "gist_rainbow", "Tab10": "tab10", "Tab20": "tab20",
    "Tab20b": "tab20b", "Tab20c": "tab20c", "Set1": "Set1", "Set2": "Set2", "Set3": "Set3",
    "Paired": "Paired", "Accent": "Accent", "Dark2": "Dark2", "Pastel1": "Pastel1", "Pastel2": "Pastel2",
}


def register_colourmaps() -> None:
    if "parula" not in matplotlib.colormaps:
        cmap = LinearSegmentedColormap.from_list("parula", _PARULA_64, N=256)
        matplotlib.colormaps.register(cmap, name="parula")
        matplotlib.colormaps.register(cmap.reversed(), name="parula_r")
    if "lines" not in matplotlib.colormaps:
        base = matplotlib.colormaps["tab10"]
        matplotlib.colormaps.register(ListedColormap([base(i) for i in range(10)], name="lines"), name="lines")
    if "colorcube" not in matplotlib.colormaps:
        cube = [
            "#000000", "#0000FF", "#00FF00", "#00FFFF", "#FF0000", "#FF00FF", "#FFFF00", "#FFFFFF",
            "#7F0000", "#007F00", "#00007F", "#7F7F00", "#7F007F", "#007F7F", "#BFBFBF", "#404040",
        ]
        matplotlib.colormaps.register(ListedColormap(cube, name="colorcube"), name="colorcube")
    if "white" not in matplotlib.colormaps:
        matplotlib.colormaps.register(LinearSegmentedColormap.from_list("white", ["#FFFFFF", "#FFFFFF"], N=256), name="white")


def _registry_name(display_name: str) -> str:
    raw = str(display_name or "").strip()
    if raw in _MPL_NAMES:
        return _MPL_NAMES[raw]
    key = raw.lower().replace(" ", "_")
    return key


def populate_colormap_combo(combo, current: str | None = None) -> None:
    from PySide6.QtCore import Qt
    combo.clear()
    model = combo.model()
    for category, names in COLOURMAP_CATEGORIES.items():
        combo.addItem(f"— {category} —")
        idx = combo.count() - 1
        item = model.item(idx)
        if item is not None:
            item.setEnabled(False)
            item.setData(category, Qt.ToolTipRole)
        for name in names:
            combo.addItem(name)
    target = current or "Parula"
    if combo.findText(target) >= 0:
        combo.setCurrentText(target)


def resolve_colourmap(name: str):
    register_colourmaps()
    key = _registry_name(name)
    try:
        return matplotlib.colormaps[key]
    except (KeyError, ValueError):
        # Last chance: exact display name for mixed-case registered maps.
        try:
            return matplotlib.colormaps[str(name)]
        except (KeyError, ValueError):
            return matplotlib.colormaps["viridis"]


register_colourmaps()
