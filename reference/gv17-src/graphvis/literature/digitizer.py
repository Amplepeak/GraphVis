# =========================================================================
# digitizer.py - optical figure digitizer with 3-point calibration, manual
# point picking, magnifier loupe and colour-threshold auto tracing.
# =========================================================================
from __future__ import annotations

import os
from dataclasses import dataclass

import numpy as np
import pandas as pd
from PIL import Image
from PySide6.QtCore import QPoint, Qt, Signal
from PySide6.QtGui import QColor, QImage, QPainter, QPen, QPixmap
from PySide6.QtWidgets import (QCheckBox, QColorDialog, QComboBox, QDialog, QDialogButtonBox,
                               QFileDialog, QFormLayout, QHBoxLayout, QLabel, QLineEdit,
                               QMessageBox, QPushButton, QSpinBox, QVBoxLayout, QWidget, QInputDialog)


@dataclass
class Calibration:
    origin_px: tuple[float, float]
    xref_px: tuple[float, float]
    yref_px: tuple[float, float]
    origin_value: tuple[float, float]
    xref_value: float
    yref_value: float
    x_log: bool = False
    y_log: bool = False

    def map(self, px: float, py: float) -> tuple[float, float]:
        # Treat the two calibrated pixel axes as an affine basis. This works for
        # ordinary horizontal/vertical plots and for rotated or mildly skewed
        # screenshots, unlike independent screen-X/screen-Y interpolation.
        origin = np.asarray(self.origin_px, dtype=float)
        ex = np.asarray(self.xref_px, dtype=float) - origin
        ey = np.asarray(self.yref_px, dtype=float) - origin
        basis = np.column_stack([ex, ey])
        if abs(float(np.linalg.det(basis))) < 1e-10:
            raise ValueError("Calibration points are degenerate or nearly collinear.")
        fx, fy = np.linalg.solve(basis, np.asarray([px, py], dtype=float) - origin)
        x0, y0 = self.origin_value
        if self.x_log:
            if x0 <= 0 or self.xref_value <= 0:
                raise ValueError("Log X calibration values must be positive.")
            lx = np.log10(x0) + fx * (np.log10(self.xref_value) - np.log10(x0))
            xv = 10 ** lx
        else:
            xv = x0 + fx * (self.xref_value - x0)
        if self.y_log:
            if y0 <= 0 or self.yref_value <= 0:
                raise ValueError("Log Y calibration values must be positive.")
            ly = np.log10(y0) + fy * (np.log10(self.yref_value) - np.log10(y0))
            yv = 10 ** ly
        else:
            yv = y0 + fy * (self.yref_value - y0)
        return float(xv), float(yv)


@dataclass
class PolarCalibration:
    center_px: tuple[float, float]
    radial_ref_px: tuple[float, float]
    angular_ref_px: tuple[float, float]
    radius_origin: float = 0.0
    radius_ref: float = 1.0
    angular_ref_deg: float = 90.0
    radius_log: bool = False

    def map(self, px: float, py: float) -> tuple[float, float]:
        c = np.asarray(self.center_px, float)
        vr = np.asarray(self.radial_ref_px, float) - c
        va = np.asarray(self.angular_ref_px, float) - c
        v = np.asarray([px, py], float) - c
        rr = float(np.linalg.norm(vr))
        if rr <= 1e-12:
            raise ValueError("Polar radial reference cannot coincide with the centre.")
        def signed_angle(a, b):
            return float(np.arctan2(a[0]*b[1]-a[1]*b[0], np.dot(a,b)))
        observed_ref = signed_angle(vr, va)
        if abs(observed_ref) <= 1e-9:
            raise ValueError("Polar angular reference is collinear with the radial reference.")
        theta = signed_angle(vr, v) * (float(self.angular_ref_deg) / observed_ref)
        radius_fraction = float(np.linalg.norm(v)) / rr
        if self.radius_log:
            if self.radius_origin <= 0 or self.radius_ref <= 0:
                raise ValueError("Logarithmic polar radius calibration requires positive radius values.")
            radius = 10 ** (np.log10(self.radius_origin) + radius_fraction * (np.log10(self.radius_ref)-np.log10(self.radius_origin)))
        else:
            radius = self.radius_origin + radius_fraction * (self.radius_ref-self.radius_origin)
        return float(np.degrees(theta) if abs(self.angular_ref_deg) <= 2*np.pi + 1e-9 else theta), float(radius)


@dataclass
class TernaryCalibration:
    a_px: tuple[float, float]
    b_px: tuple[float, float]
    c_px: tuple[float, float]

    def map(self, px: float, py: float) -> tuple[float, float, float]:
        # Solve p = A*a + B*b + C*c with A+B+C=1.
        M = np.array([[self.a_px[0], self.b_px[0], self.c_px[0]],
                      [self.a_px[1], self.b_px[1], self.c_px[1]],
                      [1.0, 1.0, 1.0]], dtype=float)
        if abs(float(np.linalg.det(M))) < 1e-12:
            raise ValueError("Ternary vertex calibration is degenerate.")
        w = np.linalg.solve(M, np.array([px, py, 1.0], dtype=float))
        return float(w[0]), float(w[1]), float(w[2])


class DigitizerCanvas(QLabel):
    clicked = Signal(float, float)
    moved = Signal(float, float)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMouseTracking(True)
        self.setAlignment(Qt.AlignCenter)
        self.setMinimumSize(640, 420)
        self._pixmap_original: QPixmap | None = None
        self._points: list[tuple[float, float, QColor]] = []

    def set_source_pixmap(self, pix: QPixmap):
        self._pixmap_original = pix
        self._points.clear()
        self._refresh()

    def resizeEvent(self, ev):
        super().resizeEvent(ev)
        self._refresh()

    def _refresh(self):
        if self._pixmap_original is None:
            return
        target = self._pixmap_original.scaled(self.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation)
        canvas = QPixmap(target)
        p = QPainter(canvas)
        p.setRenderHint(QPainter.Antialiasing)
        sx = target.width() / max(self._pixmap_original.width(), 1)
        sy = target.height() / max(self._pixmap_original.height(), 1)
        for x, y, col in self._points:
            p.setPen(QPen(col, 2))
            p.drawEllipse(QPoint(int(x * sx), int(y * sy)), 5, 5)
        p.end()
        self.setPixmap(canvas)

    def add_point(self, x, y, color=QColor("#E74C3C")):
        self._points.append((float(x), float(y), QColor(color)))
        self._refresh()

    def clear_points(self):
        self._points.clear()
        self._refresh()

    def _widget_to_image(self, pos):
        if self._pixmap_original is None or self.pixmap() is None:
            return None
        shown = self.pixmap().size()
        x0 = (self.width() - shown.width()) / 2
        y0 = (self.height() - shown.height()) / 2
        x = pos.x() - x0
        y = pos.y() - y0
        if x < 0 or y < 0 or x >= shown.width() or y >= shown.height():
            return None
        return (x * self._pixmap_original.width() / shown.width(),
                y * self._pixmap_original.height() / shown.height())

    def mousePressEvent(self, ev):
        if ev.button() == Qt.LeftButton:
            pt = self._widget_to_image(ev.position())
            if pt:
                self.clicked.emit(*pt)
        super().mousePressEvent(ev)

    def mouseMoveEvent(self, ev):
        pt = self._widget_to_image(ev.position())
        if pt:
            self.moved.emit(*pt)
        super().mouseMoveEvent(ev)


class FigureDigitizerDialog(QDialog):
    """Integrated raster curve digitizer.

    Workflow: select origin, X reference and Y reference, enter their data
    values, then manually click curve points or auto-trace a selected colour.
    """
    def __init__(self, image_path: str | None = None, parent=None):
        super().__init__(parent)
        self.setWindowTitle("GraphVis Figure Digitizer")
        self.resize(1180, 760)
        self.image_path = image_path
        self.image_rgb: np.ndarray | None = None
        self.cal_points: list[tuple[float, float]] = []
        self.manual_points: list[tuple[float, float]] = []
        self.trace_points: list[tuple[float, float]] = []
        self.pick_mode = "calibration"
        self.trace_color = QColor("#1f77b4")
        self._build_ui()
        if image_path:
            self.load_image(image_path)

    def _build_ui(self):
        root = QHBoxLayout(self)
        left = QVBoxLayout()
        self.canvas = DigitizerCanvas()
        self.canvas.clicked.connect(self._canvas_click)
        self.canvas.moved.connect(self._update_loupe)
        left.addWidget(self.canvas, 1)
        self.lbl_status = QLabel("Load an image, then click Origin → X reference → Y reference.")
        left.addWidget(self.lbl_status)
        root.addLayout(left, 1)

        side = QVBoxLayout()
        load = QPushButton("Load image / PDF page…")
        load.clicked.connect(self._browse)
        side.addWidget(load)
        self.loupe = QLabel("Loupe")
        self.loupe.setFixedSize(180, 180)
        self.loupe.setAlignment(Qt.AlignCenter)
        self.loupe.setStyleSheet("border:1px solid #BFC9CA;background:white;")
        side.addWidget(self.loupe, alignment=Qt.AlignHCenter)

        form = QFormLayout()
        self.cb_coordinates = QComboBox(); self.cb_coordinates.addItems(["Cartesian", "Polar", "Ternary"])
        self.cb_coordinates.setToolTip("Cartesian: origin/X-ref/Y-ref. Polar: centre/radial-ref/angular-ref. Ternary: vertices A/B/C.")
        self.cb_coordinates.currentTextChanged.connect(self._coordinate_mode_changed)
        form.addRow("Coordinate system:", self.cb_coordinates)
        self.x0 = QLineEdit("0"); self.y0 = QLineEdit("0")
        self.x1 = QLineEdit("1"); self.y1 = QLineEdit("1")
        self.lbl_x0 = QLabel("Origin X value:"); self.lbl_y0 = QLabel("Origin Y value:")
        self.lbl_x1 = QLabel("X-reference value:"); self.lbl_y1 = QLabel("Y-reference value:")
        form.addRow(self.lbl_x0, self.x0); form.addRow(self.lbl_y0, self.y0)
        form.addRow(self.lbl_x1, self.x1); form.addRow(self.lbl_y1, self.y1)
        self.chk_xlog = QCheckBox("Logarithmic X axis")
        self.chk_ylog = QCheckBox("Logarithmic Y axis")
        form.addRow(self.chk_xlog); form.addRow(self.chk_ylog)
        side.addLayout(form)

        self.btn_cal = QPushButton("Re-pick 3 calibration points")
        self.btn_cal.clicked.connect(self._start_calibration)
        side.addWidget(self.btn_cal)
        self.btn_manual = QPushButton("Manual point extraction")
        self.btn_manual.clicked.connect(self._start_manual)
        side.addWidget(self.btn_manual)

        row = QHBoxLayout()
        self.btn_color = QPushButton("Trace colour")
        self.btn_color.clicked.connect(self._choose_color)
        self.spin_tol = QSpinBox(); self.spin_tol.setRange(1, 160); self.spin_tol.setValue(45)
        row.addWidget(self.btn_color); row.addWidget(QLabel("Tolerance:")); row.addWidget(self.spin_tol)
        side.addLayout(row)
        self.cb_trace = QComboBox(); self.cb_trace.addItems(["Median Y per image column", "Mean Y per image column"])
        side.addWidget(self.cb_trace)
        trace = QPushButton("Auto-trace matching curve")
        trace.clicked.connect(self._auto_trace)
        side.addWidget(trace)
        clear = QPushButton("Clear extracted points")
        clear.clicked.connect(self._clear_extracted)
        side.addWidget(clear)
        side.addStretch(1)

        self.lbl_counts = QLabel("Calibration: 0/3 | manual: 0 | traced: 0")
        side.addWidget(self.lbl_counts)
        buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
        buttons.button(QDialogButtonBox.Save).setText("Create dataset")
        buttons.accepted.connect(self._accept_checked)
        buttons.rejected.connect(self.reject)
        side.addWidget(buttons)
        root.addLayout(side)

    def _browse(self):
        path, _ = QFileDialog.getOpenFileName(self, "Open raster figure", "", "Images/PDF (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.pdf)")
        if path:
            self.load_image(path)

    def load_image(self, path: str):
        if str(path).lower().endswith('.pdf'):
            try:
                try:
                    import pymupdf as fitz
                except ImportError:
                    import fitz  # type: ignore
                doc = fitz.open(path)
                if doc.page_count < 1:
                    raise ValueError("PDF has no pages.")
                page_index = 0
                if doc.page_count > 1:
                    page_no, ok = QInputDialog.getInt(self, "PDF page", "Page to digitize:", 1, 1, doc.page_count, 1)
                    if not ok:
                        doc.close(); return
                    page_index = page_no - 1
                pix = doc[page_index].get_pixmap(dpi=220, alpha=False)
                img = Image.frombytes("RGB", (pix.width, pix.height), pix.samples)
                doc.close()
            except Exception as exc:
                QMessageBox.critical(self, "PDF rasterization failed", str(exc)); return
        else:
            img = Image.open(path).convert("RGB")
        self.image_rgb = np.asarray(img)
        qimg = QImage(self.image_rgb.data, img.width, img.height, img.width * 3, QImage.Format_RGB888).copy()
        self.canvas.set_source_pixmap(QPixmap.fromImage(qimg))
        self.image_path = path
        self._start_calibration()

    def _coordinate_mode_changed(self, mode: str) -> None:
        is_cart = mode == "Cartesian"
        is_polar = mode == "Polar"
        visible = mode != "Ternary"
        self.x0.setVisible(visible); self.y0.setVisible(visible)
        self.x1.setVisible(visible); self.y1.setVisible(visible)
        for lab in (self.lbl_x0, self.lbl_y0, self.lbl_x1, self.lbl_y1):
            lab.setVisible(visible)
        self.chk_xlog.setVisible(is_cart)
        self.chk_ylog.setVisible(is_cart or is_polar)
        if is_polar:
            self.lbl_x0.setText("Centre radius value:")
            self.lbl_y0.setText("Angle offset [deg]:")
            self.lbl_x1.setText("Radial-reference value:")
            self.lbl_y1.setText("Angular-reference [deg]:")
            if self.y1.text().strip() in {"1", ""}:
                self.y1.setText("90")
            self.chk_ylog.setText("Logarithmic radius")
        elif is_cart:
            self.lbl_x0.setText("Origin X value:")
            self.lbl_y0.setText("Origin Y value:")
            self.lbl_x1.setText("X-reference value:")
            self.lbl_y1.setText("Y-reference value:")
            self.chk_ylog.setText("Logarithmic Y axis")
        self._start_calibration()

    def _start_calibration(self):
        self.pick_mode = "calibration"
        self.cal_points.clear()
        self.canvas.clear_points()
        mode = self.cb_coordinates.currentText() if hasattr(self, "cb_coordinates") else "Cartesian"
        if mode == "Polar":
            msg = "Polar calibration: click centre → radial reference → angular reference."
        elif mode == "Ternary":
            msg = "Ternary calibration: click vertex A → vertex B → vertex C."
        else:
            msg = "Calibration: click the axis origin → X-axis reference → Y-axis reference."
        self.lbl_status.setText(msg)
        self._update_counts()

    def _start_manual(self):
        if len(self.cal_points) != 3:
            QMessageBox.warning(self, "Calibration required", "Pick all three calibration points first.")
            return
        self.pick_mode = "manual"
        self.lbl_status.setText("Manual extraction: click points along the curve. The loupe follows the pointer.")

    def _canvas_click(self, x, y):
        if self.image_rgb is None:
            return
        if self.pick_mode == "calibration":
            if len(self.cal_points) < 3:
                self.cal_points.append((x, y))
                self.canvas.add_point(x, y, QColor("#E67E22"))
                if len(self.cal_points) == 3:
                    self.pick_mode = "manual"
                    self.lbl_status.setText("Calibration complete. Click curve points or use Auto-trace.")
        elif self.pick_mode == "manual":
            self.manual_points.append((x, y))
            self.canvas.add_point(x, y, QColor("#C0392B"))
        self._update_counts()

    def _calibration(self):
        if len(self.cal_points) != 3:
            raise ValueError("Three calibration points are required.")
        mode = self.cb_coordinates.currentText()
        if mode == "Polar":
            return PolarCalibration(self.cal_points[0], self.cal_points[1], self.cal_points[2],
                                    float(self.x0.text()), float(self.x1.text()), float(self.y1.text()),
                                    self.chk_ylog.isChecked())
        if mode == "Ternary":
            return TernaryCalibration(self.cal_points[0], self.cal_points[1], self.cal_points[2])
        return Calibration(self.cal_points[0], self.cal_points[1], self.cal_points[2],
                           (float(self.x0.text()), float(self.y0.text())),
                           float(self.x1.text()), float(self.y1.text()),
                           self.chk_xlog.isChecked(), self.chk_ylog.isChecked())

    def _choose_color(self):
        c = QColorDialog.getColor(self.trace_color, self, "Choose curve colour")
        if c.isValid():
            self.trace_color = c
            self.btn_color.setStyleSheet(f"background:{c.name()};color:white;")

    def _auto_trace(self):
        if self.image_rgb is None or len(self.cal_points) != 3:
            QMessageBox.warning(self, "Not ready", "Load an image and complete the 3-point calibration first.")
            return
        rgb = np.array([self.trace_color.red(), self.trace_color.green(), self.trace_color.blue()], dtype=float)
        arr = self.image_rgb.astype(float)
        dist = np.sqrt(np.sum((arr - rgb) ** 2, axis=2))
        mask = dist <= float(self.spin_tol.value())
        pts = []
        reducer = np.median if self.cb_trace.currentIndex() == 0 else np.mean
        for x in range(mask.shape[1]):
            ys = np.flatnonzero(mask[:, x])
            if ys.size:
                pts.append((float(x), float(reducer(ys))))
        if len(pts) > 2500:
            idx = np.linspace(0, len(pts) - 1, 2500).astype(int)
            pts = [pts[i] for i in idx]
        self.trace_points = pts
        for x, y in pts[::max(1, len(pts)//250 or 1)]:
            self.canvas.add_point(x, y, QColor("#16A085"))
        self.lbl_status.setText(f"Auto-traced {len(pts):,} image columns. Green markers show a decimated preview.")
        self._update_counts()

    def _update_loupe(self, x, y):
        if self.image_rgb is None:
            return
        h, w, _ = self.image_rgb.shape
        xi, yi = int(round(x)), int(round(y))
        r = 18
        x0, x1 = max(0, xi-r), min(w, xi+r+1)
        y0, y1 = max(0, yi-r), min(h, yi+r+1)
        crop = self.image_rgb[y0:y1, x0:x1]
        if crop.size == 0:
            return
        q = QImage(crop.data, crop.shape[1], crop.shape[0], crop.shape[1]*3, QImage.Format_RGB888).copy()
        pix = QPixmap.fromImage(q).scaled(self.loupe.size(), Qt.KeepAspectRatio, Qt.FastTransformation)
        painter = QPainter(pix); painter.setPen(QPen(QColor("#E74C3C"), 1))
        painter.drawLine(pix.width()//2, 0, pix.width()//2, pix.height())
        painter.drawLine(0, pix.height()//2, pix.width(), pix.height()//2); painter.end()
        self.loupe.setPixmap(pix)

    def _clear_extracted(self):
        self.manual_points.clear(); self.trace_points.clear()
        existing = list(self.cal_points)
        self.canvas.clear_points()
        for p in existing:
            self.canvas.add_point(*p, QColor("#E67E22"))
        self._update_counts()

    def _update_counts(self):
        self.lbl_counts.setText(f"Calibration: {len(self.cal_points)}/3 | manual: {len(self.manual_points)} | traced: {len(self.trace_points)}")

    def dataframe(self) -> pd.DataFrame:
        cal = self._calibration()
        raw = self.trace_points if self.trace_points else self.manual_points
        if not raw:
            return pd.DataFrame(columns=["x", "y"])
        mapped = [cal.map(x, y) for x, y in raw]
        mode = self.cb_coordinates.currentText()
        if mode == "Ternary":
            df = pd.DataFrame(mapped, columns=["A", "B", "C"]).replace([np.inf, -np.inf], np.nan).dropna()
            return df.reset_index(drop=True)
        if mode == "Polar":
            df = pd.DataFrame(mapped, columns=["theta_deg", "radius"]).replace([np.inf, -np.inf], np.nan).dropna()
            return df.sort_values("theta_deg").reset_index(drop=True)
        df = pd.DataFrame(mapped, columns=["x", "y"]).replace([np.inf, -np.inf], np.nan).dropna()
        return df.sort_values("x").reset_index(drop=True)

    def _accept_checked(self):
        try:
            df = self.dataframe()
        except Exception as exc:
            QMessageBox.warning(self, "Calibration error", str(exc)); return
        if df.empty:
            QMessageBox.warning(self, "No data", "Extract at least one manual point or auto-trace a curve first."); return
        self.accept()
