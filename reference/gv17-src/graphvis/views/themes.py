"""GraphVis application themes.

All themes are generated from the same semantic palette so every input, popup,
tree, dock, toolbar and disabled state remains readable.  The theme engine is
UI-only; it never changes figure colours unless the user explicitly applies a
plot style/colormap.
"""
from __future__ import annotations

import colorsys
from dataclasses import dataclass


@dataclass(frozen=True)
class ThemeColours:
    background: str
    panel: str
    surface: str
    field: str
    text: str
    muted: str
    border: str
    accent: str
    accent_hover: str
    selection: str
    selection_text: str
    disabled_bg: str
    disabled_text: str
    tooltip_bg: str
    tooltip_text: str


def _qss(c: ThemeColours, *, radius: int = 4) -> str:
    """Compact desktop-studio theme inspired by modern design/CAE workspaces.

    Controls stay visually quiet until hovered/focused; panel chrome is only
    24 px high and toolbars use icon-sized buttons so the graph remains the
    dominant object on screen.
    """
    return f"""
    QMainWindow, QDialog {{ background: {c.background}; color: {c.text}; }}
    QWidget {{ color: {c.text}; font-size: 9pt; }}
    QWidget#centralWidget, QScrollArea, QScrollArea > QWidget > QWidget {{ background: {c.background}; }}
    QLabel {{ background: transparent; color: {c.text}; }}
    QLabel:disabled {{ color: {c.disabled_text}; }}

    QGroupBox {{
        background: transparent; color: {c.text}; border: 0;
        margin-top: 5px; padding: 4px 2px 2px 2px; font-weight: 600;
    }}
    QGroupBox::title {{ subcontrol-origin: margin; left: 3px; padding: 0 3px; color: {c.muted}; }}

    QPushButton, QToolButton {{
        color: {c.text}; background: {c.surface}; border: 1px solid {c.border};
        border-radius: {radius}px; padding: 3px 7px; min-height: 18px;
    }}
    QPushButton:hover, QToolButton:hover {{ background: {c.accent_hover}; border-color: {c.accent}; }}
    QPushButton:pressed, QToolButton:pressed {{ background: {c.selection}; }}
    QPushButton:checked, QToolButton:checked {{ background: {c.accent}; color: {c.selection_text}; border-color: {c.accent}; }}
    QPushButton:disabled, QToolButton:disabled {{ background: {c.disabled_bg}; color: {c.disabled_text}; border-color: {c.border}; }}
    QToolButton#GraphVisApplyChoiceButton {{ padding: 0; min-width: 25px; max-width: 25px; min-height: 25px; max-height: 25px; }}
    QToolButton#GraphVisApplyChoiceButton:enabled {{ background: {c.accent}; color: {c.selection_text}; border-color: {c.accent}; font-weight: 700; }}
    QPushButton#GraphVisApplyAllButton {{ background: {c.accent}; color: {c.selection_text}; border-color: {c.accent}; font-weight: 700; }}

    QToolButton#CompactChromeButton, QToolButton#CompactPanelChevron {{
        border: 0; background: transparent; padding: 0; min-height: 0; border-radius: 3px;
    }}
    QToolButton#CompactChromeButton:hover, QToolButton#CompactPanelChevron:hover {{ background: {c.accent_hover}; }}
    QLabel#CompactPanelTitle, QLabel#GraphVisDockTitle {{ color: {c.muted}; font-size: 8.5pt; font-weight: 600; }}

    QToolBar {{ spacing: 1px; padding: 2px; background: {c.panel}; border: 0; }}
    QToolBar::separator {{ background: {c.border}; width: 1px; height: 16px; margin: 4px 3px; }}
    QToolBar#GraphVisScientificTools QToolButton, QToolBar#GraphVisWindowLayoutToolbar QToolButton {{
        min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px;
        margin: 0; padding: 1px; border: 0; background: transparent;
    }}
    QToolBar#GraphVisScientificTools QToolButton:hover, QToolBar#GraphVisWindowLayoutToolbar QToolButton:hover {{
        background: {c.accent_hover}; border: 0; border-radius: 4px;
    }}

    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit {{
        color: {c.text}; background: {c.field}; border: 1px solid {c.border};
        border-radius: {max(radius-1, 2)}px; padding: 3px 5px;
        selection-background-color: {c.selection}; selection-color: {c.selection_text};
    }}
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,
    QTextEdit:focus, QPlainTextEdit:focus {{ border: 1px solid {c.accent}; }}
    QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled,
    QTextEdit:disabled, QPlainTextEdit:disabled {{ background: {c.disabled_bg}; color: {c.disabled_text}; }}
    QComboBox QAbstractItemView {{
        color: {c.text}; background: {c.field}; border: 1px solid {c.border};
        selection-background-color: {c.selection}; selection-color: {c.selection_text}; outline: 0;
    }}

    QListWidget, QTreeWidget, QTableWidget, QTableView, QTreeView {{
        color: {c.text}; background: {c.field}; alternate-background-color: {c.panel};
        border: 1px solid {c.border}; selection-background-color: {c.selection};
        selection-color: {c.selection_text}; outline: 0;
    }}
    QAbstractItemView::item {{ color: {c.text}; background: transparent; padding: 2px 3px; min-height: 18px; }}
    QAbstractItemView::item:hover {{ background: {c.accent_hover}; color: {c.text}; }}
    QAbstractItemView::item:selected {{ background: {c.selection}; color: {c.selection_text}; }}
    QAbstractItemView::item:disabled {{ color: {c.disabled_text}; }}
    QHeaderView::section {{
        color: {c.muted}; background: {c.panel}; border: 0; border-right: 1px solid {c.border};
        border-bottom: 1px solid {c.border}; padding: 3px 4px; font-weight: 600;
    }}

    QCheckBox, QRadioButton {{ color: {c.text}; background: transparent; spacing: 4px; }}
    QCheckBox:disabled, QRadioButton:disabled {{ color: {c.disabled_text}; }}

    QMenuBar, QMenu {{ color: {c.text}; background: {c.surface}; }}
    QMenuBar {{ border-bottom: 1px solid {c.border}; }}
    QMenuBar::item {{ background: transparent; padding: 3px 7px; }}
    QMenuBar::item:selected, QMenu::item:selected {{ background: {c.selection}; color: {c.selection_text}; }}
    QMenu::item {{ padding: 4px 26px 4px 20px; }}
    QMenu::item:disabled {{ color: {c.disabled_text}; }}
    QMenu::separator {{ height: 1px; background: {c.border}; margin: 3px 7px; }}

    QTabWidget::pane {{ border: 0; background: {c.surface}; }}
    QTabBar::tab {{
        color: {c.muted}; background: transparent; border: 0; border-right: 1px solid {c.border};
        padding: 5px 10px; min-width: 62px;
    }}
    QTabBar::tab:selected {{ background: {c.surface}; color: {c.text}; border-top: 2px solid {c.accent}; }}
    QTabBar::tab:hover:!selected {{ background: {c.accent_hover}; color: {c.text}; }}
    QToolButton#GraphVisTabScrollButton {{
        background: {c.surface}; border: 1px solid {c.border}; border-radius: 4px; padding: 1px; margin: 1px;
    }}
    QToolButton#GraphVisTabScrollButton:hover {{ background: {c.accent_hover}; border-color: {c.accent}; }}

    QDockWidget {{ color: {c.text}; background: {c.panel}; border: 0; }}
    QFrame#GraphVisDockTitleBar {{ background: {c.panel}; border: 0; border-bottom: 1px solid {c.border}; }}
    QFrame#DetachablePanelHeader {{ background: {c.panel}; border: 0; border-bottom: 1px solid {c.border}; }}
    QWidget#DetachablePanelContent {{ background: {c.background}; color: {c.text}; border: 0; }}
    QScrollArea#GraphControlStrip, QScrollArea#InteractiveControlStrip {{
        background: {c.panel}; color: {c.text}; border: 0;
    }}
    QScrollArea#GraphControlStrip > QWidget > QWidget,
    QScrollArea#InteractiveControlStrip > QWidget > QWidget,
    QWidget#GraphControlStripBody, QWidget#InteractiveControlStripBody {{
        background: {c.panel}; color: {c.text};
    }}
    QStatusBar {{ color: {c.muted}; background: {c.panel}; border-top: 1px solid {c.border}; min-height: 20px; }}
    QProgressBar {{ color: {c.text}; background: {c.field}; border: 1px solid {c.border}; border-radius: 3px; text-align: center; }}
    QProgressBar::chunk {{ background: {c.accent}; border-radius: 2px; }}

    QSplitter::handle {{ background: transparent; }}
    QSplitter::handle:hover {{ background: {c.accent_hover}; }}
    QSplitterHandle#GraphVisCompactSplitterHandle:hover {{ background: {c.accent_hover}; }}

    QScrollBar:vertical {{ background: transparent; width: 8px; margin: 0; }}
    QScrollBar::handle:vertical {{ background: {c.border}; min-height: 24px; border-radius: 3px; margin: 1px; }}
    QScrollBar::handle:vertical:hover {{ background: {c.accent}; }}
    QScrollBar:horizontal {{ background: transparent; height: 8px; margin: 0; }}
    QScrollBar::handle:horizontal {{ background: {c.border}; min-width: 24px; border-radius: 3px; margin: 1px; }}
    QScrollBar::handle:horizontal:hover {{ background: {c.accent}; }}
    QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}

    QFrame#GraphHoverCard {{ background: {c.surface}; border: 1px solid {c.accent}; border-radius: {radius}px; }}
    QLabel#GraphHoverText {{ color: {c.text}; }}
    QToolTip {{ color: {c.tooltip_text}; background: {c.tooltip_bg}; border: 1px solid {c.border}; padding: 4px; }}
    """


LIGHT = ThemeColours(
    background="#F3F6FA", panel="#F8FAFC", surface="#FFFFFF", field="#FFFFFF",
    text="#243447", muted="#667788", border="#CBD5E1", accent="#2E86C1",
    accent_hover="#EAF2F8", selection="#D6EAF8", selection_text="#17324D",
    disabled_bg="#EEF2F6", disabled_text="#8C9BAA", tooltip_bg="#FFFFFF", tooltip_text="#243447",
)
GLASS = ThemeColours(
    background="#EAF2F8", panel="#F4F9FC", surface="#FBFDFF", field="#FFFFFF",
    text="#17202A", muted="#5D6D7E", border="#AFC7D8", accent="#2471A3",
    accent_hover="#DDEFF8", selection="#C6E4F3", selection_text="#102A3A",
    disabled_bg="#E3EBF0", disabled_text="#718596", tooltip_bg="#FDFEFE", tooltip_text="#17202A",
)
GRAPHITE = ThemeColours(
    background="#20252B", panel="#272E36", surface="#2D3540", field="#1D232A",
    text="#F2F5F7", muted="#AEB9C4", border="#4A5968", accent="#6CB6FF",
    accent_hover="#33485C", selection="#356A93", selection_text="#FFFFFF",
    disabled_bg="#242B32", disabled_text="#788693", tooltip_bg="#15191E", tooltip_text="#F5F7FA",
)
MIDNIGHT = ThemeColours(
    background="#0E1621", panel="#121E2C", surface="#172536", field="#0B1420",
    text="#EAF4FF", muted="#9EB3C7", border="#2D465C", accent="#39A9FF",
    accent_hover="#163A55", selection="#14608D", selection_text="#FFFFFF",
    disabled_bg="#111C28", disabled_text="#6F8598", tooltip_bg="#08111A", tooltip_text="#EDF7FF",
)
HIGH_CONTRAST = ThemeColours(
    background="#000000", panel="#050505", surface="#000000", field="#000000",
    text="#FFFFFF", muted="#E0E0E0", border="#FFFFFF", accent="#00FFFF",
    accent_hover="#202020", selection="#FFFFFF", selection_text="#000000",
    disabled_bg="#111111", disabled_text="#B0B0B0", tooltip_bg="#000000", tooltip_text="#FFFFFF",
)

PAPER = ThemeColours(
    background="#EEEDE8", panel="#F4F2EC", surface="#FBFAF6", field="#FFFFFF",
    text="#2C2B29", muted="#726F69", border="#C8C3B8", accent="#3B6EA5",
    accent_hover="#E4EAF1", selection="#D6E1ED", selection_text="#1D3044",
    disabled_bg="#E8E5DE", disabled_text="#928E86", tooltip_bg="#FFFDF8", tooltip_text="#2C2B29",
)
WARM_GRAY = ThemeColours(
    background="#EEEAE6", panel="#F3F0EC", surface="#FAF8F5", field="#FFFDFC",
    text="#332F2B", muted="#766E67", border="#CFC6BE", accent="#8B5E3C",
    accent_hover="#EEE1D7", selection="#E3D2C5", selection_text="#36251A",
    disabled_bg="#E9E4DF", disabled_text="#948A82", tooltip_bg="#FFFDFC", tooltip_text="#332F2B",
)
SLATE = ThemeColours(
    background="#171C22", panel="#1E252D", surface="#242D37", field="#141A20",
    text="#E7EDF3", muted="#A1AFBC", border="#3C4A58", accent="#7AA2C8",
    accent_hover="#293A4B", selection="#355B7A", selection_text="#FFFFFF",
    disabled_bg="#1A2027", disabled_text="#74818D", tooltip_bg="#10151A", tooltip_text="#EEF3F8",
)
CARBON = ThemeColours(
    background="#111111", panel="#181818", surface="#202020", field="#0D0D0D",
    text="#F2F2F2", muted="#A7A7A7", border="#383838", accent="#B8C4D1",
    accent_hover="#2A2A2A", selection="#4A5661", selection_text="#FFFFFF",
    disabled_bg="#161616", disabled_text="#707070", tooltip_bg="#080808", tooltip_text="#F7F7F7",
)
DEEP_OCEAN = ThemeColours(
    background="#071820", panel="#0B222D", surface="#10303D", field="#06141B",
    text="#E7F7FA", muted="#8CB4BE", border="#214A57", accent="#41C7D9",
    accent_hover="#123D4B", selection="#176579", selection_text="#FFFFFF",
    disabled_bg="#0A1C24", disabled_text="#63858E", tooltip_bg="#041116", tooltip_text="#ECFAFC",
)


def _colour_theme(accent: str, tint: str, border: str) -> ThemeColours:
    return ThemeColours(
        background="#F7F8FA", panel=tint, surface="#FFFFFF", field="#FFFFFF",
        text="#26323D", muted="#687785", border=border, accent=accent,
        accent_hover=tint, selection=tint, selection_text="#17232D",
        disabled_bg="#EFF2F4", disabled_text="#8795A1", tooltip_bg="#FFFFFF", tooltip_text="#26323D",
    )


THEME_COLOURS: dict[str, ThemeColours] = {
    "Light": LIGHT,
    "Glass": GLASS,
    "Paper": PAPER,
    "Warm Gray": WARM_GRAY,
    "Graphite": GRAPHITE,
    "Slate": SLATE,
    "Carbon": CARBON,
    "Midnight": MIDNIGHT,
    "Deep Ocean": DEEP_OCEAN,
    "High Contrast": HIGH_CONTRAST,
    "Red": _colour_theme("#C0392B", "#FBE9E7", "#E6B8B2"),
    "Orange": _colour_theme("#D97706", "#FFF1DF", "#F0C58F"),
    "Yellow": _colour_theme("#B58900", "#FFF8D9", "#E9D886"),
    "Lime": _colour_theme("#65A30D", "#F1F9D8", "#C9DD91"),
    "Green": _colour_theme("#238636", "#E5F5E8", "#A8D3B0"),
    "Teal": _colour_theme("#0F8B8D", "#E1F4F3", "#9CCFCB"),
    "Cyan": _colour_theme("#0891B2", "#E0F5FA", "#9ED2DF"),
    "Blue": _colour_theme("#2563EB", "#E6EEFF", "#AFC2F0"),
    "Purple": _colour_theme("#7C3AED", "#F0E9FF", "#C7B3EB"),
    "Magenta": _colour_theme("#C026D3", "#F9E7FB", "#DFB5E5"),
    "Rose": _colour_theme("#BE4466", "#FBE8EE", "#E5B0BF"),
    "Coral": _colour_theme("#D85D4B", "#FCEAE6", "#E8B4AA"),
    "Amber": _colour_theme("#C78300", "#FFF1D5", "#E8C47A"),
    "Gold": _colour_theme("#A77B00", "#FFF6D7", "#DDC779"),
    "Olive": _colour_theme("#6F7D22", "#F1F3DA", "#C5CC8A"),
    "Emerald": _colour_theme("#11845B", "#E1F5EC", "#91CDB6"),
    "Mint": _colour_theme("#168C77", "#DFF6F0", "#95D3C5"),
    "Azure": _colour_theme("#1479B8", "#E2F1FB", "#9FCBE7"),
    "Indigo": _colour_theme("#4F55B8", "#E9EAFE", "#B2B5E8"),
    "Violet": _colour_theme("#8A4EB8", "#F2E8FA", "#CEAFE2"),
}

THEME_GROUPS: dict[str, list[str]] = {
    "Core": ["Light", "Glass", "Paper", "Warm Gray", "Graphite", "Slate", "Carbon", "Midnight", "Deep Ocean", "High Contrast"],
    "Colour themes": ["Red", "Orange", "Yellow", "Lime", "Green", "Teal", "Cyan", "Blue", "Purple", "Magenta",
                      "Rose", "Coral", "Amber", "Gold", "Olive", "Emerald", "Mint", "Azure", "Indigo", "Violet"],
}

THEMES: dict[str, str] = {name: _qss(colours) for name, colours in THEME_COLOURS.items()}

# Migrate saved names from earlier GraphVis builds without exposing duplicate
# entries in the UI.
THEME_ALIASES = {
    "GraphVis Light": "Light",
    "Scientific Light": "Light",
    "Glassmorphism": "Glass",
    "Neon Dark": "Midnight",
}


def canonical_theme_name(name: str) -> str:
    return THEME_ALIASES.get(str(name), str(name) if str(name) in THEMES else "Light")


def colours_for(name: str) -> ThemeColours:
    return THEME_COLOURS.get(canonical_theme_name(name), LIGHT)


def blend_colours(first: str, second: str, amount: float) -> str:
    """Blend two ``#RRGGBB`` colours without depending on Qt.

    ``amount=0`` returns ``first`` and ``amount=1`` returns ``second``.  Keeping
    this helper in the theme module lets the graph background controls and the
    thumbnail UI use exactly the same colour maths.
    """
    def rgb(value: str) -> tuple[int, int, int]:
        text = str(value or "#000000").lstrip("#")
        if len(text) == 3:
            text = "".join(ch * 2 for ch in text)
        try:
            return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]
        except Exception:
            return 0, 0, 0

    a = max(0.0, min(float(amount), 1.0))
    c1, c2 = rgb(first), rgb(second)
    out = tuple(round((1.0 - a) * x + a * y) for x, y in zip(c1, c2))
    return "#" + "".join(f"{max(0, min(255, int(v))):02X}" for v in out)


def colour_with_lightness(colour: str, lightness_percent: float) -> str:
    """Preserve hue/saturation while assigning an absolute HLS lightness."""
    text = str(colour or "#FFFFFF").lstrip("#")
    if len(text) == 3:
        text = "".join(ch * 2 for ch in text)
    try:
        r, g, b = (int(text[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    except Exception:
        r = g = b = 1.0
    h, _l, s = colorsys.rgb_to_hls(r, g, b)
    l = max(0.0, min(float(lightness_percent) / 100.0, 1.0))
    rr, gg, bb = colorsys.hls_to_rgb(h, l, s)
    return f"#{round(rr * 255):02X}{round(gg * 255):02X}{round(bb * 255):02X}"


def colour_lightness(colour: str) -> float:
    """Return HLS lightness as a percentage for a hex colour."""
    text = str(colour or "#FFFFFF").lstrip("#")
    if len(text) == 3:
        text = "".join(ch * 2 for ch in text)
    try:
        r, g, b = (int(text[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    except Exception:
        return 100.0
    return colorsys.rgb_to_hls(r, g, b)[1] * 100.0


def contrasting_graph_colours(background: str) -> dict[str, str]:
    """Choose readable graph chrome for a user-selected canvas background."""
    text = str(background or "#FFFFFF").lstrip("#")
    try:
        r, g, b = (int(text[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    except Exception:
        r = g = b = 1.0
    # WCAG-style relative luminance is more reliable than raw HLS lightness
    # for deciding whether axes/ticks need light or dark ink.
    def linear(v: float) -> float:
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    lum = 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b)
    if lum < 0.30:
        foreground = "#F3F7FA"
        muted = blend_colours(foreground, background, 0.28)
        border = blend_colours(foreground, background, 0.66)
        grid = blend_colours(foreground, background, 0.74)
    else:
        foreground = "#243447"
        muted = blend_colours(foreground, background, 0.26)
        border = blend_colours(foreground, background, 0.68)
        grid = blend_colours(foreground, background, 0.78)
    return {"text": foreground, "muted": muted, "border": border, "grid": grid}
