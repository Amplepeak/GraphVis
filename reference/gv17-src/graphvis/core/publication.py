"""Publication-ready styling profiles and project-persistent profile storage."""
from __future__ import annotations

import json
import re
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

from graphvis.rendering.render_core import DEFAULT_STYLING, PlotSpec


@dataclass(slots=True)
class PublicationProfile:
    name: str
    dpi: int = 600
    font_family: str = "Arial"
    base_font_size: int = 8
    title_size: int = 9
    axis_label_size: int = 8
    tick_size: int = 7
    legend_size: int = 7
    line_width: float = 1.2
    marker_size: float = 4.0
    margin_padding: float = 0.08
    color_space: str = "RGB"
    figure_width_in: float = 3.5
    figure_height_in: float = 2.7
    grid_visible: bool = False
    legend_loc: str = "best"
    notes: str = ""

    def normalized(self) -> "PublicationProfile":
        self.dpi = max(1, int(self.dpi))
        self.base_font_size = int(max(5, min(36, self.base_font_size)))
        self.line_width = float(max(0.2, min(8.0, self.line_width)))
        self.color_space = "CMYK" if str(self.color_space).upper() == "CMYK" else "RGB"
        return self

    def apply_to_spec(self, spec: PlotSpec) -> PlotSpec:
        p = self.normalized()
        sp = spec.clone()
        sp.font_size = p.base_font_size
        sp.line_width = p.line_width
        sp.grid_visible = p.grid_visible
        sp.legend_fontsize = p.legend_size
        sp.legend_loc = p.legend_loc
        sp.label_padding = max(2, int(round(72 * p.margin_padding)))
        sp.figsize = (p.figure_width_in, p.figure_height_in)
        sp.publication_profile = p.name
        sp.export_dpi = p.dpi
        sp.export_color_mode = p.color_space
        for key, size in (("Main Title", p.title_size), ("X-Axis Label", p.axis_label_size),
                          ("Y-Axis Label", p.axis_label_size), ("Z-Axis Label", p.axis_label_size),
                          ("Tick Labels", p.tick_size), ("Legend", p.legend_size),
                          ("Colourbar Title", p.axis_label_size)):
            sp.styling.setdefault(key, dict(DEFAULT_STYLING.get(key, {})))
            sp.styling[key]["size"] = size
            sp.styling[key]["font_family"] = p.font_family
        return sp

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "PublicationProfile":
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__}).normalized()


BUILTIN_PROFILES: dict[str, PublicationProfile] = {
    "Nature single-column": PublicationProfile(
        "Nature single-column", dpi=600, font_family="Arial", base_font_size=6,
        title_size=7, axis_label_size=6, tick_size=5, legend_size=5, line_width=0.75,
        marker_size=3.5, margin_padding=0.05, color_space="RGB",
        figure_width_in=89.0/25.4, figure_height_in=2.55, grid_visible=False,
        notes=("Nature baseline: 89 mm single-column width; standard Arial/Helvetica; ordinary text 5–7 pt; "
               "lines 0.25–1 pt; RGB; vector PDF/EPS preferred. 600 dpi is a conservative GraphVis raster default.")),
    "Nature double-column": PublicationProfile(
        "Nature double-column", dpi=600, font_family="Arial", base_font_size=6,
        title_size=7, axis_label_size=6, tick_size=5, legend_size=5, line_width=0.75,
        marker_size=3.5, margin_padding=0.05, color_space="RGB",
        figure_width_in=183.0/25.4, figure_height_in=5.4, grid_visible=False,
        notes="Nature baseline: 183 mm double-column width, 5–7 pt figure text and 0.25–1 pt lines."),
    "Science (AAAS) compact": PublicationProfile(
        "Science (AAAS) compact", dpi=300, font_family="Arial", base_font_size=7,
        title_size=8, axis_label_size=7, tick_size=6, legend_size=6, line_width=0.8,
        marker_size=4.0, margin_padding=0.06, color_space="RGB",
        figure_width_in=57.0/25.4, figure_height_in=2.5, grid_visible=False,
        notes=("Science/AAAS compact baseline: editable/vector artwork preferred, 300 dpi raster baseline, compact "
               "high-contrast labels. Verify the current target Science-family journal instructions before final submission.")),
    "IEEE journal single-column": PublicationProfile(
        "IEEE journal single-column", dpi=600, font_family="Arial", base_font_size=9,
        title_size=10, axis_label_size=9, tick_size=8, legend_size=8, line_width=1.0,
        marker_size=4.0, margin_padding=0.06, color_space="RGB",
        figure_width_in=3.5, figure_height_in=2.75, grid_visible=False,
        notes=("IEEE baseline: 3.5 in single-column width; graphics text approximately 9–10 pt at full size; "
               ">300 dpi color/grayscale and >600 dpi line art; PS/EPS/PDF vector preferred.")),
    "IEEE journal double-column": PublicationProfile(
        "IEEE journal double-column", dpi=600, font_family="Arial", base_font_size=9,
        title_size=10, axis_label_size=9, tick_size=8, legend_size=8, line_width=1.0,
        marker_size=4.0, margin_padding=0.06, color_space="RGB",
        figure_width_in=7.16, figure_height_in=4.8, grid_visible=False,
        notes="IEEE baseline: 7.16 in two-column width; >300 dpi color/grayscale and >600 dpi line art."),
    "Elsevier single-column line art": PublicationProfile(
        "Elsevier single-column line art", dpi=1000, font_family="Arial", base_font_size=7,
        title_size=8, axis_label_size=7, tick_size=7, legend_size=7, line_width=1.0,
        marker_size=4.0, margin_padding=0.06, color_space="RGB",
        figure_width_in=90.0/25.4, figure_height_in=2.8, grid_visible=False,
        notes=("Elsevier general artwork baseline: ~90 mm single-column; normal lettering ~7 pt; prominent graph "
               "lines ~1 pt (0.25 pt recommended line work minimum); 1000 dpi line art / 500 dpi combination / 300 dpi halftone. "
               "Journal-specific Elsevier instructions take precedence.")),
    "APA 7 figure": PublicationProfile(
        "APA 7 figure", dpi=300, font_family="Arial", base_font_size=10,
        title_size=11, axis_label_size=10, tick_size=9, legend_size=9, line_width=1.0,
        marker_size=4.5, margin_padding=0.08, color_space="RGB",
        figure_width_in=6.5, figure_height_in=4.0, grid_visible=False,
        notes=("APA 7 figure baseline: simple sans-serif figure text 8–14 pt, clear dark labels and restrained decoration. "
               "APA specifies sufficient print/view resolution rather than one universal DPI; GraphVis uses 300 dpi as a practical baseline.")),
}



class PublicationProfileStore:
    def __init__(self, directory: str | Path):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)

    @staticmethod
    def _safe(name: str) -> str:
        return re.sub(r"[^A-Za-z0-9._ -]+", "_", name).strip(" ._") or "profile"

    def save(self, profile: PublicationProfile) -> Path:
        path = self.directory / f"{self._safe(profile.name)}.json"
        path.write_text(json.dumps(profile.to_dict(), indent=2, ensure_ascii=False), encoding="utf-8")
        return path

    def load(self, name_or_path: str | Path) -> PublicationProfile:
        p = Path(name_or_path)
        if not p.exists():
            p = self.directory / f"{self._safe(str(name_or_path))}.json"
        return PublicationProfile.from_dict(json.loads(p.read_text(encoding="utf-8")))

    def list(self) -> list[PublicationProfile]:
        profiles = [PublicationProfile.from_dict(p.to_dict()) for p in BUILTIN_PROFILES.values()]
        for path in sorted(self.directory.glob("*.json")):
            try:
                p = PublicationProfile.from_dict(json.loads(path.read_text(encoding="utf-8")))
                profiles = [q for q in profiles if q.name != p.name]
                profiles.append(p)
            except Exception:
                continue
        return profiles

    def delete(self, name: str) -> bool:
        """Delete a custom project profile; built-in profiles are immutable."""
        if name in BUILTIN_PROFILES:
            return False
        path = self.directory / f"{self._safe(name)}.json"
        if not path.exists():
            return False
        path.unlink()
        return True
