# =========================================================================
# export.py — multi-format, high-resolution export suite.
#
# Export re-renders the figure from its PlotSpec at the requested DPI
# (matplotlib scales fonts and line widths proportionally because they are
# defined in points), applies the live view state (zoom, 3-D angles,
# dragged legend position) and writes the file in a worker thread so the
# UI never blocks — a 3000 dpi TIFF can take substantial time and memory.
# =========================================================================
from __future__ import annotations

import os

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFormLayout,
                               QHBoxLayout, QLabel, QLineEdit, QPushButton)

from graphvis.data.loader import CancellableTask
from graphvis.rendering.render_core import PlotSpec, build_figure

EXPORT_FORMATS = {"PNG": "png", "JPG": "jpg", "TIFF": "tiff", "PDF": "pdf", "EPS": "eps", "SVG": "svg"}
RASTER_FORMATS = {"PNG", "JPG", "TIFF"}
DPI_TIERS = [100, 150, 300, 600, 900, 1200]
DPI_TIERS_EXTENDED = [100, 150, 300, 600, 900, 1200, 1500, 1800, 2400, 3000]


def estimate_raster_bytes(figsize, dpi: int) -> int:
    """Peak memory for an Agg raster + PIL copy at the export size."""
    w_px, h_px = figsize[0] * dpi, figsize[1] * dpi
    return int(w_px * h_px * 4 * 2.2)


def available_memory_bytes() -> int | None:
    try:
        import psutil
        return int(psutil.virtual_memory().available)
    except Exception:
        return None


def memory_verdict(figsize, dpi: int, fmt: str) -> tuple[bool, str]:
    if fmt not in RASTER_FORMATS:
        return True, "Vector output — DPI only affects rasterised layers (heatmaps)."
    need = estimate_raster_bytes(figsize, dpi)
    w_px, h_px = int(figsize[0] * dpi), int(figsize[1] * dpi)
    raw_bytes = max(w_px, 0) * max(h_px, 0) * 4
    avail = available_memory_bytes()
    txt = (
        f"{w_px:,} × {h_px:,} px, ≈{need / 1e6:,.0f} MB peak RAM; "
        f"raw RGBA ≈{raw_bytes / 1e6:,.0f} MB before file compression"
    )
    if avail is not None:
        txt += f" (≈{avail / 1e9:,.1f} GB free)"
        if need > 0.6 * avail:
            return True, txt + " — WARNING: this exceeds 60% of currently free RAM; GraphVis will still attempt it and the export may fail or heavily page to disk."
    if need > 12.0e9:
        return True, txt + " — WARNING: very large raster allocation; GraphVis will attempt it, but a vector format may be substantially safer/smaller."
    if dpi > 3000:
        return True, txt + " — High custom DPI: file size, render time and memory use can increase sharply."
    return True, txt


def apply_publication_font_rc() -> dict:
    """Journal-compliant font handling for vector exports.

    PDF/EPS embed TrueType (Type 42) fonts instead of converting text to
    Type 3 bitmapped glyphs, and SVG keeps real selectable/editable text —
    matching standard academic submission requirements.  Returns the previous
    rcParams so callers can restore them.
    """
    import matplotlib
    previous = {k: matplotlib.rcParams.get(k) for k in ("pdf.fonttype", "ps.fonttype", "svg.fonttype")}
    matplotlib.rcParams["pdf.fonttype"] = 42
    matplotlib.rcParams["ps.fonttype"] = 42
    matplotlib.rcParams["svg.fonttype"] = "none"
    return previous


def savefig_kwargs(fmt: str, dpi: int, transparent: bool) -> dict:
    kw = dict(dpi=dpi, bbox_inches="tight", pad_inches=0.05)
    if fmt == "JPG":
        kw.update(pil_kwargs={"quality": 95, "optimize": True}, facecolor="white")
    elif fmt == "TIFF":
        kw.update(pil_kwargs={"compression": "tiff_lzw"}, facecolor="white" if not transparent else "none")
    elif fmt == "PNG":
        kw.update(transparent=transparent, facecolor="none" if transparent else "white")
    elif fmt == "EPS":
        kw.update(facecolor="white")          # EPS cannot carry alpha
    else:  # PDF / SVG
        kw.update(transparent=transparent, facecolor="none" if transparent else "white")
    return kw


class ExportTask(CancellableTask):
    def __init__(self, spec: PlotSpec, path: str, fmt: str, dpi: int, transparent: bool = False,
                 color_mode: str = "RGB", icc_profile: str = ""):
        super().__init__(f"export:{os.path.basename(path)}")
        self.spec, self.path, self.fmt, self.dpi, self.transparent = spec, path, fmt, dpi, transparent
        self.color_mode = str(color_mode or "RGB").upper()
        self.icc_profile = str(icc_profile or "").strip()

    def execute(self):
        self.stage(f"Rendering at {self.dpi} dpi…", 20)
        import matplotlib
        rc_backup = apply_publication_font_rc()
        try:
            res = build_figure(self.spec, dpi=self.dpi, for_export=True)
            self.stage(f"Writing {self.fmt}…", 70)
            kw = savefig_kwargs(self.fmt, self.dpi, self.transparent)
            # Data-provenance stamp: the spec hash uniquely identifies the
            # parameters + datasets that produced this figure.
            provenance = self.spec.cache_key(dpi=self.dpi)
            if self.fmt in ("PNG", "PDF", "SVG"):
                kw["metadata"] = {"Title": self.spec.chart_type,
                                  ("Subject" if self.fmt != "PNG" else "Description"):
                                      f"GraphVis provenance {provenance}"}
            res.fig.savefig(self.path, format=EXPORT_FORMATS[self.fmt], **kw)
        finally:
            for key, value in rc_backup.items():
                if value is not None:
                    matplotlib.rcParams[key] = value
        try:
            import json
            with open(self.path + ".provenance.json", "w", encoding="utf-8") as fh:
                json.dump({"provenance_hash": provenance, "chart_type": self.spec.chart_type,
                           "dpi": self.dpi, "format": self.fmt,
                           "mappings": {k: v for k, v in (self.spec.mappings or {}).items() if v},
                           "datasets": list(self.spec.datasets.keys()),
                           "exported_utc": __import__("datetime").datetime.now(
                               __import__("datetime").timezone.utc).isoformat()}, fh, indent=2)
        except Exception:
            pass
        warnings = list(res.warnings)
        if self.color_mode == "CMYK":
            if self.fmt in ("JPG", "TIFF"):
                from PIL import Image
                with Image.open(self.path) as src:
                    src.load()
                    converted = None
                    icc_bytes = None
                    if self.icc_profile and os.path.isfile(self.icc_profile):
                        try:
                            from PIL import ImageCms
                            srgb = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB"))
                            target = ImageCms.getOpenProfile(self.icc_profile)
                            converted = ImageCms.profileToProfile(src.convert("RGB"), srgb, target, outputMode="CMYK")
                            with open(self.icc_profile, "rb") as pf:
                                icc_bytes = pf.read()
                        except Exception as exc:
                            warnings.append(f"ICC conversion failed ({exc}); falling back to generic CMYK pixel mode.")
                    if converted is None:
                        converted = src.convert("CMYK")
                    save_kw = {"dpi": (self.dpi, self.dpi)}
                    if icc_bytes:
                        save_kw["icc_profile"] = icc_bytes
                    if self.fmt == "JPG":
                        converted.save(self.path, format="JPEG", quality=95, optimize=True, **save_kw)
                    else:
                        converted.save(self.path, format="TIFF", compression="tiff_lzw", **save_kw)
            else:
                warnings.append("CMYK/ICC conversion is available for TIFF/JPG raster export; this format was written in RGB.")
        self.stage("Done", 100)
        size = os.path.getsize(self.path) if os.path.exists(self.path) else 0
        return {"path": self.path, "bytes": size, "dpi": self.dpi, "format": self.fmt,
                "color_mode": self.color_mode if self.fmt in ("JPG", "TIFF") else "RGB", "warnings": warnings}


class ExportDialog(QDialog):
    def __init__(self, figsize, default_fmt="PNG", default_dpi=300, default_dir="", parent=None):
        super().__init__(parent)
        self.setWindowTitle("Export figure")
        self.setMinimumWidth(520)
        self.figsize = figsize
        form = QFormLayout(self)

        self.cb_fmt = QComboBox()
        self.cb_fmt.addItems(list(EXPORT_FORMATS))
        self.cb_fmt.setCurrentText(default_fmt if default_fmt in EXPORT_FORMATS else "PNG")
        form.addRow("Format:", self.cb_fmt)

        self.cb_dpi = QComboBox()
        self.cb_dpi.setEditable(True)
        self.cb_dpi.addItems([str(t) for t in DPI_TIERS])
        self.cb_dpi.setCurrentText(str(max(1, int(default_dpi or 300))))
        self.cb_dpi.setToolTip("Standard publication tiers are listed; any positive custom DPI can be typed. "
                               "Fonts and line widths scale proportionally with DPI because they are defined in points. "
                               "Extremely large values can consume substantial RAM, disk space and render time.")
        self.txt_dpi = self.cb_dpi.lineEdit()   # back-compat alias for code/tests reading txt_dpi
        form.addRow("Resolution (DPI):", self.cb_dpi)

        self.cb_color_mode = QComboBox()
        self.cb_color_mode.addItems(["RGB", "CMYK (TIFF/JPG)"])
        self.cb_color_mode.setToolTip("CMYK raster output supports an optional publisher-supplied ICC profile.")
        form.addRow("Colour mode:", self.cb_color_mode)

        icc_row = QHBoxLayout()
        self.txt_icc = QLineEdit()
        self.txt_icc.setPlaceholderText("Optional CMYK ICC/ICM profile")
        b_icc = QPushButton("Browse…")
        b_icc.clicked.connect(self._browse_icc)
        icc_row.addWidget(self.txt_icc, 1); icc_row.addWidget(b_icc)
        self._icc_button = b_icc
        form.addRow("ICC profile:", icc_row)

        self.chk_transparent = QCheckBox("Transparent background (PNG / PDF / SVG / TIFF)")
        form.addRow(self.chk_transparent)

        self.cb_vector_surface = QComboBox()
        self.cb_vector_surface.addItems(["Hybrid (vector text + raster field)", "Full vector surface"])
        self.cb_vector_surface.setToolTip(
            "PDF/SVG always keep labels, ticks, lines and annotations as vectors. Hybrid rasterizes only dense heatmap/surface tiles for smaller files; Full vector preserves every field polygon and can be very large."
        )
        form.addRow("Vector surface detail:", self.cb_vector_surface)

        path_row = QHBoxLayout()
        self.txt_path = QLineEdit(os.path.join(default_dir or os.getcwd(), "figure.png"))
        b_browse = QPushButton("Browse…")
        b_browse.clicked.connect(self._browse)
        path_row.addWidget(self.txt_path, 1)
        path_row.addWidget(b_browse)
        form.addRow("Save as:", path_row)

        self.lbl_mem = QLabel()
        self.lbl_mem.setWordWrap(True)
        self.lbl_mem.setStyleSheet("font-size:10px;")
        form.addRow("", self.lbl_mem)

        self.buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        self.buttons.button(QDialogButtonBox.Ok).setText("Export")
        self.buttons.accepted.connect(self.accept)
        self.buttons.rejected.connect(self.reject)
        form.addRow(self.buttons)

        self.cb_fmt.currentTextChanged.connect(self._refresh)
        self.txt_dpi.textChanged.connect(self._refresh)
        self.cb_color_mode.currentTextChanged.connect(self._refresh)
        self._refresh()

    def _browse_icc(self):
        path, _ = QFileDialog.getOpenFileName(self, "Select CMYK ICC profile", "", "ICC profiles (*.icc *.icm);;All files (*)")
        if path:
            self.txt_icc.setText(path)

    def _browse(self):
        fmt = self.cb_fmt.currentText()
        ext = EXPORT_FORMATS[fmt]
        path, _ = QFileDialog.getSaveFileName(self, f"Export {fmt}", self.txt_path.text(), f"{fmt} (*.{ext})")
        if path:
            self.txt_path.setText(path)

    def _refresh(self):
        fmt = self.cb_fmt.currentText()
        ext = EXPORT_FORMATS[fmt]
        base, _ = os.path.splitext(self.txt_path.text() or "figure")
        self.txt_path.setText(f"{base}.{ext}")
        self.chk_transparent.setEnabled(fmt not in ("JPG", "EPS"))
        self.cb_vector_surface.setEnabled(fmt in ("PDF", "SVG", "EPS"))
        cmyk = self.cb_color_mode.currentText().startswith("CMYK")
        self.txt_icc.setEnabled(cmyk and fmt in ("JPG", "TIFF"))
        self._icc_button.setEnabled(cmyk and fmt in ("JPG", "TIFF"))
        if cmyk and fmt not in ("JPG", "TIFF"):
            colour_note = " CMYK/ICC conversion applies only to TIFF/JPG; vector/PNG outputs remain RGB."
        elif cmyk and self.txt_icc.text().strip():
            colour_note = " Publisher ICC conversion will be attempted before writing the raster file."
        elif cmyk:
            colour_note = " No ICC selected: generic CMYK pixel mode will be used."
        else:
            colour_note = ""
        try:
            dpi = self.dpi()
            if dpi <= 0:
                raise ValueError
        except Exception:
            self.lbl_mem.setText("Enter a positive whole-number DPI. Values above 3000 are allowed.")
            self.lbl_mem.setStyleSheet("color:#C0392B;font-size:10px;")
            self.buttons.button(QDialogButtonBox.Ok).setEnabled(False)
            return
        ok, txt = memory_verdict(self.figsize, dpi, fmt)
        self.lbl_mem.setText(txt + colour_note)
        warning = "WARNING:" in txt or dpi > 3000
        self.lbl_mem.setStyleSheet(f"color:{'#C47A1A' if warning else '#7F8C8D'};font-size:10px;")
        self.buttons.button(QDialogButtonBox.Ok).setEnabled(True)

    def dpi(self) -> int:
        value = int(self.txt_dpi.text().strip())
        if value <= 0:
            raise ValueError("DPI must be positive")
        return value

    def options(self) -> dict:
        return {"format": self.cb_fmt.currentText(), "dpi": self.dpi(),
                "transparent": self.chk_transparent.isChecked() and self.chk_transparent.isEnabled(),
                "color_mode": "CMYK" if self.cb_color_mode.currentText().startswith("CMYK") else "RGB",
                "icc_profile": self.txt_icc.text().strip() if self.cb_color_mode.currentText().startswith("CMYK") else "",
                "vector_surface_mode": self.cb_vector_surface.currentText(),
                "path": self.txt_path.text().strip()}

class BatchExportTask(CancellableTask):
    """Render/export a list of independent PlotSpecs sequentially on one worker."""
    def __init__(self, jobs: list[dict], directory: str, fmt: str = "PNG", dpi: int = 300,
                 color_mode: str = "RGB", icc_profile: str = "") -> None:
        super().__init__("batch-export")
        self.jobs = list(jobs)
        self.directory = directory
        self.fmt = fmt if fmt in EXPORT_FORMATS else "PNG"
        self.dpi = int(dpi)
        self.color_mode = str(color_mode or "RGB").upper()
        self.icc_profile = str(icc_profile or "")

    @staticmethod
    def _safe_name(name: str) -> str:
        import re
        text = re.sub(r"[^A-Za-z0-9._ -]+", "_", str(name)).strip(" ._")
        return text or "figure"

    def execute(self) -> list[dict]:
        os.makedirs(self.directory, exist_ok=True)
        results: list[dict] = []
        total = max(len(self.jobs), 1)
        for i, job in enumerate(self.jobs, 1):
            if self.cancelled:
                raise Exception("Cancelled")
            name = self._safe_name(job.get("name", f"figure_{i}"))
            spec = job["spec"]
            ext = EXPORT_FORMATS[self.fmt]
            path = os.path.join(self.directory, f"{name}.{ext}")
            self.stage(f"Exporting {i}/{total}: {name}", int(5 + 90 * (i - 1) / total))
            task = ExportTask(spec, path, self.fmt, self.dpi, False, self.color_mode, self.icc_profile)
            results.append(task.execute())
        self.stage("Batch export complete", 100)
        return results
