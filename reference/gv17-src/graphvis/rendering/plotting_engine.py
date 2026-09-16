# =========================================================================
# plotting_engine.py — ScientificPlotCanvas (Qt side of the plotting stack)
#
#   * builds figures in a background worker (render_core.build_figure) and
#     only touches Qt on the GUI thread; a LoadingOverlay is shown meanwhile
#   * caches finished figures in memory (per canvas, LRU) and as pickles in
#     .cache/figures so tab restoration / undo / redo never re-render
#   * GestureCanvas adds pinch-zoom, two-finger pan, one-finger pan/rotate,
#     wheel zoom and a right-click context menu on top of FigureCanvasQTAgg
#   * bottom drawer with tactical/staged dropdowns plus live numeric controls
#   * export via the ExportDialog + ExportTask worker
# =========================================================================
from __future__ import annotations

import os
import pickle
import time

import numpy as np
# NumPy 2.0 removed ``trapz``; ``trapezoid`` is its exact replacement.
_np_trapz = getattr(np, "trapezoid", getattr(np, "trapz", None))

import pandas as pd
from matplotlib.backend_bases import MouseEvent, ResizeEvent
from matplotlib.figure import Figure
from matplotlib.widgets import RectangleSelector
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from PySide6.QtCore import QEvent, QPointF, Qt, QThreadPool, Signal, QTimer
from PySide6.QtGui import QColor, QCursor, QGuiApplication, QPalette
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QDoubleSpinBox, QHBoxLayout, QInputDialog,
                               QLabel, QLineEdit, QMenu, QMessageBox, QPushButton, QSizePolicy, QSlider, QVBoxLayout, QWidget, QColorDialog, QFileDialog, QDialog,
                               QScrollArea, QSplitter, QFrame, QToolButton, QToolTip, QSpinBox, QAbstractSpinBox)

from graphvis.rendering.colormaps import COLOURMAPS, populate_colormap_combo, resolve_colourmap
from graphvis.data.loader import CACHE_DIR, CancellableTask, get_pretty_label
from graphvis.core.diagnostics import app_settings, log_line
from graphvis.core.session_diagnostics import record_activity
from graphvis.rendering.export import ExportDialog, ExportTask
from graphvis.rendering.render_core import (PlotSpec, RenderResult, build_figure, canonical_axis_scale,
                                            precompute_surfaces, clean_series_name, DEFAULT_STYLING)  # noqa: F401
from graphvis.views.widgets import (DetachablePanel, LoadingOverlay, RangeSlider, CompactSplitter,
                                    compact_tool_icon, install_typing_commit)
from graphvis.views.dialogs import AnnotationManagerDialog

FIGURE_CACHE_DIR = os.path.join(CACHE_DIR, "figures")
os.makedirs(FIGURE_CACHE_DIR, exist_ok=True)
MAX_DISK_FIGURES = 40

# One worker for figure construction + export: matplotlib is used from
# exactly one non-GUI thread at a time, and the GUI thread only ever draws.
HEAVY_POOL = QThreadPool()
HEAVY_POOL.setMaxThreadCount(1)
# Numerical surface work is independent from Matplotlib figure construction.
# Keeping it on a separate pool means a stale TPS/Kriging request cannot block
# the only Matplotlib worker after the user changes a slider or mapping.
COMPUTE_POOL = QThreadPool()
COMPUTE_POOL.setMaxThreadCount(max(1, min(2, (os.cpu_count() or 2) - 1)))

# Observed wall-clock render durations per chart/estimator family, shared by
# every canvas in the session.  Drives the automatic fast-preview decision.
_RENDER_DURATION_HISTORY: dict[str, float] = {}

# Auto-detected hardware capability (computed once; the user's explicit
# On/Off preference is read fresh on every call so toggling applies live).
_LOW_SPEC_HARDWARE: bool | None = None


# Charts whose renders are expensive enough that parameter tweaks should queue
# behind an explicit Apply in Adaptive mode (surfaces, volumes, all 3-D).
HEAVY_RENDER_CHARTS = {
    "2D Heatmap", "2D Contour", "3D Topography / Surface", "3D Mesh", "Surface + Contours",
    "Waterfall", "Ribbon", "3D Contour", "Quiver Field", "Stream Field",
    "Isosurface", "Isocaps", "Isonormals", "Volume Show", "Volume Slice", "Contour Slice",
    "Implicit Surface", "Function Surface", "Function Mesh",
}


def apply_mode() -> str:
    """Current dropdown apply policy: 'adaptive' | 'manual' | 'instant'.

    Adaptive (default) keeps standard tools fully reactive while heavy
    scientific visualizations queue behind an explicit ▶ Apply so an
    expensive render can never hang the UI mid-adjustment.
    """
    raw = str(app_settings().value("ui/dropdown_apply_mode", "Adaptive") or "Adaptive").strip().lower()
    if raw in ("manual", "instant"):
        return raw
    return "adaptive"


def manual_apply_for(chart_type: str) -> bool:
    """Whether changes targeting ``chart_type`` should wait for ▶ Apply."""
    mode = apply_mode()
    if mode == "manual":
        return True
    if mode == "instant":
        return False
    chart = str(chart_type or "")
    return chart in HEAVY_RENDER_CHARTS or chart.startswith("3D ")


def manual_apply_mode() -> bool:
    """Whether the ▶ Apply buttons should be visible (any non-instant mode)."""
    return apply_mode() != "instant"


def low_power_active() -> bool:
    """Whether Low-Power Mode is in effect (explicit setting or auto-detect).

    ``render/low_power_mode``: "Auto" (default) enables it on low-spec
    hardware (≤4 logical cores or <7.5 GB RAM); "On"/"Off" force it.
    When active, GraphVis lowers interactive DPI, point/grid budgets and
    animation frame rates, and degrades heavy 3-D scenes during camera drags.
    """
    global _LOW_SPEC_HARDWARE
    mode = str(app_settings().value("render/low_power_mode", "Auto") or "Auto").strip().lower()
    if mode == "on":
        return True
    if mode == "off":
        return False
    if _LOW_SPEC_HARDWARE is None:
        cores = os.cpu_count() or 4
        ram_gb = None
        try:
            import psutil
            ram_gb = psutil.virtual_memory().total / 1e9
        except Exception:
            pass
        _LOW_SPEC_HARDWARE = cores <= 4 or (ram_gb is not None and ram_gb < 7.5)
    return bool(_LOW_SPEC_HARDWARE)


# ---------------------------------------------------------------- tasks
class SurfacePrecomputeTask(CancellableTask):
    def __init__(self, spec: PlotSpec, seq: int, cache_key: str, use_disk: bool, dpi: int):
        super().__init__(f"surface-precompute:{spec.chart_type}")
        self.spec, self.seq, self.key = spec, seq, cache_key
        self.use_disk, self.dpi = bool(use_disk), int(dpi)

    def execute(self):
        self.stage("Preparing surface geometry/interpolation…", 8)
        self.spec.metadata = dict(self.spec.metadata or {})
        self.spec.metadata["_cancel_check"] = lambda: self._cancelled
        try:
            summary = precompute_surfaces(self.spec, for_export=False)
        finally:
            self.spec.metadata.pop("_cancel_check", None)
        if self._cancelled:
            raise Exception("Cancelled")
        self.stage("Surface ready — composing figure…", 62)
        return self.seq, self.spec, self.key, self.use_disk, self.dpi, summary


class RenderTask(CancellableTask):
    def __init__(self, spec: PlotSpec, seq: int, cache_key: str, use_disk_cache=True, dpi: int = 84):
        super().__init__(f"render:{spec.chart_type}")
        self.spec, self.seq, self.key, self.use_disk = spec, seq, cache_key, use_disk_cache
        self.dpi = max(60, min(int(dpi), 160))

    def execute(self):
        started = time.perf_counter()
        path = os.path.join(FIGURE_CACHE_DIR, f"{self.key}.pkl")
        if self.use_disk and os.path.exists(path):
            try:
                self.stage("Restoring cached figure…", 30)
                with open(path, "rb") as fh:
                    fig = pickle.load(fh)
                from matplotlib.backends.backend_agg import FigureCanvasAgg
                FigureCanvasAgg(fig)
                return self.seq, RenderResult(fig, {"chart": self.spec.chart_type, "notes": ["Restored from .cache"], "n_points": 0}), True, self.key
            except Exception as exc:
                log_line(f"Figure cache read failed ({exc}); re-rendering.", "CACHE")
        self.stage(f"Rendering {self.spec.chart_type}…", 20)
        self.spec.metadata = dict(self.spec.metadata or {})
        # Cooperative cancellation lets long local surface estimators yield the
        # single Matplotlib worker when a newer slider/graph request supersedes
        # them, instead of making the newest request wait behind stale work.
        self.spec.metadata["_cancel_check"] = lambda: self._cancelled
        try:
            res = build_figure(self.spec, dpi=self.dpi)
        finally:
            self.spec.metadata.pop("_cancel_check", None)
        elapsed = time.perf_counter() - started
        res.summary["render_seconds"] = round(float(elapsed), 4)
        if elapsed >= 1.5:
            log_line(f"Slow interactive render: {self.spec.chart_type} took {elapsed:.2f}s", "PERF")
        if self._cancelled:
            raise Exception("Cancelled")
        if self.use_disk and not res.warnings:
            try:
                with open(path, "wb") as fh:
                    pickle.dump(res.fig, fh, protocol=pickle.HIGHEST_PROTOCOL)
                _prune_disk_cache()
            except Exception as exc:
                log_line(f"Figure cache write skipped: {exc}", "CACHE")
        return self.seq, res, False, self.key


def _prune_disk_cache():
    try:
        files = [os.path.join(FIGURE_CACHE_DIR, f) for f in os.listdir(FIGURE_CACHE_DIR) if f.endswith(".pkl")]
        files.sort(key=os.path.getmtime)
        for f in files[:-MAX_DISK_FIGURES]:
            os.remove(f)
    except Exception:
        pass


def clear_figure_cache():
    for f in os.listdir(FIGURE_CACHE_DIR):
        try:
            os.remove(os.path.join(FIGURE_CACHE_DIR, f))
        except Exception:
            pass


# ------------------------------------------------------- axis view helpers
def zoom_axes(ax, factor: float, display_xy=None):
    """Zoom `ax` by `factor` (>1 zooms in) about a display point (or centre).
    Works in display space so log/linear scales behave identically."""
    if hasattr(ax, "get_zlim3d"):
        for get, set_ in ((ax.get_xlim3d, ax.set_xlim3d), (ax.get_ylim3d, ax.set_ylim3d), (ax.get_zlim3d, ax.set_zlim3d)):
            lo, hi = get()
            c = 0.5 * (lo + hi)
            set_(c + (lo - c) / factor, c + (hi - c) / factor)
        return
    bbox = ax.bbox
    if display_xy is None:
        display_xy = (bbox.x0 + bbox.width / 2, bbox.y0 + bbox.height / 2)
    cx, cy = display_xy
    x0, y0, x1, y1 = bbox.x0, bbox.y0, bbox.x1, bbox.y1
    nx0, nx1 = cx + (x0 - cx) / factor, cx + (x1 - cx) / factor
    ny0, ny1 = cy + (y0 - cy) / factor, cy + (y1 - cy) / factor
    inv = ax.transData.inverted()
    (dx0, dy0), (dx1, dy1) = inv.transform([(nx0, ny0), (nx1, ny1)])
    if np.all(np.isfinite([dx0, dx1, dy0, dy1])):
        ax.set_xlim(dx0, dx1)
        ax.set_ylim(dy0, dy1)


def pan_axes(ax, dx_px: float, dy_px: float):
    """Shift the view of `ax` by a pixel delta (display space, y up)."""
    if hasattr(ax, "get_zlim3d"):
        return
    bbox = ax.bbox
    inv = ax.transData.inverted()
    (x0, y0), (x1, y1) = inv.transform([(bbox.x0 - dx_px, bbox.y0 - dy_px), (bbox.x1 - dx_px, bbox.y1 - dy_px)])
    if np.all(np.isfinite([x0, x1, y0, y1])):
        ax.set_xlim(x0, x1)
        ax.set_ylim(y0, y1)


# ---------------------------------------------------------- gesture canvas
class GestureCanvas(FigureCanvasQTAgg):
    """FigureCanvasQTAgg with touch/tablet navigation.

    one finger / left mouse drag : pan (2-D) or rotate (3-D, via mplot3d)
    two fingers                  : pinch-zoom, un-pinch, two-finger pan
    wheel                        : zoom about the cursor
    right click / long press     : context menu (handled by the owner)
    """

    view_changed = Signal()

    def __init__(self, fig, gestures=True):
        self._gestures = False          # must exist before QWidget.__init__ dispatches events
        super().__init__(fig)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setFocusPolicy(Qt.ClickFocus)
        # Preserve the already-painted canvas while a dock/splitter moves; Qt
        # only needs to repaint newly exposed pixels until the final Matplotlib
        # resize is committed.  This trims the last bit of resize-time paint
        # churn on Windows.
        self.setAttribute(Qt.WA_StaticContents, True)
        self._gestures = gestures
        if gestures:
            self.setAttribute(Qt.WA_AcceptTouchEvents, True)
            self.grabGesture(Qt.PinchGesture)
            self.grabGesture(Qt.PanGesture)
        self._touch_prev: dict[int, QPointF] = {}
        self._touch_mouse_active = False
        self._drag_last = None
        self._drag_ax = None
        self._view_bounds = None
        # 3-D camera-drag management: matplotlib's Axes3D issues one full
        # uncoalesced draw per mouse-move; these fields let GestureCanvas
        # throttle those draws and temporarily hide very heavy artists.
        self._rotate3d_active = False
        self._hidden_rotation_artists: list = []
        self._last_full_draw_seconds = 0.0
        # Matplotlib redraws are expensive on large/high-DPI figures.  Coalesce
        # mouse/touch motion into at most ~60 repaint requests per second
        # (~30 in Low-Power Mode).
        self._draw_interval_ms = 33 if low_power_active() else 16
        self._draw_timer = QTimer(self)
        self._draw_timer.setSingleShot(True)
        self._draw_timer.timeout.connect(lambda: FigureCanvasQTAgg.draw_idle(self))
        # Qt can emit dozens of resize events per second while a splitter is
        # dragged.  Matplotlib's default Qt canvas schedules a redraw for every
        # one; defer the expensive resize notification/draw until movement has
        # settled while still resizing the QWidget itself immediately.
        self._resize_finalize_timer = QTimer(self)
        self._resize_finalize_timer.setSingleShot(True)
        self._resize_finalize_timer.timeout.connect(self._finish_deferred_resize)
        self.set_canvas_background(fig.get_facecolor())
        self.mpl_connect("scroll_event", self._on_scroll)
        self.mpl_connect("button_press_event", self._on_press)
        self.mpl_connect("motion_notify_event", self._on_motion)
        self.mpl_connect("button_release_event", self._on_release)

    def _schedule_draw(self) -> None:
        if not self._draw_timer.isActive():
            self._draw_timer.start(self._draw_interval_ms)

    def draw_idle(self, *args, **kwargs):
        # During an active 3-D camera drag, Axes3D calls draw_idle on every
        # mouse-move.  Route those through the frame-rate coalescer so the
        # event loop stays responsive; everything else draws normally.
        if self._rotate3d_active:
            self._schedule_draw()
            return
        super().draw_idle(*args, **kwargs)

    def paintEvent(self, event):
        t0 = time.perf_counter()
        super().paintEvent(event)
        if not self._rotate3d_active:
            # Full-quality draw cost drives the rotation-degradation decision.
            self._last_full_draw_seconds = time.perf_counter() - t0

    @staticmethod
    def _artist_element_count(artist) -> int:
        try:
            if hasattr(artist, "_segments3d"):
                return len(artist._segments3d)
            if hasattr(artist, "get_paths"):
                return len(artist.get_paths())
            if hasattr(artist, "get_offsets"):
                return len(artist.get_offsets())
            if hasattr(artist, "get_xdata"):
                return len(artist.get_xdata())
        except Exception:
            pass
        return 0

    def _begin_3d_rotation(self, ax) -> None:
        """Enter throttled-rotation mode; hide very heavy artists if the last
        full-quality draw was slow, so the camera follows the mouse smoothly."""
        if self._rotate3d_active:
            return
        self._rotate3d_active = True
        low_power = low_power_active()
        if self._last_full_draw_seconds < 0.05 and not low_power:
            return   # scene already redraws fast enough at full detail
        threshold = 400 if low_power else 1500
        hidden = []
        try:
            for artist in list(getattr(ax, "collections", ())) + list(getattr(ax, "lines", ())):
                if self._artist_element_count(artist) > threshold and artist.get_visible():
                    artist.set_visible(False)
                    hidden.append(artist)
        except Exception:
            pass
        self._hidden_rotation_artists = hidden

    def _end_3d_rotation(self) -> None:
        if not self._rotate3d_active:
            return
        self._rotate3d_active = False
        restored = bool(self._hidden_rotation_artists)
        for artist in self._hidden_rotation_artists:
            try:
                artist.set_visible(True)
            except Exception:
                pass
        self._hidden_rotation_artists = []
        del restored
        FigureCanvasQTAgg.draw_idle(self)   # one full-quality redraw
        # Mouse-driven 3-D rotation previously never announced its new camera
        # state; emitting here keeps linked views and pick caches in sync.
        self.view_changed.emit()

    def resizeEvent(self, event) -> None:
        try:
            # Bypass FigureCanvasQT.resizeEvent because it queues a complete
            # Matplotlib redraw on every intermediate splitter position.  The
            # QWidget resizes immediately, but the expensive Figure geometry
            # update is deferred until the user has stopped dragging.
            QWidget.resizeEvent(self, event)
            self._resize_finalize_timer.start(130)
        except Exception:
            FigureCanvasQTAgg.resizeEvent(self, event)

    def _finish_deferred_resize(self) -> None:
        try:
            size = self.size()
            if size.width() > 0 and size.height() > 0 and self.figure is not None:
                ratio_value = getattr(self, "device_pixel_ratio", 1.0)
                if callable(ratio_value):
                    ratio_value = ratio_value()
                ratio = float(ratio_value or 1.0)
                dpi = float(self.figure.dpi or 100.0)
                self.figure.set_size_inches(size.width() * ratio / dpi, size.height() * ratio / dpi, forward=False)
            ResizeEvent("resize_event", self)._process()
        except Exception:
            pass
        FigureCanvasQTAgg.draw_idle(self)

    def set_canvas_background(self, colour) -> None:
        """Fill transient resize/expose regions with the graph background.

        FigureCanvasQTAgg otherwise exposes the platform's default white while
        a dock/splitter moves faster than Matplotlib can repaint, producing the
        distracting white flash reported on dark/themed workspaces.
        """
        try:
            if isinstance(colour, (tuple, list)) and len(colour) >= 3:
                values = [int(round(float(v) * 255.0)) if float(v) <= 1.0 else int(round(float(v))) for v in colour[:3]]
                qcolour = QColor(*values)
            else:
                qcolour = QColor(str(colour))
            if not qcolour.isValid():
                qcolour = QColor("#FFFFFF")
            palette = self.palette()
            palette.setColor(QPalette.Window, qcolour)
            palette.setColor(QPalette.Base, qcolour)
            self.setPalette(palette)
            self.setAutoFillBackground(True)
        except Exception:
            pass

    # -- 2-D drag-pan with the mouse or a single finger ------------------
    def set_view_bounds(self, state: dict | None):
        """Set the data cage used to clamp pan/zoom operations."""
        self._view_bounds = dict(state or {}) if state else None

    @staticmethod
    def _clamp_pair(cur, home):
        lo, hi = sorted(map(float, cur)); hlo, hhi = sorted(map(float, home))
        span, hspan = hi - lo, hhi - hlo
        if hspan <= 0:
            return cur
        if span >= hspan:
            return (hlo, hhi)
        if lo < hlo:
            hi += hlo - lo; lo = hlo
        if hi > hhi:
            lo -= hi - hhi; hi = hhi
        return (lo, hi)

    def _clamp_view(self, ax):
        # Free panning by default: users can drag the scene anywhere across
        # the viewport plane. Enable ui/clamp_pan to restore the data cage.
        if not app_settings().value("ui/clamp_pan", False, bool):
            return
        b = self._view_bounds
        if not b or ax is not self.figure.axes[0]:
            return
        try:
            ax.set_xlim(*self._clamp_pair(ax.get_xlim(), b["xlim"]))
            ax.set_ylim(*self._clamp_pair(ax.get_ylim(), b["ylim"]))
            if hasattr(ax, "get_zlim3d") and b.get("zlim"):
                ax.set_zlim3d(*self._clamp_pair(ax.get_zlim3d(), b["zlim"]))
        except Exception:
            pass

    def _legend_hit(self, event):
        for ax in self.figure.axes:
            leg = ax.get_legend()
            if leg is not None and leg.get_visible():
                try:
                    if leg.contains(event)[0]:
                        return True
                except Exception:
                    pass
        return False

    def _on_press(self, event):
        if event.inaxes is not None and hasattr(event.inaxes, "get_zlim3d"):
            # Matplotlib's Axes3D owns the actual rotation/pan; GraphVis only
            # manages redraw throttling + optional detail degradation.  Right
            # clicks open the context menu, which can swallow the release
            # event, so only rotate (1) and pan (2) buttons enter drag mode.
            if event.button in (1, 2) and not self._legend_hit(event):
                self._begin_3d_rotation(event.inaxes)
            return
        if event.button != 1 or event.inaxes is None:
            return
        if self._legend_hit(event):
            return
        self._drag_ax = event.inaxes
        self._drag_last = (event.x, event.y)

    def _on_motion(self, event):
        if self._drag_ax is None or self._drag_last is None or event.x is None:
            return
        dx, dy = event.x - self._drag_last[0], event.y - self._drag_last[1]
        self._drag_last = (event.x, event.y)
        pan_axes(self._drag_ax, dx, dy)
        self._clamp_view(self._drag_ax)
        self._schedule_draw()

    def _on_release(self, event):
        self._end_3d_rotation()
        if self._drag_ax is not None:
            self._drag_ax = None
            self._drag_last = None
            self.view_changed.emit()

    def _on_scroll(self, event):
        ax = event.inaxes or (self.figure.axes[0] if self.figure.axes else None)
        if ax is None:
            return
        factor = 1.18 ** (event.step if event.step else 0)
        zoom_axes(ax, factor, (event.x, event.y) if event.inaxes else None)
        self._clamp_view(ax)
        self._schedule_draw()
        self.view_changed.emit()

    # -- Qt event routing ---------------------------------------------------
    def event(self, e):
        t = e.type()
        if self._gestures:
            if t == QEvent.Gesture:
                return self._gesture_event(e)
            if t in (QEvent.TouchBegin, QEvent.TouchUpdate, QEvent.TouchEnd, QEvent.TouchCancel):
                return self._touch_event(e)
        return super().event(e)

    def _target_axes(self, widget_pos: QPointF):
        x, y = self.mouseEventCoords(widget_pos)
        for ax in self.figure.axes:
            if ax.bbox.contains(x, y):
                return ax, (x, y)
        return (self.figure.axes[0] if self.figure.axes else None), (x, y)

    def _gesture_event(self, e):
        handled = False
        pinch = e.gesture(Qt.PinchGesture)
        if pinch is not None:
            handled = True
            flags = pinch.changeFlags()
            pos = self.mapFromGlobal(pinch.hotSpot().toPoint()) if not pinch.hotSpot().isNull() else self.rect().center()
            ax, disp = self._target_axes(QPointF(pos))
            if ax is not None and (flags & pinch.ChangeFlag.ScaleFactorChanged):
                s = pinch.scaleFactor()
                if s > 0 and abs(s - 1.0) > 1e-4:
                    zoom_axes(ax, s, disp)
            if ax is not None and hasattr(ax, "view_init") and (flags & pinch.ChangeFlag.RotationAngleChanged):
                ax.view_init(elev=ax.elev, azim=ax.azim - pinch.rotationAngle())
            if ax is not None:
                self._clamp_view(ax)
            if pinch.state() in (Qt.GestureFinished, Qt.GestureCanceled):
                self.view_changed.emit()
            self._schedule_draw()
        pan = e.gesture(Qt.PanGesture)
        if pan is not None:
            handled = True
            d = pan.delta()
            pos = self.mapFromGlobal(pan.hotSpot().toPoint()) if not pan.hotSpot().isNull() else self.rect().center()
            ax, _ = self._target_axes(QPointF(pos))
            if ax is not None:
                ratio = self.devicePixelRatioF()
                if hasattr(ax, "view_init"):
                    ax.view_init(elev=ax.elev - d.y() * 0.4, azim=ax.azim - d.x() * 0.4)
                else:
                    pan_axes(ax, d.x() * ratio, -d.y() * ratio)
                self._clamp_view(ax)
            if pan.state() in (Qt.GestureFinished, Qt.GestureCanceled):
                self.view_changed.emit()
            self._schedule_draw()
        if handled:
            e.accept()
        return handled

    def _emit_mpl_mouse(self, name, pos: QPointF, button=1):
        x, y = self.mouseEventCoords(pos)
        ev = MouseEvent(name, self, x, y, button=button, guiEvent=None)
        self.callbacks.process(name, ev)

    def _touch_event(self, e):
        pts = e.points()
        t = e.type()
        if t == QEvent.TouchBegin:
            self._touch_prev = {p.id(): p.position() for p in pts}
            if len(pts) == 1:
                self._touch_mouse_active = True
                self._emit_mpl_mouse("button_press_event", pts[0].position())
            e.accept()
            return True
        if t == QEvent.TouchUpdate:
            if len(pts) >= 2:
                if self._touch_mouse_active:          # second finger landed → stop the 1-finger drag
                    self._emit_mpl_mouse("button_release_event", pts[0].position())
                    self._touch_mouse_active = False
                p0, p1 = pts[0], pts[1]
                prev0 = self._touch_prev.get(p0.id(), p0.position())
                prev1 = self._touch_prev.get(p1.id(), p1.position())
                d_prev = ((prev0.x() - prev1.x()) ** 2 + (prev0.y() - prev1.y()) ** 2) ** 0.5
                d_now = ((p0.position().x() - p1.position().x()) ** 2 + (p0.position().y() - p1.position().y()) ** 2) ** 0.5
                centre_prev = QPointF((prev0.x() + prev1.x()) / 2, (prev0.y() + prev1.y()) / 2)
                centre_now = QPointF((p0.position().x() + p1.position().x()) / 2, (p0.position().y() + p1.position().y()) / 2)
                ax, disp = self._target_axes(centre_now)
                if ax is not None:
                    ratio = self.devicePixelRatioF()
                    if d_prev > 1 and abs(d_now / d_prev - 1) > 1e-3:
                        zoom_axes(ax, d_now / d_prev, disp)
                    dx, dy = centre_now.x() - centre_prev.x(), centre_now.y() - centre_prev.y()
                    if hasattr(ax, "view_init"):
                        ax.view_init(elev=ax.elev - dy * 0.4, azim=ax.azim - dx * 0.4)
                    else:
                        pan_axes(ax, dx * ratio, -dy * ratio)
                    self._clamp_view(ax)
                    self._schedule_draw()
            elif len(pts) == 1 and self._touch_mouse_active:
                self._emit_mpl_mouse("motion_notify_event", pts[0].position())
            self._touch_prev = {p.id(): p.position() for p in pts}
            e.accept()
            return True
        if t in (QEvent.TouchEnd, QEvent.TouchCancel):
            if self._touch_mouse_active and pts:
                self._emit_mpl_mouse("button_release_event", pts[0].position())
            self._touch_mouse_active = False
            self._touch_prev = {}
            self.view_changed.emit()
            e.accept()
            return True
        return False


# --------------------------------------------------------- cross sections
class CrossSectionDialog(QDialog):
    """Live orthogonal 1-D slices through a rendered 2-D surface."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("GraphVis Surface Cross-Sections")
        self.resize(900, 430)
        layout = QVBoxLayout(self)
        self.info = QLabel("Click a 2-D heatmap while cross-section mode is active.")
        self.info.setWordWrap(True); layout.addWidget(self.info)
        self.figure = Figure(figsize=(9, 4), dpi=100, facecolor="white")
        self.canvas = FigureCanvasQTAgg(self.figure)
        layout.addWidget(self.canvas, 1)
        self.ax_h = self.figure.add_subplot(121)
        self.ax_v = self.figure.add_subplot(122)
        self.line_h, = self.ax_h.plot([], [], linewidth=1.8)
        self.line_v, = self.ax_v.plot([], [], linewidth=1.8)
        self._last_payload = None

    @staticmethod
    def _nearest(axis: np.ndarray, value: float, log: bool) -> int:
        a = np.asarray(axis, dtype=float)
        v = float(value)
        if log:
            good = a > 0
            if not good.any() or v <= 0:
                return int(np.nanargmin(np.abs(a - v)))
            metric = np.full_like(a, np.nan, dtype=float); metric[good] = np.log10(a[good])
            return int(np.nanargmin(np.abs(metric - np.log10(v))))
        return int(np.nanargmin(np.abs(a - v)))

    def update_slice(self, payload: dict, x0: float, y0: float) -> None:
        X = np.asarray(payload["X"], dtype=float)
        Y = np.asarray(payload["Y"], dtype=float)
        Z = np.ma.asarray(payload["Z"], dtype=float)
        x = X[0, :]; y = Y[:, 0]
        ix = self._nearest(x, x0, str(payload.get("x_scale", "Linear")) == "Log10")
        iy = self._nearest(y, y0, str(payload.get("y_scale", "Linear")) == "Log10")
        zh = np.ma.filled(Z[iy, :], np.nan)
        zv = np.ma.filled(Z[:, ix], np.nan)
        self.line_h.set_data(x, zh); self.line_v.set_data(y, zv)
        self.ax_h.relim(); self.ax_h.autoscale_view(); self.ax_v.relim(); self.ax_v.autoscale_view()
        self.ax_h.set_xscale("log" if str(payload.get("x_scale", "Linear")) == "Log10" else "linear")
        self.ax_v.set_xscale("log" if str(payload.get("y_scale", "Linear")) == "Log10" else "linear")
        zlabel = get_pretty_label(payload.get("z_label")) or "Response"
        xlab = get_pretty_label(payload.get("x_label")) or "X"
        ylab = get_pretty_label(payload.get("y_label")) or "Y"
        self.ax_h.set_xlabel(xlab); self.ax_h.set_ylabel(zlabel)
        self.ax_v.set_xlabel(ylab); self.ax_v.set_ylabel(zlabel)
        self.ax_h.set_title(f"{zlabel} vs {xlab}\n{ylab} = {y[iy]:.6g}")
        self.ax_v.set_title(f"{zlabel} vs {ylab}\n{xlab} = {x[ix]:.6g}")
        for ax in (self.ax_h, self.ax_v):
            ax.grid(True, alpha=0.22)
        self.info.setText(f"{payload.get('dataset', 'Surface')} • {payload.get('method', '')} • selected X={x[ix]:.8g}, Y={y[iy]:.8g}")
        self.figure.tight_layout()
        self.canvas.draw_idle()
        self._last_payload = payload


# ------------------------------------------------------------- the canvas
class ScientificPlotCanvas(QWidget):
    preview_requested = Signal()
    auto_render_requested = Signal()
    status_message = Signal(str)
    spec_changed = Signal(object)
    render_finished = Signal(object)       # RenderResult
    render_reverted = Signal()             # cancelled/failed → UI must roll back
    analysis_summary = Signal(str)
    view_state_changed = Signal(object)

    MEMORY_CACHE = 8
    # Predicted-duration threshold (seconds) above which a coarse fast preview
    # is rendered first while the high-resolution render queues behind it.
    FAST_PREVIEW_THRESHOLD = 10.0

    def __init__(self, spec: PlotSpec, parent=None, title: str = "", autorender: bool = True):
        super().__init__(parent)
        self.spec = spec
        self.title = title
        self.current_plot_widget: GestureCanvas | None = None
        self._seq = 0
        self._active_task: RenderTask | None = None
        self._active_compute_task: SurfacePrecomputeTask | None = None
        self._fig_cache: dict[str, RenderResult] = {}
        self._fig_cache_order: list[str] = []
        self._annotation_artists: list[tuple[object, int]] = []
        self._inspector_artist = None
        self._point_pick_cache: list[dict] = []
        self._point_display_cache: dict[tuple, np.ndarray] = {}
        self._live_marker_artists: list[object] = []
        self._drag_annotation_index: int | None = None
        self._drag_annotation_start: tuple[float, float] | None = None
        self._drag_annotation_origin: dict | None = None
        self._inline_editor: QLineEdit | None = None
        self._roi_selector: RectangleSelector | None = None
        self._home_view = None
        self.last_result: RenderResult | None = None
        self._rendered_spec_snapshot: PlotSpec | None = None
        self._applying_external_view = False
        self._animation_payload = None
        self._animation_index = 0
        self._animation_timer = QTimer(self)
        self._animation_timer.timeout.connect(self._animation_step)
        self._slice_mode = False
        self._slice_dialog: CrossSectionDialog | None = None
        # ---- instant auto-apply / fast-preview machinery -----------------
        self._preview_active = False
        self._queued_full_request: tuple | None = None
        self._current_request: tuple | None = None
        self._render_started_at = 0.0
        self._local_combo_debounce = QTimer(self)
        self._local_combo_debounce.setSingleShot(True)
        self._local_combo_debounce.setInterval(350)
        self._local_combo_debounce.timeout.connect(self._apply_all_local_combos)
        self._preview_fallback_timer = QTimer(self)
        self._preview_fallback_timer.setSingleShot(True)
        self._preview_fallback_timer.setInterval(int(self.FAST_PREVIEW_THRESHOLD * 1000))
        self._preview_fallback_timer.timeout.connect(self._fallback_to_preview)
        self._build_ui()
        self.overlay = LoadingOverlay(self.plot_host, mode="corner")
        if low_power_active():
            # ~11 fps mascot animation instead of ~33: cheaper background repaint.
            self.overlay._timer.setInterval(90)
        self.overlay.cancel_requested.connect(self.cancel_active_render)
        self._sync_range_slider_domains()
        self._blank_placeholder = None
        self.apply_workspace_background(dict((self.spec.metadata or {}).get("ui_theme") or {}), redraw=False)
        if autorender:
            self.request_render()
        else:
            self.show_blank_workspace()

    def show_blank_workspace(self, message: str = "Blank workspace — select a dataset or read literature; graphs render automatically as you adjust the controls."):
        """Show a lightweight placeholder without constructing a matplotlib figure."""
        if self.current_plot_widget is not None:
            self.plot_container.removeWidget(self.current_plot_widget)
            self.current_plot_widget.setParent(None)
            self.current_plot_widget.deleteLater()
            self.current_plot_widget = None
        if self._blank_placeholder is None:
            box = QWidget(self.plot_host)
            lay = QVBoxLayout(box); lay.setContentsMargins(28, 28, 28, 28)
            lay.addStretch(1)
            label = QLabel(message)
            label.setObjectName("BlankWorkspaceMessage")
            label.setAlignment(Qt.AlignCenter); label.setWordWrap(True)
            label.setMinimumHeight(100)
            lay.addWidget(label)
            lay.addStretch(1)
            self._blank_placeholder = box
        else:
            label = self._blank_placeholder.findChild(QLabel, "BlankWorkspaceMessage")
            if label is not None:
                label.setText(message)
        if self.plot_container.indexOf(self._blank_placeholder) < 0:
            self.plot_container.addWidget(self._blank_placeholder, 1)
        self._apply_background_palette(self._blank_placeholder, self._workspace_figure_colour())
        self._blank_placeholder.show()

    @staticmethod
    def _apply_background_palette(widget: QWidget | None, colour: str) -> None:
        if widget is None:
            return
        try:
            qcolour = QColor(str(colour or "#FFFFFF"))
            if not qcolour.isValid():
                qcolour = QColor("#FFFFFF")
            palette = widget.palette()
            palette.setColor(QPalette.Window, qcolour)
            palette.setColor(QPalette.Base, qcolour)
            widget.setPalette(palette)
            widget.setAutoFillBackground(True)
        except Exception:
            pass

    def _workspace_figure_colour(self) -> str:
        theme = dict((self.spec.metadata or {}).get("ui_theme") or {})
        return str(theme.get("figure") or "#FFFFFF")

    def apply_workspace_background(self, theme: dict | None, *, redraw: bool = True) -> None:
        """Apply a UI-only graph background without rebuilding plot data.

        This is intentionally cheaper than ``request_render`` so changing a
        canvas colour or application theme does not trigger a full scientific
        render.  Explicit user styling colours are preserved; GraphVis default
        label/tick colours are replaced with contrast-aware theme colours.
        """
        theme = dict(theme or {})
        self.spec.metadata = dict(self.spec.metadata or {})
        self.spec.metadata["ui_theme"] = theme
        figure_bg = str(theme.get("figure") or "#FFFFFF")
        axes_bg = str(theme.get("axes") or figure_bg)
        text = str(theme.get("text") or "#243447")
        muted = str(theme.get("muted") or text)
        border = str(theme.get("border") or muted)
        grid = str(theme.get("grid") or border)
        # Keep graph colours scoped to the actual plot viewport.  Applying the
        # figure palette to ``self`` leaks it into the detachable graph-control
        # strips through Qt palette inheritance (white/light graph backgrounds
        # therefore made dark-theme toolbar text unreadable).
        try:
            app = QApplication.instance()
            if app is not None:
                self.setPalette(app.palette())
            self.setAutoFillBackground(False)
        except Exception:
            pass
        self._apply_background_palette(getattr(self, "plot_host", None), figure_bg)
        self._apply_background_palette(getattr(self, "plot_area", None), figure_bg)
        self._apply_background_palette(getattr(self, "_blank_placeholder", None), figure_bg)

        canvas = self.current_plot_widget
        if canvas is None or canvas.figure is None:
            return
        canvas.set_canvas_background(figure_bg)
        fig = canvas.figure
        try:
            fig.set_facecolor(figure_bg)
        except Exception:
            pass

        styling = self.spec.styling or {}
        def themed_default(element: str, fallback: str) -> str | None:
            current = str((styling.get(element) or {}).get("color") or fallback)
            default = str((DEFAULT_STYLING.get(element) or {}).get("color") or fallback)
            if current.lower() != default.lower():
                return None
            return muted if element in ("Tick Labels", "Legend") else text

        tick_colour = themed_default("Tick Labels", "#2C3E50")
        title_colour = themed_default("Main Title", "#1F2D3D")
        xlabel_colour = themed_default("X-Axis Label", "#34495E")
        ylabel_colour = themed_default("Y-Axis Label", "#34495E")
        zlabel_colour = themed_default("Z-Axis Label", "#34495E")
        legend_colour = themed_default("Legend", "#2C3E50")
        colourbar_colour = themed_default("Colourbar Title", "#34495E")
        for ax in fig.axes:
            try:
                ax.set_facecolor(axes_bg)
                for spine in ax.spines.values():
                    spine.set_color(border)
                if tick_colour:
                    ax.tick_params(axis="both", colors=tick_colour)
                    if hasattr(ax, "zaxis"):
                        ax.tick_params(axis="z", colors=tick_colour)
                if title_colour and getattr(ax, "title", None) is not None:
                    ax.title.set_color(title_colour)
                if xlabel_colour and getattr(ax, "xaxis", None) is not None:
                    ax.xaxis.label.set_color(xlabel_colour)
                if ylabel_colour and getattr(ax, "yaxis", None) is not None:
                    ax.yaxis.label.set_color(ylabel_colour)
                if zlabel_colour and getattr(ax, "zaxis", None) is not None:
                    ax.zaxis.label.set_color(zlabel_colour)
                if hasattr(ax, "xaxis") and hasattr(ax.xaxis, "pane"):
                    for axis in (ax.xaxis, ax.yaxis, getattr(ax, "zaxis", None)):
                        if axis is not None and hasattr(axis, "pane"):
                            axis.pane.set_facecolor(axes_bg)
                            axis.pane.set_edgecolor(border)
                for line in ax.get_xgridlines() + ax.get_ygridlines():
                    line.set_color(grid)
                leg = ax.get_legend()
                if leg is not None:
                    leg.get_frame().set_facecolor(axes_bg)
                    leg.get_frame().set_edgecolor(border)
                    if legend_colour:
                        for label in leg.get_texts():
                            label.set_color(legend_colour)
                # Colorbar axes do not expose a stable public marker across all
                # Matplotlib versions, but their axis label can be updated
                # safely and this also helps secondary/twin axes.
                if colourbar_colour and ax.get_ylabel():
                    if ax not in fig.axes[:1] or not ylabel_colour:
                        ax.yaxis.label.set_color(colourbar_colour)
            except Exception:
                continue
        if redraw:
            canvas.draw_idle()

    def clear_for_dataset_change(self) -> None:
        """Drop every data-specific visual when the selected dataset changes."""
        self._seq += 1
        if self._active_task is not None:
            self._active_task.cancel()
            self._active_task = None
        if self._active_compute_task is not None:
            self._active_compute_task.cancel()
            self._active_compute_task = None
        self._preview_fallback_timer.stop()
        self._queued_full_request = None
        self._set_preview_border(False)
        self.overlay.stop()
        self.last_result = None
        self._rendered_spec_snapshot = None
        self._point_pick_cache.clear()
        self._point_display_cache.clear()
        self._live_marker_artists.clear()
        self._inspector_artist = None
        self.spec.view_state = None
        self.spec.experimental_overlay = None
        self.spec.literature_overlays = []
        self.spec.metadata = dict(self.spec.metadata or {})
        self.spec.metadata.pop("data_markers", None)
        self.lbl_inspector.setText("X —   Y —   Z —")
        self.show_blank_workspace("Dataset changed — choose a graph; it renders automatically.")

    def cancel_active_render(self):
        cancelled = False
        if self._active_compute_task is not None:
            self._active_compute_task.cancel(); self._active_compute_task = None; cancelled = True
        if self._active_task is not None:
            self._active_task.cancel(); self._active_task = None; cancelled = True
        self._preview_fallback_timer.stop()
        self._queued_full_request = None
        if cancelled:
            self.overlay.mark_cancelling()
            self.status_message.emit("Cancelling render — reverting controls to the last applied state…")
            # Roll pending inputs back so the UI matches what is displayed.
            self._revert_to_rendered_state()

    # ---------------------------------------------------------------- UI
    def _build_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ------------------------------------------------------ top controls
        # A compact studio strip: only a 24 px collapsible header plus one
        # 32 px action row.  Secondary actions are icon-only with tooltips so
        # they do not steal vertical or horizontal space from the graph.
        self.top_controls = DetachablePanel("Graph controls", expanded=True)
        self.top_controls.title_label.hide()
        self.top_controls.header.setToolTip("Graph controls — collapse, drag the splitter, or pop out")
        self.top_controls.header.setFixedHeight(22); self.top_controls.toggle.setFixedSize(18, 18); self.top_controls.detach_button.setFixedSize(18, 18)
        self.top_controls.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        top_scroll = QScrollArea()
        top_scroll.setObjectName("GraphControlStrip")
        top_scroll.setWidgetResizable(True)
        top_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        top_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        top_scroll.setFrameShape(QFrame.NoFrame)
        top_body = QWidget()
        top_body.setObjectName("GraphControlStripBody")
        top = QHBoxLayout(top_body)
        top.setContentsMargins(3, 2, 3, 2)
        top.setSpacing(3)

        # GraphVis 16: rendering is fully automatic (debounced auto-apply), so
        # the manual Preview button is retired.  The widget is kept, hidden,
        # for API/back-compat with saved layouts and static smoke tests.
        self.btn_generate = QPushButton("Preview")
        self.btn_generate.setObjectName("PrimaryGraphAction")
        self.btn_generate.setIcon(compact_tool_icon("preview"))
        self.btn_generate.setToolTip("Generate / refresh the active graph")
        self.btn_generate.setFixedHeight(28)
        self.btn_generate.clicked.connect(self.preview_requested.emit)
        self.btn_generate.setVisible(False)
        top.addWidget(self.btn_generate)

        self.chk_grid = QToolButton()
        self.chk_grid.setObjectName("CompactGraphTool")
        self.chk_grid.setIcon(compact_tool_icon("grid")); self.chk_grid.setCheckable(True)
        self.chk_grid.setChecked(self.spec.grid_visible); self.chk_grid.setToolTip("Toggle grid")
        self.chk_grid.setFixedSize(28, 28); self.chk_grid.toggled.connect(self._trigger_change)
        top.addWidget(self.chk_grid)

        self.lbl_inspector = QLabel("X —   Y —   Z —")
        self.lbl_inspector.setMinimumWidth(210); self.lbl_inspector.setMaximumWidth(430)
        self.lbl_inspector.setObjectName("CompactCoordinateInspector")
        self.lbl_inspector.setStyleSheet("font-family:Consolas,monospace;padding:1px 4px;")
        self.lbl_inspector.setToolTip("Click the graph to inspect exact axis coordinates; 3-D inspection snaps to the nearest displayed data point.")
        top.addWidget(self.lbl_inspector)

        def _tool(icon_name, tip, slot):
            b = QToolButton(); b.setObjectName("CompactGraphTool"); b.setIcon(compact_tool_icon(icon_name))
            b.setToolTip(tip); b.setFixedSize(28, 28); b.clicked.connect(slot); top.addWidget(b); return b

        self.btn_annotate_quick = _tool("text", "Add text / annotation", self._add_annotation_prompt)
        self.btn_gradient_quick = _tool("fit", "Find gradient in a selected region", self._focus_gradient_region)
        self.btn_gradient_report_quick = _tool("document", "Gradient report", self._compute_and_display_gradient)
        self.btn_clear_notes_quick = _tool("reset", "Clear graph notes / annotations", self._clear_annotations)
        self.btn_fit_area_quick = _tool("roi", "Fit view to plotted area", self._reset_view)
        self.btn_cross_section = QToolButton(); self.btn_cross_section.setObjectName("CompactGraphTool")
        self.btn_cross_section.setText("⊥"); self.btn_cross_section.setCheckable(True); self.btn_cross_section.setFixedSize(28, 28)
        self.btn_cross_section.setToolTip("Cross-section mode — click a 2-D heatmap to open/update orthogonal response slices at that exact X/Y location")
        self.btn_cross_section.toggled.connect(self._toggle_cross_section_mode); top.addWidget(self.btn_cross_section)
        self.btn_perf_quick = _tool("document", "Performance inspector — show computation/render timing for the current figure", self._show_performance_inspector)

        self.btn_play = QToolButton()
        self.btn_play.setObjectName("CompactGraphTool"); self.btn_play.setIcon(compact_tool_icon("play"))
        self.btn_play.setToolTip("Play / pause animation"); self.btn_play.clicked.connect(self._toggle_animation)
        self.btn_play.setVisible(False); self.btn_play.setFixedSize(28, 28); top.addWidget(self.btn_play)
        self.spin_animation_speed = QDoubleSpinBox()
        self.spin_animation_speed.setRange(0.1, 100.0); self.spin_animation_speed.setSingleStep(0.1); self.spin_animation_speed.setSuffix("×")
        self.spin_animation_speed.setValue(max(float(self.spec.animation_speed), 0.1))
        self.spin_animation_speed.valueChanged.connect(self._animation_speed_changed)
        self.spin_animation_speed.setMaximumWidth(68); self.spin_animation_speed.setFixedHeight(28); self.spin_animation_speed.setVisible(False)
        top.addWidget(self.spin_animation_speed)
        top.addStretch(1)

        self.btn_auto_render = QToolButton()
        self.btn_auto_render.setObjectName("CompactGraphTool")
        self.btn_auto_render.setIcon(compact_tool_icon("play"))
        self.btn_auto_render.setToolTip("Auto-map the variables this chart requires, then force a fresh render")
        self.btn_auto_render.setFixedSize(28, 28)
        self.btn_auto_render.clicked.connect(self.auto_render_requested.emit)
        top.addWidget(self.btn_auto_render)

        self.btn_export = QToolButton()
        self.btn_export.setObjectName("CompactGraphTool"); self.btn_export.setIcon(compact_tool_icon("export"))
        self.btn_export.setToolTip("Export figure"); self.btn_export.setFixedSize(28, 28)
        self.btn_export.clicked.connect(self.export_figure); top.addWidget(self.btn_export)
        top_scroll.setWidget(top_body)
        top_scroll.setFixedHeight(34)
        self.top_controls.addWidget(top_scroll)
        root.addWidget(self.top_controls)

        # --------------------------------------------------------- plot area
        self.plot_host = QWidget()
        self.plot_host.setObjectName("GraphVisPlotHost")
        self.plot_host.setAttribute(Qt.WA_StyledBackground, True)
        self.plot_host.setAttribute(Qt.WA_StaticContents, True)
        self.plot_host.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.plot_host.setMinimumSize(320, 140)
        self.plot_container = QVBoxLayout(self.plot_host)
        self.plot_container.setContentsMargins(0, 0, 0, 0)
        self.lbl_error = QLabel("")
        self.lbl_error.setStyleSheet("color:#C0392B;")
        self.lbl_error.setWordWrap(True)
        self.lbl_error.hide()
        plot_area = QWidget()
        self.plot_area = plot_area
        plot_lay = QVBoxLayout(plot_area)
        plot_lay.setContentsMargins(0, 0, 0, 0)
        plot_lay.setSpacing(2)
        plot_lay.addWidget(self.plot_host, 1)
        plot_lay.addWidget(self.lbl_error)

        # --------------------------------------------------- bottom controls
        # The Interactive strip (including its Axis clipping section) always
        # initialises collapsed; the chevron still expands it on demand.
        self.drawer = DetachablePanel("Interactive", expanded=False)
        self.drawer.title_label.hide()
        self.drawer.header.setToolTip("Interactive controls — collapse, resize, or pop out")
        self.drawer.header.setFixedHeight(22); self.drawer.toggle.setFixedSize(18, 18); self.drawer.detach_button.setFixedSize(18, 18)
        row1_widget = QWidget()
        row1_widget.setObjectName("InteractiveControlStripBody")
        row1 = QHBoxLayout(row1_widget)
        row1.setContentsMargins(3, 2, 3, 2)
        row1.setSpacing(4)
        row1.addWidget(QLabel("Colourmap"))
        self.bottom_cmap = QComboBox()
        populate_colormap_combo(self.bottom_cmap, self.spec.colourmap)
        self.bottom_cmap.setMinimumWidth(120); self.bottom_cmap.setMaximumWidth(150); self.bottom_cmap.setFixedHeight(27)
        row1.addWidget(self.bottom_cmap)
        self.btn_apply_bottom_cmap = self._stage_local_combo(
            self.bottom_cmap,
            "Apply the selected colourmap. Merely opening/selecting the dropdown does not redraw the figure.",
        )
        row1.addWidget(self.btn_apply_bottom_cmap)

        row1.addWidget(QLabel("Noise"))
        self.chk_noise = QCheckBox("On")
        self.chk_noise.setToolTip("Master switch for response outlier/noise rejection. Turn off to use every finite sample.")
        self.chk_noise.setChecked(self.spec.noise_filter_enabled)
        self.chk_noise.toggled.connect(self._trigger_change)
        row1.addWidget(self.chk_noise)
        self.cb_noise_method = QComboBox()
        self.cb_noise_method.addItems(["Off", "Z-Score", "IQR"])
        self.cb_noise_method.setCurrentText(self.spec.noise_filter_method if self.spec.noise_filter_enabled else "Off")
        self.cb_noise_method.setToolTip("Off disables noise filtering completely. Z-Score rejects deviations by standard deviations; IQR uses robust quartile fences.")
        row1.addWidget(self.cb_noise_method)
        self.btn_apply_noise_method = self._stage_local_combo(
            self.cb_noise_method,
            "Apply the selected noise filter method. Choose Off to disable filtering completely.",
            after_apply=self._noise_method_changed,
        )
        row1.addWidget(self.btn_apply_noise_method)
        row1.addWidget(QLabel("σ"))
        self.spin_threshold = QDoubleSpinBox()
        self.spin_threshold.setRange(0.5, 10.0)
        self.spin_threshold.setSingleStep(0.1)
        self.spin_threshold.setValue(self.spec.noise_filter_threshold)
        self.spin_threshold.valueChanged.connect(self._trigger_change)
        row1.addWidget(self.spin_threshold)

        row1.addWidget(QLabel("Smooth"))
        self.chk_smoothing = QCheckBox()
        self.chk_smoothing.setChecked(self.spec.smoothing > 0)
        self.chk_smoothing.toggled.connect(self._trigger_change)
        row1.addWidget(self.chk_smoothing)
        self.spin_smoothing = QDoubleSpinBox()
        self.spin_smoothing.setRange(0.1, 100.0)
        self.spin_smoothing.setSingleStep(0.1)
        self.spin_smoothing.setDecimals(2)
        self.spin_smoothing.setToolTip("Gaussian smoothing sigma. Range 0.1-100; very high values can heavily flatten structure and take longer on large grids.")
        self.spin_smoothing.setValue(max(self.spec.smoothing, 0.1))
        self.spin_smoothing.valueChanged.connect(self._trigger_change)
        row1.addWidget(self.spin_smoothing)

        self.chk_gradient_overlay = QCheckBox("Gradient")
        self.chk_gradient_overlay.setChecked(self.spec.quiver_overlay)
        self.chk_gradient_overlay.toggled.connect(self._trigger_change)
        row1.addWidget(self.chk_gradient_overlay)
        self.cb_quiver = QComboBox()
        self.cb_quiver.addItems(["Quiver", "Streamplot"])
        self.cb_quiver.setCurrentText(self.spec.quiver_type if self.spec.quiver_type in ("Quiver", "Streamplot") else "Quiver")
        row1.addWidget(self.cb_quiver)
        self.btn_apply_quiver = self._stage_local_combo(
            self.cb_quiver,
            "Apply the selected gradient-vector display. Quiver draws arrows; Streamplot draws continuous flow lines.",
        )
        row1.addWidget(self.btn_apply_quiver)
        row1.addWidget(QLabel("Opacity"))
        self.slider_opacity = QSlider(Qt.Horizontal)
        self.slider_opacity.setRange(5, 100)
        self.slider_opacity.setValue(int(round(self.spec.surface_alpha * 100)))
        self.slider_opacity.setFixedWidth(78)
        self.slider_opacity.valueChanged.connect(self._trigger_change)
        row1.addWidget(self.slider_opacity)
        row1.addWidget(QLabel("Assumed"))
        self.slider_assumed_alpha = QSlider(Qt.Horizontal)
        self.slider_assumed_alpha.setRange(5, 80)
        self.slider_assumed_alpha.setValue(int(round(self.spec.assumed_alpha * 100)))
        self.slider_assumed_alpha.setFixedWidth(72)
        self.slider_assumed_alpha.valueChanged.connect(self._trigger_change)
        row1.addWidget(self.slider_assumed_alpha)
        row1.addWidget(QLabel("Line"))
        self.spin_line_width = QDoubleSpinBox()
        self.spin_line_width.setRange(0.2, 8.0)
        self.spin_line_width.setSingleStep(0.2)
        self.spin_line_width.setValue(float(self.spec.line_width))
        self.spin_line_width.valueChanged.connect(self._trigger_change)
        self.spin_line_width.setMaximumWidth(70)
        row1.addWidget(self.spin_line_width)
        row1.addWidget(QLabel("Marker"))
        self.cb_marker = QComboBox()
        self.cb_marker.addItems(["None", "o", "s", "^", "D", "v", "x", "+", ".", "*"])
        self.cb_marker.setCurrentText(self.spec.marker or "None")
        self.cb_marker.setMaximumWidth(70)
        row1.addWidget(self.cb_marker)
        self.btn_apply_marker = self._stage_local_combo(
            self.cb_marker,
            "Apply the selected series marker symbol. None draws the series without point markers.",
        )
        row1.addWidget(self.btn_apply_marker)

        # ---- point-cloud readability & density management ----------------
        row1.addWidget(QLabel("Pts"))
        self.spin_point_budget = QSpinBox()
        self.spin_point_budget.setRange(0, 20000); self.spin_point_budget.setSingleStep(500)
        self.spin_point_budget.setSpecialValueText("auto")
        self.spin_point_budget.setValue(int(getattr(self.spec, "point_budget", 0) or 0))
        self.spin_point_budget.setToolTip("Point thinning budget: decimate dense clouds to this many points while "
                                          "preserving the envelope and extrema (0 = automatic global budget).")
        self.spin_point_budget.setMaximumWidth(78)
        self.spin_point_budget.valueChanged.connect(self._trigger_change)
        row1.addWidget(self.spin_point_budget)
        self.chk_density_alpha = QCheckBox("Density α")
        self.chk_density_alpha.setChecked(bool(getattr(self.spec, "density_alpha", False)))
        self.chk_density_alpha.setToolTip("Density-dependent transparency: heavily overlapping points fade into "
                                          "smooth gradient clouds instead of opaque blobs.")
        self.chk_density_alpha.toggled.connect(self._trigger_change)
        row1.addWidget(self.chk_density_alpha)
        self.chk_auto_aggregate = QCheckBox("Aggregate")
        self.chk_auto_aggregate.setChecked(bool(getattr(self.spec, "auto_aggregate", False)))
        self.chk_auto_aggregate.setToolTip("Automatically switch very dense 2-D scatters into a binned hexagonal "
                                           "density map when point counts exceed the readability threshold.")
        self.chk_auto_aggregate.toggled.connect(self._trigger_change)
        row1.addWidget(self.chk_auto_aggregate)
        self.chk_color_gate = QCheckBox("Gate W/V")
        self.chk_color_gate.setChecked(bool(getattr(self.spec, "color_gate", False)))
        self.chk_color_gate.setToolTip("Noise gate: the 4th/5th-axis clipping sliders REMOVE points outside their "
                                       "range instead of only rescaling colour/size — isolate the Pareto frontier "
                                       "or high-performing clusters with one toggle.")
        self.chk_color_gate.toggled.connect(self._trigger_change)
        row1.addWidget(self.chk_color_gate)

        self.btn_apply_local_dropdowns = QToolButton()
        self.btn_apply_local_dropdowns.setText("▶ All")
        self.btn_apply_local_dropdowns.setToolTip("Apply every pending dropdown in this Interactive strip in one redraw.")
        self.btn_apply_local_dropdowns.setFixedHeight(27)
        self.btn_apply_local_dropdowns.clicked.connect(self._apply_all_local_combos)
        self.btn_apply_local_dropdowns.setVisible(manual_apply_mode())
        row1.addWidget(self.btn_apply_local_dropdowns)
        row1.addStretch(1)

        bottom_scroll = QScrollArea()
        bottom_scroll.setObjectName("InteractiveControlStrip")
        bottom_scroll.setWidgetResizable(True)
        bottom_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        bottom_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        bottom_scroll.setFrameShape(QFrame.NoFrame)
        row1_widget.setMinimumWidth(max(row1_widget.sizeHint().width(), 900))
        bottom_scroll.setWidget(row1_widget)
        bottom_scroll.setFixedHeight(36)
        self.drawer.addWidget(bottom_scroll)

        self.drawer.addWidget(QLabel("Axis clipping"))
        self.range_sliders: dict[str, RangeSlider] = {}
        for key, label in (('x', 'X:'), ('y', 'Y:'), ('z', 'Z / colour:'), ('w', '4th axis:'), ('v', '5th axis:')):
            ar = QHBoxLayout()
            lbl = QLabel(label)
            lbl.setMinimumWidth(52)
            ar.addWidget(lbl)
            rs = RangeSlider(0.0, 1.0)
            rs.rangeChanged.connect(lambda lo, hi, k=key: self._on_range_slider_changed(k, lo, hi))
            ar.addWidget(rs)
            btn_fit = QPushButton("Fit")
            btn_fit.setFixedWidth(40); btn_fit.setFixedHeight(26)
            btn_fit.clicked.connect(lambda _=False, k=key: self._fit_range_slider(k))
            ar.addWidget(btn_fit)
            self.drawer.addLayout(ar)
            self.range_sliders[key] = rs

        # Do not let the bottom control panel's large sizeHint dominate the
        # vertical splitter.  Its contents remain scrollable/detachable, but
        # the panel itself may be reduced to its header by dragging.
        self.drawer.setMinimumHeight(24)
        self.drawer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Ignored)
        try:
            self.drawer.content.setMinimumHeight(0)
            self.drawer.content.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Ignored)
        except Exception:
            pass

        # A true splitter between graph and controls fixes the old binary
        # "huge or tiny" drawer behaviour.  The user can drag this separator
        # continuously; the drawer can also be collapsed or detached.
        self.graph_splitter = CompactSplitter(Qt.Vertical)
        self.graph_splitter.setChildrenCollapsible(True)
        self.graph_splitter.setCollapsible(0, False)
        self.graph_splitter.setCollapsible(1, True)
        self.graph_splitter.addWidget(plot_area)
        self.graph_splitter.addWidget(self.drawer)
        self.graph_splitter.setStretchFactor(0, 8)
        self.graph_splitter.setStretchFactor(1, 1)
        saved_bottom = app_settings().value("ui/graph_bottom_controls_height", 150, int)
        if self.drawer.isExpanded():
            self.graph_splitter.setSizes([800, max(70, min(int(saved_bottom or 150), 420))])
        else:
            self.graph_splitter.setSizes([900, 24])
        self.drawer.toggle.clicked.connect(lambda on: app_settings().setValue("ui/graph_interactive_expanded", bool(on)))
        self.graph_splitter.splitterMoved.connect(self._remember_graph_splitter)
        root.addWidget(self.graph_splitter, 1)
        # Numeric-input reliability: commit typed values on Enter/focus-out only
        # (no per-keystroke intermediate values corrupting ranges) and keep the
        # embedded step arrows visible on every numeric box.
        for spin in self.findChildren(QAbstractSpinBox):
            spin.setKeyboardTracking(False)
            spin.setButtonSymbols(QAbstractSpinBox.UpDownArrows)
            install_typing_commit(spin)

    def set_manual_apply(self, manual: bool) -> None:
        """Show/hide the tactile ▶ Apply buttons for this canvas's dropdowns."""
        manual = bool(manual)
        for button in (self.btn_apply_bottom_cmap, self.btn_apply_noise_method,
                       self.btn_apply_quiver, self.btn_apply_marker, self.btn_apply_local_dropdowns):
            button.setVisible(manual)

    def _remember_graph_splitter(self, _pos: int = 0, _index: int = 0) -> None:
        sizes = self.graph_splitter.sizes()
        if len(sizes) >= 2 and sizes[1] >= 0:
            app_settings().setValue("ui/graph_bottom_controls_height", int(sizes[1]))

    def set_drawer_visible(self, on: bool):
        # Advanced view exposes the Interactive strip but never force-expands
        # it: the Axis clipping section stays collapsed until the user opens it.
        if not on:
            self.drawer.setExpanded(False)

    # ------------------------------------------------ tactical dropdown apply
    def _stage_local_combo(self, combo: QComboBox, help_text: str, after_apply=None) -> QToolButton:
        """Interactive-strip combos auto-apply with a short debounce.

        The last committed value is remembered in ``_graphvis_applied_text`` so
        a cancelled/failed render can roll the combo back to its confirmed
        state.  The per-combo play button is retained (hidden) for
        back-compatibility with saved layouts and static smoke tests.
        """
        combo.setToolTip((combo.toolTip() + "\n\n" if combo.toolTip() else "") + help_text)
        combo.setProperty("_graphvis_applied_text", combo.currentText())
        button = QToolButton()
        button.setObjectName("GraphVisApplyChoiceButton")
        button.setText("▶")
        button.setFixedSize(25, 25)
        button.setToolTip(help_text)
        button.setEnabled(False)
        button.setVisible(manual_apply_mode())

        def pending(*_):
            applied = str(combo.property("_graphvis_applied_text") or "")
            if combo.currentText() != applied:
                button.setEnabled(True)
                if not manual_apply_for(self.spec.chart_type):
                    # Reactive path: commit after a short debounce so rapid
                    # keyboard/scroll navigation coalesces into one render.
                    self._local_combo_debounce.start()

        def apply_one():
            combo.setProperty("_graphvis_applied_text", combo.currentText())
            button.setEnabled(False)
            if after_apply is not None:
                after_apply(combo.currentText())
            else:
                self._trigger_change()

        combo.currentTextChanged.connect(pending)
        button.clicked.connect(apply_one)
        return button

    @staticmethod
    def _applied_local_text(combo: QComboBox) -> str:
        value = combo.property("_graphvis_applied_text")
        return str(value) if value is not None else combo.currentText()

    def _sync_local_combo(self, combo: QComboBox, button: QToolButton | None = None) -> None:
        combo.setProperty("_graphvis_applied_text", combo.currentText())
        if button is not None:
            button.setEnabled(False)

    def _apply_all_local_combos(self) -> None:
        for combo, button in (
            (self.bottom_cmap, self.btn_apply_bottom_cmap),
            (self.cb_noise_method, self.btn_apply_noise_method),
            (self.cb_quiver, self.btn_apply_quiver),
            (self.cb_marker, self.btn_apply_marker),
        ):
            self._sync_local_combo(combo, button)
        method = self._applied_local_text(self.cb_noise_method)
        on = method != "Off"
        self.chk_noise.blockSignals(True); self.chk_noise.setChecked(on); self.chk_noise.blockSignals(False)
        self._trigger_change()

    # ----------------------------------------------------------- state
    def _noise_method_changed(self, method: str) -> None:
        on = str(method) != "Off"
        self.chk_noise.blockSignals(True); self.chk_noise.setChecked(on); self.chk_noise.blockSignals(False)
        self._trigger_change()

    def local_state(self) -> dict:
        return {"x_scale": self.spec.x_scale, "y_scale": self.spec.y_scale, "z_scale": self.spec.z_scale,
                "cmap": self._applied_local_text(self.bottom_cmap), "grid": self.chk_grid.isChecked(),
                "opacity": self.slider_opacity.value() / 100.0,
                "assumed_alpha": self.slider_assumed_alpha.value() / 100.0,
                "noise": self.chk_noise.isChecked(), "noise_method": self._applied_local_text(self.cb_noise_method),
                "threshold": self.spin_threshold.value(), "smooth_on": self.chk_smoothing.isChecked(),
                "smoothing": self.spin_smoothing.value(), "quiver": self.chk_gradient_overlay.isChecked(),
                "quiver_type": self._applied_local_text(self.cb_quiver), "line_width": self.spin_line_width.value(),
                "marker": "" if self._applied_local_text(self.cb_marker) == "None" else self._applied_local_text(self.cb_marker),
                "animation_speed": self.spin_animation_speed.value(), "animation_trail": int(self.spec.animation_trail),
                "point_budget": self.spin_point_budget.value(), "density_alpha": self.chk_density_alpha.isChecked(),
                "auto_aggregate": self.chk_auto_aggregate.isChecked(), "color_gate": self.chk_color_gate.isChecked(),
                "clips": dict(self.spec.clipping_ranges)}

    def apply_local_state(self, st: dict):
        widgets = (self.bottom_cmap, self.chk_grid, self.slider_opacity, self.slider_assumed_alpha,
                   self.chk_noise, self.cb_noise_method, self.spin_threshold, self.chk_smoothing, self.spin_smoothing,
                   self.chk_gradient_overlay, self.cb_quiver, self.spin_line_width, self.cb_marker, self.spin_animation_speed,
                   self.spin_point_budget, self.chk_density_alpha, self.chk_auto_aggregate, self.chk_color_gate)
        for w in widgets:
            w.blockSignals(True)
        try:
            self.bottom_cmap.setCurrentText(st.get("cmap", "Parula"))
            self.chk_grid.setChecked(bool(st.get("grid", True)))
            self.slider_opacity.setValue(int(round(float(st.get("opacity", 1.0)) * 100)))
            self.slider_assumed_alpha.setValue(int(round(float(st.get("assumed_alpha", 0.28)) * 100)))
            self.chk_noise.setChecked(bool(st.get("noise", False)))
            self.cb_noise_method.setCurrentText(st.get("noise_method", "Z-Score") if st.get("noise", False) else "Off")
            self.spin_threshold.setValue(float(st.get("threshold", 2.5)))
            self.chk_smoothing.setChecked(bool(st.get("smooth_on", True)))
            self.spin_smoothing.setValue(float(st.get("smoothing", 1.2)))
            self.chk_gradient_overlay.setChecked(bool(st.get("quiver", False)))
            self.cb_quiver.setCurrentText(st.get("quiver_type", "Quiver"))
            self.spin_line_width.setValue(float(st.get("line_width", self.spec.line_width)))
            self.cb_marker.setCurrentText(st.get("marker") or "None")
            self.spin_animation_speed.setValue(float(st.get("animation_speed", self.spec.animation_speed)))
            self.spin_point_budget.setValue(int(st.get("point_budget", 0) or 0))
            self.chk_density_alpha.setChecked(bool(st.get("density_alpha", False)))
            self.chk_auto_aggregate.setChecked(bool(st.get("auto_aggregate", False)))
            self.chk_color_gate.setChecked(bool(st.get("color_gate", False)))
        finally:
            for w in widgets:
                w.blockSignals(False)
        self._sync_local_combo(self.bottom_cmap, self.btn_apply_bottom_cmap)
        self._sync_local_combo(self.cb_noise_method, self.btn_apply_noise_method)
        self._sync_local_combo(self.cb_quiver, self.btn_apply_quiver)
        self._sync_local_combo(self.cb_marker, self.btn_apply_marker)
        self.spec.x_scale = str(st.get("x_scale", self.spec.x_scale))
        self.spec.y_scale = str(st.get("y_scale", self.spec.y_scale))
        self.spec.z_scale = str(st.get("z_scale", self.spec.z_scale))
        self.spec.clipping_ranges = dict(st.get("clips", {}))
        self._push_local_to_spec()

    def _push_local_to_spec(self):
        sp = self.spec
        sp.colourmap = self._applied_local_text(self.bottom_cmap)
        sp.grid_visible = self.chk_grid.isChecked()
        sp.surface_alpha = self.slider_opacity.value() / 100.0
        sp.assumed_alpha = self.slider_assumed_alpha.value() / 100.0
        noise_method = self._applied_local_text(self.cb_noise_method)
        sp.noise_filter_enabled = self.chk_noise.isChecked() and noise_method != "Off"
        sp.noise_filter_method = "Z-Score" if noise_method == "Off" else noise_method
        sp.noise_filter_threshold = self.spin_threshold.value()
        sp.smoothing = self.spin_smoothing.value() if self.chk_smoothing.isChecked() else 0.0
        sp.quiver_overlay = self.chk_gradient_overlay.isChecked()
        sp.quiver_type = self._applied_local_text(self.cb_quiver)
        sp.line_width = self.spin_line_width.value()
        marker = self._applied_local_text(self.cb_marker)
        sp.marker = "" if marker == "None" else marker
        sp.animation_speed = self.spin_animation_speed.value()
        sp.point_budget = self.spin_point_budget.value()
        sp.density_alpha = self.chk_density_alpha.isChecked()
        sp.auto_aggregate = self.chk_auto_aggregate.isChecked()
        sp.color_gate = self.chk_color_gate.isChecked()

    def _sync_local_from_spec(self):
        sp = self.spec
        self.apply_local_state({"x_scale": sp.x_scale, "y_scale": sp.y_scale, "z_scale": sp.z_scale,
                                "cmap": sp.colourmap, "grid": sp.grid_visible,
                                "opacity": sp.surface_alpha, "assumed_alpha": sp.assumed_alpha, "noise": sp.noise_filter_enabled,
                                "noise_method": sp.noise_filter_method, "threshold": sp.noise_filter_threshold,
                                "smooth_on": sp.smoothing > 0, "smoothing": max(sp.smoothing, 0.1),
                                "quiver": sp.quiver_overlay, "quiver_type": sp.quiver_type,
                                "line_width": sp.line_width, "marker": sp.marker, "animation_speed": sp.animation_speed,
                                "point_budget": getattr(sp, "point_budget", 0), "density_alpha": getattr(sp, "density_alpha", False),
                                "auto_aggregate": getattr(sp, "auto_aggregate", False), "color_gate": getattr(sp, "color_gate", False),
                                "clips": sp.clipping_ranges})

    def _trigger_change(self):
        self._push_local_to_spec()
        self.spec_changed.emit(self.spec)
        self.request_render()

    # ------------------------------------------------------- range sliders
    def _column_for(self, key):
        col = self.spec.mappings.get(key)
        for ds in self.spec.datasets.values():
            if col and col in ds.df.columns:
                return col
        return None

    def _sync_range_slider_domains(self):
        for key, rs in self.range_sliders.items():
            col = self._column_for(key)
            lo, hi = 0.0, 1.0
            for ds in self.spec.datasets.values():
                if col and col in ds.df.columns:
                    v = ds.df[col].to_numpy(float)
                    v = v[np.isfinite(v)]
                    if v.size:
                        lo, hi = float(v.min()), float(v.max())
                    break
            rs.setEnabled(col is not None)
            rs.setDomain(lo, hi, keep_selection=True)
            clip = self.spec.clipping_ranges.get(key)
            if clip and clip[0] != clip[1]:
                rs.setValues(clip[0], clip[1])

    def _on_range_slider_changed(self, key, lo, hi):
        rs = self.range_sliders[key]
        if rs.isFullRange():
            self.spec.clipping_ranges.pop(key, None)
        else:
            self.spec.clipping_ranges[key] = (lo, hi)
        self.spec_changed.emit(self.spec)
        self.request_render()

    def _fit_range_slider(self, key):
        rs = self.range_sliders[key]
        lo, hi = rs.domain()
        rs.setValues(lo, hi, emit=True)

    # ------------------------------------------------------- rendering
    def _try_fast_visual_update(self) -> bool:
        """Mutate cheap Matplotlib artist properties without rebuilding data.

        Scientific data/geometry changes still go through the background render
        pipeline.  Pure colourmap/grid changes are intentionally handled in the
        GUI thread because they only touch existing artists and should feel
        instantaneous even for a 250x250 surface.
        """
        snap = self._rendered_spec_snapshot
        canvas = self.current_plot_widget
        if snap is None or canvas is None or self.last_result is None:
            return False
        try:
            before = snap.to_dict(); after = self.spec.to_dict()
            # These fields do not represent scientific/render content for this
            # comparison. View state is managed separately by navigation.
            before.pop("view_state", None); after.pop("view_state", None)
            changed = {k for k in set(before) | set(after) if before.get(k) != after.get(k)}
        except Exception:
            return False
        if not changed or not changed.issubset({"colourmap", "grid_visible", "surface_alpha"}):
            return False
        fig = canvas.figure
        if "colourmap" in changed:
            cmap = resolve_colourmap(self.spec.colourmap)
            for ax in fig.axes:
                for artist in list(getattr(ax, "collections", ())) + list(getattr(ax, "images", ())):
                    try:
                        old_cmap = artist.get_cmap() if hasattr(artist, "get_cmap") else None
                        # Preserve the single-colour failed-simulation fallback layer.
                        if old_cmap is not None and hasattr(old_cmap, "colors") and len(getattr(old_cmap, "colors", ())) == 1:
                            continue
                        if hasattr(artist, "set_cmap") and getattr(artist, "get_array", lambda: None)() is not None:
                            artist.set_cmap(cmap)
                            cb = getattr(artist, "colorbar", None)
                            if cb is not None:
                                cb.update_normal(artist)
                    except Exception:
                        continue
        if "surface_alpha" in changed:
            alpha = max(0.05, min(1.0, float(self.spec.surface_alpha)))
            for ax in fig.axes:
                for artist in list(getattr(ax, "collections", ())) + list(getattr(ax, "images", ())):
                    try:
                        if getattr(artist, "get_gid", lambda: None)() == "graphvis-surface-field":
                            artist.set_alpha(alpha)
                    except Exception:
                        continue
        if "grid_visible" in changed:
            for ax in fig.axes:
                try:
                    # Do not add a generic grid over automatic MATLAB-style
                    # scientific fields unless the user explicitly enabled it.
                    ax.grid(bool(self.spec.grid_visible), alpha=0.22)
                except Exception:
                    pass
        self._rendered_spec_snapshot = self.spec.clone()
        canvas.draw_idle()
        self.status_message.emit("Applied visual style without rebuilding scientific data.")
        return True

    def update_spec(self, spec: PlotSpec):
        self.spec = spec
        self._sync_local_from_spec()
        self._sync_range_slider_domains()
        self.request_render()

    def request_render(self):
        if self._try_fast_visual_update():
            return
        self._seq += 1
        seq = self._seq
        spec = self.spec.clone()
        spec.view_state = None

        # Interactive previews favour responsiveness; export always re-renders
        # the untouched source spec at the requested publication quality.
        settings = app_settings()
        low_power = low_power_active()
        interactive_dpi = settings.value("render/interactive_dpi", 84, int)
        if low_power:
            interactive_dpi = min(int(interactive_dpi), 72)
        fast_preview = settings.value("render/fast_preview", True, bool)
        surface_charts = {"2D Heatmap", "2D Contour", "3D Topography / Surface", "3D Mesh",
                          "Surface + Contours", "Waterfall", "Ribbon", "3D Contour",
                          "Quiver Field", "Stream Field"}
        if fast_preview or low_power:
            spec.metadata = dict(spec.metadata or {})
            max_points = settings.value("render/interactive_max_points", 10000, int)
            if low_power:
                max_points = min(int(max_points), 4000)
            spec.metadata["interactive_max_points"] = max_points
            if bool(spec.metadata.get("progressive_surface_preview", False)) and spec.chart_type in surface_charts:
                # Slider-drag previews are intentionally coarse.  On release the
                # metadata flag disappears and the user's requested grid is
                # restored rather than remaining stuck at the old 96×96 cap.
                progressive_cap = settings.value("render/progressive_grid_resolution", 48, int)
                spec.grid_resolution = min(int(spec.grid_resolution), 32 if low_power else int(progressive_cap))
            elif spec.chart_type not in surface_charts:
                interactive_cap = settings.value("render/interactive_grid_resolution", 96, int)
                spec.grid_resolution = min(int(spec.grid_resolution), 64 if low_power else int(interactive_cap))
        key = spec.cache_key(dpi=interactive_dpi)
        cached = self._fig_cache.get(key)
        if cached is not None:
            self._install_result(cached, from_cache=True)
            return
        if self._active_compute_task is not None:
            self._active_compute_task.cancel()
            self._active_compute_task = None
        if self._active_task is not None:
            self._active_task.cancel()
            self._active_task = None
        self._preview_fallback_timer.stop()
        self._queued_full_request = None
        self.lbl_error.hide()
        try:
            mapped = ", ".join(f"{k}={v}" for k, v in spec.mappings.items() if v is not None)
            record_activity("Rendering graph", f"chart={spec.chart_type}; {mapped}; dpi={interactive_dpi}")
        except Exception:
            pass
        animation_charts = {"Animated Line", "Comet", "Comet 3D", "Stream Particles"}
        # Pickling complete Matplotlib figures can add noticeable latency after
        # a render. Keep fast in-memory caching on by default and make the much
        # heavier cross-session disk cache opt-in.
        use_disk = settings.value("cache/figures", False, bool) and spec.chart_type not in animation_charts
        self._render_started_at = time.monotonic()
        self._current_request = (spec, seq, key, use_disk, interactive_dpi)
        predicted = _RENDER_DURATION_HISTORY.get(self._duration_key(spec), 0.0)
        progressive_drag = bool((spec.metadata or {}).get("progressive_surface_preview", False))
        if predicted > self._preview_threshold() and not progressive_drag:
            # This chart family is known to be slow: show a coarse preview
            # immediately and queue the high-resolution render behind it.
            self._queued_full_request = self._current_request
            self.overlay.start(f"Fast preview of {spec.chart_type}… (high-resolution render queued)",
                               progress=2, cancellable=True, immediate=True)
            self._launch_preview_pass(spec, seq)
            return
        self.overlay.start(f"Rendering {spec.chart_type}…", cancellable=True)
        self._preview_fallback_timer.setInterval(int(self._preview_threshold() * 1000))
        self._preview_fallback_timer.start()
        self._dispatch_render(spec, seq, key, use_disk, interactive_dpi, is_preview=False)

    def _preview_threshold(self) -> float:
        """Seconds before falling back to a fast preview (halved in Low-Power Mode)."""
        return self.FAST_PREVIEW_THRESHOLD * (0.5 if low_power_active() else 1.0)

    def _duration_key(self, spec: PlotSpec) -> str:
        return f"{spec.chart_type}|{spec.estimator}"

    def _dispatch_render(self, spec, seq, key, use_disk, dpi, is_preview=False):
        surface_charts = {"2D Heatmap", "2D Contour", "3D Topography / Surface", "3D Mesh",
                          "Surface + Contours", "Waterfall", "Ribbon", "3D Contour",
                          "Quiver Field", "Stream Field"}
        if spec.chart_type in surface_charts:
            pre = SurfacePrecomputeTask(spec, seq, key, use_disk, dpi)
            pre.signals.finished.connect(lambda payload, p=is_preview: self._on_surface_precomputed(payload, p))
            pre.signals.failed.connect(lambda msg, seq=seq, spec=spec, key=key, use_disk=use_disk, dpi=dpi, p=is_preview:
                                       self._on_surface_precompute_failed(seq, spec, key, use_disk, dpi, msg, p))
            pre.signals.progress.connect(lambda msg, pct: self.overlay.set_message(msg, pct))
            pre.signals.cancelled.connect(lambda seq=seq: self._on_task_cancelled(seq))
            self._active_compute_task = pre
            COMPUTE_POOL.start(pre)
        else:
            self._start_matplotlib_render(spec, seq, key, use_disk, dpi, is_preview)

    def _launch_preview_pass(self, spec, seq):
        """Render a low-resolution fast preview of ``spec`` under the same seq."""
        pspec = spec.clone()
        pspec.metadata = dict(pspec.metadata or {})
        pspec.metadata["progressive_surface_preview"] = True
        base_points = int(pspec.metadata.get("interactive_max_points", 10000) or 10000)
        pspec.metadata["interactive_max_points"] = min(base_points, 4000)
        pspec.grid_resolution = min(int(pspec.grid_resolution), 48)
        preview_dpi = 72
        pkey = pspec.cache_key(dpi=preview_dpi)
        self._dispatch_render(pspec, seq, pkey, False, preview_dpi, is_preview=True)

    def _fallback_to_preview(self):
        """A running render crossed the 10 s threshold: yield to a fast preview."""
        if self._current_request is None:
            return
        spec, seq, key, use_disk, dpi = self._current_request
        if seq != self._seq or (self._active_task is None and self._active_compute_task is None):
            return
        dk = self._duration_key(spec)
        _RENDER_DURATION_HISTORY[dk] = max(_RENDER_DURATION_HISTORY.get(dk, 0.0), self.FAST_PREVIEW_THRESHOLD + 1.0)
        if self._active_compute_task is not None:
            self._active_compute_task.cancel(); self._active_compute_task = None
        if self._active_task is not None:
            self._active_task.cancel(); self._active_task = None
        self._queued_full_request = (spec, seq, key, use_disk, dpi)
        self.overlay.set_message("Long render detected — composing a fast low-resolution preview; the high-resolution render is queued…", 5)
        self.status_message.emit("Render exceeded 10 s — falling back to a fast low-resolution preview.")
        self._launch_preview_pass(spec, seq)

    def _on_task_cancelled(self, seq):
        if seq != self._seq:
            return   # superseded — the newer request now owns the overlay
        if self._active_task is None and self._active_compute_task is None and self._queued_full_request is None:
            self.overlay.stop()

    def _start_matplotlib_render(self, spec, seq, key, use_disk, dpi, is_preview=False):
        task = RenderTask(spec, seq, key, use_disk_cache=use_disk, dpi=dpi)
        task.signals.finished.connect(lambda payload, p=is_preview: self._on_render_finished(payload, p))
        task.signals.failed.connect(self._on_render_failed)
        task.signals.progress.connect(lambda msg, pct: self.overlay.set_message(msg, max(62, pct)))
        task.signals.cancelled.connect(lambda seq=seq: self._on_task_cancelled(seq))
        self._active_task = task
        HEAVY_POOL.start(task)

    def _on_surface_precomputed(self, payload, is_preview=False):
        seq, spec, key, use_disk, dpi, summary = payload
        if seq != self._seq:
            return
        self._active_compute_task = None
        spec.metadata = dict(spec.metadata or {})
        spec.metadata["_surface_precompute_summary"] = dict(summary or {})
        try:
            elapsed = float((summary or {}).get("seconds", 0.0))
            if elapsed >= 0.5:
                log_line(f"Surface precompute: {spec.chart_type} took {elapsed:.2f}s before Matplotlib composition", "PERF")
        except Exception:
            pass
        self._start_matplotlib_render(spec, seq, key, use_disk, dpi, is_preview)

    def _on_surface_precompute_failed(self, seq, spec, key, use_disk, dpi, msg, is_preview=False):
        if seq != self._seq:
            return
        self._active_compute_task = None
        # The regular renderer has conservative fallbacks/diagnostic empty
        # figures, so still compose it rather than turning a recoverable
        # estimator issue into a blank UI.
        log_line(f"Surface precompute skipped/fell through: {msg.splitlines()[0] if msg else 'unknown'}", "PERF")
        self._start_matplotlib_render(spec, seq, key, use_disk, dpi, is_preview)

    def _on_render_finished(self, payload, is_preview=False):
        if len(payload) >= 4:
            seq, result, restored, cache_key = payload[:4]
        else:  # compatibility with older task results / restored sessions
            seq, result, restored = payload
            cache_key = self._cache_key_for(result)
        if seq != self._seq:
            return   # stale — a newer request superseded it
        self._active_task = None
        self._active_compute_task = None
        if is_preview:
            self._install_result(result, from_cache=restored, preview=True)
            queued = self._queued_full_request
            if queued is not None and queued[1] == seq:
                qspec, qseq, qkey, quse_disk, qdpi = queued
                self._queued_full_request = None
                self.overlay.start("Rendering high-resolution figure…", progress=4, cancellable=True, immediate=True)
                self._render_started_at = time.monotonic()
                self._dispatch_render(qspec, qseq, qkey, quse_disk, qdpi, is_preview=False)
            return
        self._preview_fallback_timer.stop()
        if self._render_started_at and not restored:
            _RENDER_DURATION_HISTORY[self._duration_key(self.spec)] = time.monotonic() - self._render_started_at
        self._remember(cache_key, result)
        try:
            elapsed = result.summary.get("render_seconds", "?") if isinstance(result.summary, dict) else "?"
            record_activity("Graph render completed", f"chart={self.spec.chart_type}; seconds={elapsed}; cache={bool(restored)}")
        except Exception:
            pass
        self._install_result(result, from_cache=restored)

    def _cache_key_for(self, result: RenderResult) -> str:
        spec = self.spec.clone()
        spec.view_state = None
        return spec.cache_key(dpi=app_settings().value("render/interactive_dpi", 84, int))

    def _remember(self, key, result):
        if key in self._fig_cache:
            return
        self._fig_cache[key] = result
        self._fig_cache_order.append(key)
        while len(self._fig_cache_order) > self.MEMORY_CACHE:
            old = self._fig_cache_order.pop(0)
            self._fig_cache.pop(old, None)

    def _on_render_failed(self, msg):
        self._active_task = None
        self._active_compute_task = None
        self._preview_fallback_timer.stop()
        self._queued_full_request = None
        self.overlay.stop()
        self.lbl_error.setText(f"Render failed: {msg.splitlines()[0]}")
        self.lbl_error.show()
        log_line(f"Render failed for {self.spec.chart_type}: {msg}", "ERROR")
        record_activity("Graph render failed", f"chart={self.spec.chart_type}; error={msg.splitlines()[0] if msg else 'unknown'}")
        self.status_message.emit("Render failed — see Logs/Errors and Logs/Debug")
        # Roll pending inputs back to the last state that actually rendered.
        self._revert_to_rendered_state()

    def _restore_compatible_view(self, fig, prev_view: dict | None, prev_spec: PlotSpec | None) -> None:
        """Carry the user's camera angle and zoom across re-renders.

        Changing visualization types, rendering modes or tool settings must
        not reset axis bounds or the 3-D camera unless the new view is
        genuinely incompatible (different dimensionality, different mapped
        axes/scales, or a different dataset selection).
        """
        if not prev_view or prev_spec is None or not fig.axes:
            return
        if not app_settings().value("ui/persist_viewport", True, bool):
            return
        ax = fig.axes[0]
        new_3d = hasattr(ax, "get_zlim3d")
        had_3d = prev_view.get("elev") is not None
        sp = self.spec
        # Camera orientation survives every 3-D → 3-D transition.
        if new_3d and had_3d:
            try:
                ax.view_init(elev=prev_view.get("elev"), azim=prev_view.get("azim"),
                             roll=prev_view.get("roll", 0) or 0)
            except Exception:
                pass
        # Axis bounds survive when the axes still mean the same thing.
        same_axes = (
            new_3d == had_3d
            and (prev_spec.mappings or {}).get("x") == (sp.mappings or {}).get("x")
            and (prev_spec.mappings or {}).get("y") == (sp.mappings or {}).get("y")
            and canonical_axis_scale(prev_spec.x_scale) == canonical_axis_scale(sp.x_scale)
            and canonical_axis_scale(prev_spec.y_scale) == canonical_axis_scale(sp.y_scale)
            and set(prev_spec.datasets.keys()) == set(sp.datasets.keys())
        )
        if same_axes:
            try:
                if prev_view.get("xlim"):
                    ax.set_xlim(*prev_view["xlim"])
                if prev_view.get("ylim"):
                    ax.set_ylim(*prev_view["ylim"])
                if new_3d and prev_view.get("zlim") and \
                        (prev_spec.mappings or {}).get("z") == (sp.mappings or {}).get("z"):
                    ax.set_zlim3d(*prev_view["zlim"])
            except Exception:
                pass

    def _log_provenance(self, result: RenderResult) -> None:
        """Append a reproducibility record for every figure that reaches the
        screen: parameter-spec hash, dataset revisions, mappings, timestamp."""
        try:
            import datetime as _dt
            import json as _json
            from graphvis.core.paths import LOG_DIR
            record = {
                "utc": _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds"),
                "provenance_hash": (result.summary or {}).get("provenance_hash", ""),
                "chart_type": self.spec.chart_type,
                "estimator": self.spec.estimator,
                "mappings": {k: v for k, v in (self.spec.mappings or {}).items() if v},
                "datasets": {name: list(map(str, ds.cache_key())) for name, ds in self.spec.datasets.items()},
                "clipping": {k: list(v) for k, v in (self.spec.clipping_ranges or {}).items()},
                "scales": [self.spec.x_scale, self.spec.y_scale, self.spec.z_scale],
                "render_seconds": (result.summary or {}).get("render_seconds"),
            }
            with open(os.path.join(str(LOG_DIR), "provenance.jsonl"), "a", encoding="utf-8") as fh:
                fh.write(_json.dumps(record, default=str) + "\n")
        except Exception:
            pass   # provenance logging must never break rendering

    def _auto_expand_bounds(self, fig) -> None:
        """Dynamic bounding-box auto-expansion.

        When plotted content (overlays, markers, freshly ingested data) lies
        outside the axes' current limits and the corresponding axis has no
        explicit clipping range, the box grows to include it with configurable
        padding instead of silently clipping the data.
        """
        if not app_settings().value("ui/auto_expand_bounds", True, bool):
            return
        pad = max(0.0, float(app_settings().value("ui/bounds_padding_pct", 4.0, float))) / 100.0
        clips = self.spec.clipping_ranges or {}
        for ax in fig.axes:
            if hasattr(ax, "get_zlim3d"):
                continue   # 3-D boxes are managed by matplotlib's autoscale
            try:
                box = ax.dataLim
                if not np.all(np.isfinite([box.x0, box.x1, box.y0, box.y1])):
                    continue
                # Expand only when content actually exceeds the current box:
                # deliberately tight surface axes stay tight otherwise.
                if not clips.get("x") and ax.get_xscale() == "linear":
                    lo, hi = ax.get_xlim()
                    span = (box.x1 - box.x0) or 1.0
                    new_lo = box.x0 - span * pad if box.x0 < lo else lo
                    new_hi = box.x1 + span * pad if box.x1 > hi else hi
                    if (new_lo, new_hi) != (lo, hi):
                        ax.set_xlim(new_lo, new_hi)
                if not clips.get("y") and ax.get_yscale() == "linear":
                    lo, hi = ax.get_ylim()
                    span = (box.y1 - box.y0) or 1.0
                    new_lo = box.y0 - span * pad if box.y0 < lo else lo
                    new_hi = box.y1 + span * pad if box.y1 > hi else hi
                    if (new_lo, new_hi) != (lo, hi):
                        ax.set_ylim(new_lo, new_hi)
            except Exception:
                continue

    def _set_preview_border(self, on: bool):
        """Dashed orange frame marking the canvas content as a fast preview."""
        on = bool(on)
        if on == self._preview_active:
            return
        self._preview_active = on
        if on:
            self.plot_host.setStyleSheet("QWidget#GraphVisPlotHost { border: 3px dashed #E67E22; }")
            self.plot_container.setContentsMargins(3, 3, 3, 3)
        else:
            self.plot_host.setStyleSheet("")
            self.plot_container.setContentsMargins(0, 0, 0, 0)

    def _revert_to_rendered_state(self):
        """Roll sliders/dropdowns back to the last successfully applied spec."""
        snap = self._rendered_spec_snapshot
        if snap is not None:
            self.spec = snap.clone()
            self._sync_local_from_spec()
            self._sync_range_slider_domains()
        self.render_reverted.emit()

    def _install_result(self, result: RenderResult, from_cache=False, preview=False):
        self._set_preview_border(preview)
        # Persistent viewport: remember the outgoing figure's camera/limits so
        # a compatible new figure keeps the user's view instead of resetting.
        prev_view = None
        prev_spec = self._rendered_spec_snapshot
        if self.current_plot_widget is not None:
            try:
                prev_view = self.capture_view_state()
            except Exception:
                prev_view = None
        fig = result.fig
        old = self.current_plot_widget
        # Size the worker-built figure to the *current* viewport before the new
        # Qt canvas becomes visible.  Previously a freshly selected visual was
        # briefly shown at the worker's small render size, then enlarged ~90 ms
        # later by resizeEvent, producing a noticeable small-then-grow flash.
        try:
            target_w = old.width() if old is not None and old.width() > 1 else self.plot_host.width()
            target_h = old.height() if old is not None and old.height() > 1 else self.plot_host.height()
            target_w, target_h = max(int(target_w), 2), max(int(target_h), 2)
            dpi = max(float(fig.dpi or 84.0), 1.0)
            fig.set_size_inches(target_w / dpi, target_h / dpi, forward=False)
        except Exception:
            target_w, target_h = 0, 0
        gestures = app_settings().value("ui/touch_gestures", True, bool)
        canvas = GestureCanvas(fig, gestures=gestures)
        if target_w > 1 and target_h > 1:
            canvas.resize(target_w, target_h)
        canvas.mpl_connect('button_press_event', self._on_canvas_click)
        canvas.mpl_connect('motion_notify_event', self._on_canvas_motion)
        canvas.mpl_connect('button_release_event', self._on_canvas_release)
        canvas.view_changed.connect(self._on_view_changed)
        # re-bind interactivity that matplotlib attached to the worker's Agg canvas
        for ax in fig.axes:
            if hasattr(ax, "mouse_init"):
                try:
                    ax.mouse_init()
                except Exception:
                    pass
            leg = ax.get_legend()
            if leg is not None:
                try:
                    leg.set_draggable(False)
                    leg.set_draggable(True)
                except Exception:
                    pass
        self._annotation_artists = []
        self._live_marker_artists = []
        self._inspector_artist = None   # belonged to the previous figure
        for artist in fig.findobj():
            gid = getattr(artist, "get_gid", lambda: None)()
            if isinstance(gid, str) and gid.startswith("graphvis-annotation-"):
                try:
                    idx = int(gid.rsplit("-", 1)[-1])
                    artist.set_picker(True)
                    self._annotation_artists.append((artist, idx))
                except Exception:
                    pass
            elif isinstance(gid, str) and gid.startswith("graphvis-data-marker-"):
                self._live_marker_artists.append(artist)
        canvas.hide()
        if old is not None:
            # replaceWidget keeps the layout cell/geometry stable instead of
            # momentarily laying out both the old and new canvases.
            self.plot_container.replaceWidget(old, canvas)
            old.hide(); old.setParent(None); old.deleteLater()
        else:
            if self._blank_placeholder is not None:
                self.plot_container.removeWidget(self._blank_placeholder)
                self._blank_placeholder.hide()
            self.plot_container.addWidget(canvas)
        self.current_plot_widget = canvas
        canvas.show()
        self.apply_workspace_background(dict((self.spec.metadata or {}).get("ui_theme") or {}), redraw=False)
        self._auto_expand_bounds(fig)
        self._restore_compatible_view(fig, prev_view, prev_spec)
        self._home_view = self._capture_view(fig)
        canvas.set_view_bounds(self._home_view)
        self.last_result = result
        self._rendered_spec_snapshot = self.spec.clone()
        self._configure_animation(result)
        self._rebuild_point_pick_cache()
        canvas.draw_idle()
        if preview:
            # Keep the corner indicator alive: the high-resolution render is
            # still queued/processing behind this fast preview.
            self.overlay.set_message("Fast preview shown — high-resolution render in progress…", 6)
        else:
            self.overlay.stop()
        self.overlay.raise_()
        notes = "; ".join(result.summary.get("notes", [])[:2])
        if preview:
            src = f"fast low-resolution preview in {result.seconds:.2f}s (high-res queued)"
        else:
            src = "restored from cache" if from_cache else f"rendered in {result.seconds:.2f}s"
        self.status_message.emit(f"{self.spec.chart_type}: {src}" + (f" — {notes}" if notes else ""))
        if result.limit_report is not None:
            self.analysis_summary.emit(result.limit_report.text() + ("\n" + "\n".join(result.limit_report.notes) if result.limit_report.notes else ""))
        else:
            self.analysis_summary.emit("")
        for w in result.warnings:
            log_line(w, "RENDER")
        if not preview:
            self._log_provenance(result)
        self.render_finished.emit(result)

    # ------------------------------------------------------- animation playback
    def _configure_animation(self, result: RenderResult):
        self._animation_timer.stop()
        payload = result.summary.get("animation") if result and result.summary else None
        self._animation_payload = payload
        self._animation_index = 0
        visible = bool(payload)
        self.btn_play.setVisible(visible)
        self.spin_animation_speed.setVisible(visible)
        self.btn_play.setText("▶ Play")
        if not visible:
            return
        self._animation_speed_changed(self.spin_animation_speed.value())

    def _animation_speed_changed(self, value):
        self.spec.animation_speed = float(value)
        self._animation_timer.setInterval(max(8, int(round(40.0 / max(float(value), 0.1)))))

    def _toggle_animation(self):
        if not self._animation_payload:
            return
        if self._animation_timer.isActive():
            self._animation_timer.stop(); self.btn_play.setText("▶ Play")
        else:
            self._animation_timer.start(); self.btn_play.setText("❚❚ Pause")

    def _animation_step(self):
        payload = self._animation_payload
        if not payload or self.current_plot_widget is None or not self.current_plot_widget.figure.axes:
            self._animation_timer.stop(); return
        x = np.asarray(payload.get("x", []), float); y = np.asarray(payload.get("y", []), float)
        if not len(x):
            self._animation_timer.stop(); return
        zraw = payload.get("z"); z = np.asarray(zraw, float) if zraw is not None else None
        self._animation_index = (self._animation_index + 1) % len(x)
        i = self._animation_index
        mode = payload.get("mode", self.spec.chart_type)
        if mode == "Animated Line":
            start = 0
        else:
            start = max(0, i - max(2, int(self.spec.animation_trail)))
        ax = self.current_plot_widget.figure.axes[0]
        try:
            line = ax.lines[0] if ax.lines else None
            if z is not None and payload.get("three_d"):
                if line is not None:
                    line.set_data_3d(x[start:i+1], y[start:i+1], z[start:i+1])
                if ax.collections:
                    ax.collections[0]._offsets3d = ([x[i]], [y[i]], [z[i]])
            else:
                if line is not None:
                    line.set_data(x[start:i+1], y[start:i+1])
                if len(ax.lines) > 1:
                    ax.lines[1].set_data([x[i]], [y[i]])
            self.current_plot_widget.draw_idle()
        except Exception as exc:
            self._animation_timer.stop(); self.btn_play.setText("▶ Play")
            log_line(f"Animation update stopped: {exc}", "RENDER")

    # ------------------------------------------------------- view state
    @staticmethod
    def _capture_view(fig) -> dict | None:
        if not fig.axes:
            return None
        ax = fig.axes[0]
        vs = {"xlim": list(ax.get_xlim()), "ylim": list(ax.get_ylim())}
        if hasattr(ax, "get_zlim3d"):
            vs.update(zlim=list(ax.get_zlim3d()), elev=float(ax.elev), azim=float(ax.azim), roll=float(getattr(ax, "roll", 0.0)))
        leg = ax.get_legend()
        if leg is not None and isinstance(getattr(leg, "_loc", None), tuple):
            vs["legend_loc"] = list(leg._loc)
        return vs

    def _on_view_changed(self):
        self._point_display_cache.clear()
        if self._applying_external_view:
            return
        state = self.capture_view_state()
        if state:
            self.view_state_changed.emit(state)

    def apply_external_view(self, state: dict | None):
        if not state or self.current_plot_widget is None or not self.current_plot_widget.figure.axes:
            return
        ax = self.current_plot_widget.figure.axes[0]
        self._applying_external_view = True
        try:
            if state.get("xlim"): ax.set_xlim(*state["xlim"])
            if state.get("ylim"): ax.set_ylim(*state["ylim"])
            if state.get("zlim") and hasattr(ax, "set_zlim3d"): ax.set_zlim3d(*state["zlim"])
            if hasattr(ax, "view_init") and ("elev" in state or "azim" in state):
                ax.view_init(elev=state.get("elev", ax.elev), azim=state.get("azim", ax.azim), roll=state.get("roll", getattr(ax, "roll", 0)))
            self.current_plot_widget.draw_idle()
        finally:
            self._applying_external_view = False

    def capture_view_state(self) -> dict | None:
        if self.current_plot_widget is None:
            return None
        return self._capture_view(self.current_plot_widget.figure)

    def _reset_view(self):
        if self.current_plot_widget is None:
            return
        fig = self.current_plot_widget.figure
        hv = self._home_view
        for ax in fig.axes:
            if hv and ax is fig.axes[0]:
                ax.set_xlim(*hv["xlim"]); ax.set_ylim(*hv["ylim"])
                if "zlim" in hv and hasattr(ax, "set_zlim3d"):
                    ax.set_zlim3d(*hv["zlim"])
                    ax.view_init(elev=hv["elev"], azim=hv["azim"], roll=hv.get("roll", 0))
            else:
                ax.autoscale()
        self.current_plot_widget.draw_idle()
        self._on_view_changed()

    # ------------------------------------------------------- annotations
    @staticmethod
    def _annotation_defaults(kind: str = "text") -> dict:
        ann = {"kind": kind, "active": True, "text": "Annotation", "x": 0.5, "y": 0.5,
               "coords": "axes", "rotation": 0.0, "color": "#2C3E50", "fontsize": 10.0,
               "background": True, "background_color": "#FFF3B0", "background_alpha": 0.72,
               "pointer_style": "->"}
        if kind in ("callout", "arrow", "bracket", "rectangle", "ellipse"):
            ann.update({"x2": 0.72, "y2": 0.68})
        if kind not in ("text", "callout"):
            ann["text"] = ""
        return ann

    def _add_annotation_at(self, text, x, y, coords, kind: str = "text"):
        ann = self._annotation_defaults(kind)
        ann.update({"text": str(text), "x": float(x), "y": float(y), "coords": coords})
        if kind in ("callout", "arrow", "bracket", "rectangle", "ellipse"):
            spanx = 0.08 if coords == "axes" else 0.08 * max(abs(float(x)), 1.0)
            spany = 0.08 if coords == "axes" else 0.08 * max(abs(float(y)), 1.0)
            ann["x2"], ann["y2"] = float(x + spanx), float(y + spany)
        self.spec.annotations.append(ann)
        self.spec_changed.emit(self.spec)
        self.request_render()

    def _add_annotation_prompt(self):
        text, ok = QInputDialog.getText(self, "Add annotation", "Text (Matplotlib MathText/LaTeX supported):")
        if ok and text:
            self._add_annotation_at(text, 0.5, 0.5, "axes")

    def open_annotation_manager(self) -> None:
        dlg = AnnotationManagerDialog(self.spec.annotations, self)
        if dlg.exec():
            self.spec.annotations = dlg.annotations()
            self.spec_changed.emit(self.spec)
            self.request_render()

    def _clear_annotations(self):
        if not self.spec.annotations:
            return
        self.spec.annotations.clear()
        self._annotation_artists = []
        self.spec_changed.emit(self.spec)
        self.request_render()

    def _annotation_hit(self, event) -> tuple[object, int] | None:
        for artist, idx in reversed(self._annotation_artists):
            try:
                if artist.get_visible() and artist.contains(event)[0]:
                    return artist, idx
            except Exception:
                continue
        return None

    def _annotation_xy_from_event(self, event, coords: str) -> tuple[float, float] | None:
        if event.inaxes is None or event.x is None or event.y is None:
            return None
        try:
            if coords == "axes":
                x, y = event.inaxes.transAxes.inverted().transform((event.x, event.y))
                return float(x), float(y)
            if event.xdata is None or event.ydata is None:
                return None
            return float(event.xdata), float(event.ydata)
        except Exception:
            return None

    def _start_annotation_drag(self, idx: int, event) -> None:
        if idx < 0 or idx >= len(self.spec.annotations):
            return
        ann = self.spec.annotations[idx]
        pos = self._annotation_xy_from_event(event, ann.get("coords", "axes"))
        if pos is None:
            return
        self._drag_annotation_index = idx
        self._drag_annotation_start = pos
        self._drag_annotation_origin = dict(ann)

    def _on_canvas_motion(self, event) -> None:
        idx = self._drag_annotation_index
        if idx is None or self._drag_annotation_start is None or self._drag_annotation_origin is None:
            return
        if idx >= len(self.spec.annotations):
            return
        ann = self.spec.annotations[idx]
        pos = self._annotation_xy_from_event(event, ann.get("coords", "axes"))
        if pos is None:
            return
        dx, dy = pos[0] - self._drag_annotation_start[0], pos[1] - self._drag_annotation_start[1]
        origin = self._drag_annotation_origin
        key = str(getattr(event, "key", "") or "").lower()
        if "control" in key or "ctrl" in key:
            ann["rotation"] = float(origin.get("rotation", 0.0)) + dx * 180.0
        elif "shift" in key and "x2" in origin and "y2" in origin:
            # Shape/callout resize: the anchored first point stays fixed while
            # the second handle follows the pointer.
            ann["x2"] = float(origin.get("x2", origin.get("x", 0.0))) + dx
            ann["y2"] = float(origin.get("y2", origin.get("y", 0.0))) + dy
        elif "shift" in key and str(origin.get("kind", "text")) == "text":
            # Text annotations have no second geometry handle. Shift-drag
            # therefore scales the text itself, matching the resize gesture
            # used by shape annotations.
            scale_delta = (dx + dy) * 18.0
            ann["fontsize"] = float(np.clip(float(origin.get("fontsize", 10.0)) + scale_delta, 4.0, 96.0))
        else:
            ann["x"] = float(origin.get("x", 0.0)) + dx
            ann["y"] = float(origin.get("y", 0.0)) + dy
            if "x2" in origin:
                ann["x2"] = float(origin.get("x2", 0.0)) + dx
                ann["y2"] = float(origin.get("y2", 0.0)) + dy
        self._refresh_annotation_artist_live(idx)
        self.status_message.emit(f"Annotation {idx + 1}: x={ann.get('x', 0):.5g}, y={ann.get('y', 0):.5g}, rotation={ann.get('rotation', 0):.1f}°")

    def _refresh_annotation_artist_live(self, idx: int) -> None:
        if self.current_plot_widget is None or idx >= len(self.spec.annotations):
            return
        ann = self.spec.annotations[idx]
        artist = next((a for a, i in self._annotation_artists if i == idx), None)
        if artist is None:
            return
        kind = str(ann.get("kind", "text"))
        x, y = float(ann.get("x", 0.5)), float(ann.get("y", 0.5))
        x2, y2 = float(ann.get("x2", x + 0.1)), float(ann.get("y2", y + 0.1))
        try:
            if kind == "text":
                artist.set_position((x, y)); artist.set_rotation(float(ann.get("rotation", 0))); artist.set_fontsize(float(ann.get("fontsize", 10.0)))
            elif kind == "callout":
                artist.set_position((x, y)); artist.xy = (x2, y2); artist.set_rotation(float(ann.get("rotation", 0)))
            elif kind in ("arrow", "bracket") and hasattr(artist, "set_positions"):
                # Rotate the line geometry around its midpoint. FancyArrowPatch
                # itself has no general rotation property, so rotate endpoints.
                angle = np.deg2rad(float(ann.get("rotation", 0.0)))
                if angle:
                    cx, cy = (x + x2) / 2.0, (y + y2) / 2.0
                    vx, vy = (x2 - x) / 2.0, (y2 - y) / 2.0
                    rx = vx * np.cos(angle) - vy * np.sin(angle)
                    ry = vx * np.sin(angle) + vy * np.cos(angle)
                    artist.set_positions((cx - rx, cy - ry), (cx + rx, cy + ry))
                else:
                    artist.set_positions((x, y), (x2, y2))
            elif kind == "rectangle":
                artist.set_xy((x, y)); artist.set_width(x2 - x); artist.set_height(y2 - y); artist.angle = float(ann.get("rotation", 0))
            elif kind == "ellipse":
                artist.center = ((x + x2) / 2.0, (y + y2) / 2.0); artist.width = abs(x2 - x); artist.height = abs(y2 - y); artist.angle = float(ann.get("rotation", 0))
            self.current_plot_widget.draw_idle()
        except Exception:
            pass

    def _on_canvas_release(self, event) -> None:
        if self._drag_annotation_index is None:
            return
        self._drag_annotation_index = None
        self._drag_annotation_start = None
        self._drag_annotation_origin = None
        self.spec_changed.emit(self.spec)
        self.request_render()

    def _annotation_context_menu(self, idx: int) -> None:
        if idx < 0 or idx >= len(self.spec.annotations):
            return
        ann = self.spec.annotations[idx]
        menu = QMenu(self)
        a_active = menu.addAction("Active / visible")
        a_active.setCheckable(True); a_active.setChecked(bool(ann.get("active", True)))
        a_text = menu.addAction("Edit text / LaTeX…")
        a_font = menu.addAction("Font size…")
        a_colour = menu.addAction("Text / line colour…")
        a_bg = menu.addAction("Background highlight")
        a_bg.setCheckable(True); a_bg.setChecked(bool(ann.get("background", False)))
        a_pointer = menu.addAction("Pointer style…")
        a_rotation = menu.addAction("Rotation…")
        menu.addSeparator(); a_delete = menu.addAction("Delete annotation")
        action = menu.exec(QCursor.pos())
        if action is None:
            return
        if action == a_active:
            ann["active"] = a_active.isChecked()
        elif action == a_text:
            text, ok = QInputDialog.getText(self, "Annotation text", "MathText/LaTeX:", text=str(ann.get("text", "")))
            if ok: ann["text"] = text
        elif action == a_font:
            value, ok = QInputDialog.getDouble(self, "Annotation font", "Font size [pt]:", float(ann.get("fontsize", 10)), 4, 72, 1)
            if ok: ann["fontsize"] = float(value)
        elif action == a_colour:
            c = QColorDialog.getColor(parent=self, title="Annotation colour")
            if c.isValid(): ann["color"] = c.name()
        elif action == a_bg:
            ann["background"] = a_bg.isChecked()
        elif action == a_pointer:
            val, ok = QInputDialog.getItem(self, "Pointer style", "Arrow / pointer:", ["->", "-|>", "-[", "<->", "fancy", "simple", "wedge"], 0, False)
            if ok: ann["pointer_style"] = val
        elif action == a_rotation:
            value, ok = QInputDialog.getDouble(self, "Annotation rotation", "Rotation [deg]:", float(ann.get("rotation", 0)), -360, 360, 1)
            if ok: ann["rotation"] = float(value)
        elif action == a_delete:
            self.spec.annotations.pop(idx)
        self.spec_changed.emit(self.spec)
        self.request_render()

    def _start_inline_text_editor(self, artist, key: str, event) -> None:
        if self._inline_editor is not None:
            self._inline_editor.deleteLater()
        edit = QLineEdit(self.plot_host)
        edit.setText(artist.get_text())
        edit.setToolTip(r"LaTeX/MathText supported, e.g. $\alpha$, $H_2$, $\mu m$")
        x = max(0, min(int(event.x) - 120, self.plot_host.width() - 260))
        y_from_bottom = int(event.y) if event.y is not None else self.plot_host.height() // 2
        y = max(0, min(self.plot_host.height() - y_from_bottom - 20, self.plot_host.height() - 32))
        edit.setGeometry(x, y, 260, 30); edit.show(); edit.raise_(); edit.setFocus(); edit.selectAll()
        self._inline_editor = edit

        def commit() -> None:
            if self._inline_editor is not edit:
                return
            text = edit.text()
            self.spec.styling.setdefault(key, dict(DEFAULT_STYLING.get(key, {})))["text"] = text
            artist.set_text(text)
            if self.current_plot_widget is not None: self.current_plot_widget.draw_idle()
            self.spec_changed.emit(self.spec)
            edit.hide(); edit.deleteLater(); self._inline_editor = None
        edit.returnPressed.connect(commit)
        edit.editingFinished.connect(commit)

    def focus_smoothing_controls(self) -> None:
        self.drawer.setExpanded(True)
        self.chk_smoothing.setChecked(True)
        self.spin_smoothing.setFocus()

    def toggle_roi_selector(self) -> None:
        """Toggle an interactive draggable ROI gadget.

        Moving/resizing the rectangle recalculates local row count, mean/std,
        min/max and trapezoidal integral without mutating the source dataset.
        The context menu can then apply the ROI as a clipping filter or export
        the extracted rows to a new CSV worksheet.
        """
        if self.current_plot_widget is None or not self.current_plot_widget.figure.axes:
            return
        if self._roi_selector is not None:
            try: self._roi_selector.set_active(False)
            except Exception: pass
            self._roi_selector = None
            self.status_message.emit("ROI gadget disabled")
            return
        ax = self.current_plot_widget.figure.axes[0]
        if hasattr(ax, "get_zlim3d"):
            QMessageBox.information(self, "ROI", "Interactive rectangular ROI analysis is available on 2-D axes. Use clipping controls for 3-D views.")
            return

        def selected(eclick, erelease) -> None:
            if None in (eclick.xdata, eclick.ydata, erelease.xdata, erelease.ydata):
                return
            xmin, xmax = sorted((float(eclick.xdata), float(erelease.xdata)))
            ymin, ymax = sorted((float(eclick.ydata), float(erelease.ydata)))
            self._roi_bounds = (xmin, xmax, ymin, ymax)
            self._roi_frame = None
            self._roi_stats = {}
            cx, cy = self.spec.mappings.get("x"), self.spec.mappings.get("y")
            frames = []
            for name, ds in self.spec.datasets.items():
                if cx not in ds.df.columns or cy not in ds.df.columns:
                    continue
                work = ds.df[[cx, cy]].replace([np.inf, -np.inf], np.nan).dropna()
                mask = work[cx].between(xmin, xmax) & work[cy].between(ymin, ymax)
                sub = work.loc[mask].copy()
                if not sub.empty:
                    sub.insert(0, "dataset", name)
                    frames.append(sub)
            if frames:
                roi = pd.concat(frames, ignore_index=True)
                self._roi_frame = roi
                yv = roi[cy].to_numpy(float)
                xv = roi[cx].to_numpy(float)
                order = np.argsort(xv, kind="stable")
                integral = float(_np_trapz(yv[order], xv[order])) if len(xv) > 1 else 0.0
                self._roi_stats = {
                    "count": int(len(roi)), "mean": float(np.mean(yv)), "std": float(np.std(yv, ddof=1)) if len(yv) > 1 else 0.0,
                    "min": float(np.min(yv)), "max": float(np.max(yv)), "integral": integral,
                }
                self.status_message.emit(
                    f"ROI: n={len(roi):,} | Y mean={self._roi_stats['mean']:.5g} | σ={self._roi_stats['std']:.5g} | ∫Y dX={integral:.5g}"
                )
            else:
                self.status_message.emit(f"ROI: no points inside X [{xmin:.5g}, {xmax:.5g}], Y [{ymin:.5g}, {ymax:.5g}]")

        self._roi_selector = RectangleSelector(ax, selected, useblit=True, button=[1], minspanx=4, minspany=4,
                                               spancoords="pixels", interactive=True, drag_from_anywhere=True)
        self.status_message.emit("ROI gadget active — drag/resize the rectangle; statistics update on release")

    def apply_roi_filter(self) -> None:
        bounds = getattr(self, "_roi_bounds", None)
        if not bounds:
            self.status_message.emit("Create an ROI first.")
            return
        xmin, xmax, ymin, ymax = bounds
        self.spec.clipping_ranges["x"] = (xmin, xmax)
        self.spec.clipping_ranges["y"] = (ymin, ymax)
        if "x" in self.range_sliders: self.range_sliders["x"].setValues(xmin, xmax)
        if "y" in self.range_sliders: self.range_sliders["y"].setValues(ymin, ymax)
        self.spec_changed.emit(self.spec)
        self.request_render()

    def export_roi_rows(self) -> None:
        frame = getattr(self, "_roi_frame", None)
        if frame is None or frame.empty:
            self.status_message.emit("The active ROI contains no extractable rows.")
            return
        path, _ = QFileDialog.getSaveFileName(self, "Export ROI data", "roi_extract.csv", "CSV (*.csv)")
        if path:
            frame.to_csv(path, index=False)
            self.status_message.emit(f"ROI rows exported: {path}")


    def _rebuild_point_pick_cache(self) -> None:
        """Cache the points actually eligible for interactive picking once.

        Click latency used to come from rebuilding/cleaning each DataFrame on
        every click.  This cache is rebuilt only when a new figure is installed.
        """
        self._point_pick_cache = []
        self._point_display_cache = {}
        max_points = max(2000, int((self.spec.metadata or {}).get("interactive_max_points", 10000)))
        for name, ds in self.spec.datasets.items():
            cx = self.spec.mappings.get("x")
            cy = self.spec.mappings.get("y")
            cz = self.spec.mappings.get("z")
            if not cx or cx not in ds.df.columns:
                cx = ds.numeric_columns[0] if ds.numeric_columns else None
            if not cy or cy not in ds.df.columns:
                cy = ds.numeric_columns[1] if len(ds.numeric_columns) > 1 else None
            if not cx or not cy:
                continue
            cols = [cx, cy] + ([cz] if cz and cz in ds.df.columns and cz not in (cx, cy) else [])
            try:
                frame = ds.df[cols].replace([np.inf, -np.inf], np.nan).dropna()
            except Exception:
                continue
            if frame.empty:
                continue
            if len(frame) > max_points:
                take = np.linspace(0, len(frame) - 1, max_points).astype(np.int64)
                frame = frame.iloc[take]
            self._point_pick_cache.append({
                "dataset": name, "x": frame[cx].to_numpy(float, copy=False),
                "y": frame[cy].to_numpy(float, copy=False),
                "z": frame[cz].to_numpy(float, copy=False) if cz and cz in frame.columns else None,
            })

    @staticmethod
    def _axis_view_key(ax) -> tuple:
        try:
            key = [id(ax), tuple(np.round(ax.get_xlim(), 10)), tuple(np.round(ax.get_ylim(), 10))]
            if hasattr(ax, "get_zlim3d"):
                key.extend([tuple(np.round(ax.get_zlim3d(), 10)), round(float(ax.elev), 3), round(float(ax.azim), 3)])
            return tuple(key)
        except Exception:
            return (id(ax),)

    def _nearest_data_point(self, event, max_distance_px: float | None = None) -> dict | None:
        if event.inaxes is None or event.x is None or event.y is None:
            return None
        ax = event.inaxes
        best: dict | None = None
        best_d2 = np.inf
        view_key = self._axis_view_key(ax)
        for i, rec in enumerate(self._point_pick_cache):
            x, y, z = rec["x"], rec["y"], rec.get("z")
            cache_key = (i,) + view_key
            disp = self._point_display_cache.get(cache_key)
            if disp is None:
                try:
                    if hasattr(ax, "get_zlim3d") and z is not None:
                        from mpl_toolkits.mplot3d import proj3d
                        xp, yp, _ = proj3d.proj_transform(x, y, z, ax.get_proj())
                        disp = ax.transData.transform(np.column_stack([xp, yp]))
                    else:
                        disp = ax.transData.transform(np.column_stack([x, y]))
                    self._point_display_cache[cache_key] = disp
                except Exception:
                    continue
            d2 = (disp[:, 0] - float(event.x)) ** 2 + (disp[:, 1] - float(event.y)) ** 2
            if not d2.size:
                continue
            k = int(np.argmin(d2)); value = float(d2[k])
            if value < best_d2:
                best_d2 = value
                best = {"dataset": rec["dataset"], "x": float(x[k]), "y": float(y[k]),
                        "z": float(z[k]) if z is not None else None, "distance_px": value ** 0.5, "ax": ax}
        if best is not None and max_distance_px is not None and best["distance_px"] > max_distance_px:
            return None
        return best

    def _clear_inspector_marker(self, redraw: bool = False) -> None:
        """Remove the temporary coordinate-inspector circle, if present.

        The inspector circle previously used near-identical styling to a
        permanent marker and was never removed by the marker deletion paths —
        the classic "one marker always survives deletion" orphan.
        """
        if self._inspector_artist is not None:
            try:
                self._inspector_artist.remove()
            except Exception:
                pass
            self._inspector_artist = None
            if redraw and self.current_plot_widget is not None:
                self.current_plot_widget.draw_idle()

    def _draw_temporary_marker(self, point: dict) -> None:
        ax = point["ax"]; x, y, z = point["x"], point["y"], point.get("z")
        try:
            self._clear_inspector_marker()
            # Deliberately distinct from the red permanent markers: a blue
            # crosshair-style ring that reads as "inspection", not "marker".
            if hasattr(ax, "get_zlim3d") and z is not None:
                self._inspector_artist = ax.scatter([x], [y], [z], s=58, facecolors="none", edgecolors="#1E88E5",
                                                    linewidths=1.4, alpha=0.9, zorder=30)
            else:
                self._inspector_artist, = ax.plot([x], [y], marker="+", markersize=11, markerfacecolor="none",
                                                  markeredgecolor="#1E88E5", markeredgewidth=1.6, linestyle="none",
                                                  alpha=0.9, zorder=30)
            self.current_plot_widget.draw_idle()
        except Exception:
            self._inspector_artist = None

    @staticmethod
    def _format_coordinate(value: float, digits: int = 12) -> str:
        try:
            return f"{float(value):.{int(digits)}g}"
        except Exception:
            return str(value)

    @staticmethod
    def _snap_nearest_enabled() -> bool:
        return app_settings().value("ui/snap_nearest_point", False, bool)

    def _cursor_axis_point(self, event) -> dict | None:
        """Return the coordinate represented by the cursor tip.

        In 2-D, ``event.xdata/ydata`` are the exact inverse transform of the
        cursor position, so markers should use those values rather than snap to
        a nearby sample.  A 3-D screen position cannot be uniquely inverted to
        X/Y/Z, therefore 3-D retains nearest-displayed-point picking.  With
        "Show nearest point on click" enabled, 2-D also snaps to the closest
        displayed data point.
        """
        if event.inaxes is None:
            return None
        ax = event.inaxes
        if hasattr(ax, "get_zlim3d") or self._snap_nearest_enabled():
            point = self._nearest_data_point(event)
            if point is not None and self.current_plot_widget is not None:
                try:
                    point["axis_index"] = self.current_plot_widget.figure.axes.index(ax)
                except Exception:
                    point["axis_index"] = 0
            if point is not None or hasattr(ax, "get_zlim3d"):
                return point
            # snap requested but no cached data point — fall through to cursor
        if event.xdata is None or event.ydata is None:
            return None
        try:
            x, y = float(event.xdata), float(event.ydata)
            if not np.all(np.isfinite([x, y])):
                return None
        except Exception:
            return None
        nearby = self._nearest_data_point(event, max_distance_px=16.0)
        dataset = nearby["dataset"] if nearby is not None else "axis cursor"
        try:
            axis_index = self.current_plot_widget.figure.axes.index(ax) if self.current_plot_widget is not None else 0
        except Exception:
            axis_index = 0
        return {"dataset": dataset, "x": x, "y": y, "z": None,
                "distance_px": 0.0, "ax": ax, "axis_index": axis_index, "cursor_exact": True}

    def _inspect_point(self, event):
        point = self._cursor_axis_point(event)
        if point is None:
            return
        x, y, z = point["x"], point["y"], point.get("z")
        snapped = not point.get("cursor_exact", False)
        source = clean_series_name(point.get("dataset", "axis cursor"))
        if z is not None:
            self.lbl_inspector.setText(
                f"X: {self._format_coordinate(x)}   Y: {self._format_coordinate(y)}   Z: {self._format_coordinate(z)}")
            self.lbl_inspector.setToolTip(
                f"Nearest displayed 3-D data point from {source}; "
                f"screen distance {point.get('distance_px', 0.0):.1f}px. "
                f"Full precision: X={x:.17g}, Y={y:.17g}, Z={float(z):.17g}")
        else:
            self.lbl_inspector.setText(
                f"X: {self._format_coordinate(x)}   Y: {self._format_coordinate(y)}   Z: —")
            if snapped:
                self.lbl_inspector.setToolTip(
                    f"Nearest data point from {source}; screen distance {point.get('distance_px', 0.0):.1f}px. "
                    f"Full precision: X={x:.17g}, Y={y:.17g}.")
            else:
                self.lbl_inspector.setToolTip(
                    f"Exact cursor/axis coordinate (not snapped). Full precision: "
                    f"X={x:.17g}, Y={y:.17g}. Nearest series: {source}.")
        if snapped and self._snap_nearest_enabled():
            # Overlay tooltip at the cursor with the snapped point's exact
            # coordinates and metadata.
            lines = [f"Nearest point — {source}",
                     f"X = {self._format_coordinate(x)}",
                     f"Y = {self._format_coordinate(y)}"]
            if z is not None:
                lines.append(f"Z = {self._format_coordinate(z)}")
            dist = point.get("distance_px")
            if dist is not None and np.isfinite(dist):
                lines.append(f"({dist:.1f} px from cursor)")
            QToolTip.showText(QCursor.pos(), "\n".join(lines), self)
        self._draw_temporary_marker(point)

    def _data_markers(self) -> list[dict]:
        self.spec.metadata = dict(self.spec.metadata or {})
        return self.spec.metadata.setdefault("data_markers", [])

    def _marker_display_distance(self, event, marker: dict) -> float | None:
        if event.inaxes is None or event.x is None or event.y is None:
            return None
        ax = event.inaxes
        try:
            axes = self.current_plot_widget.figure.axes if self.current_plot_widget is not None else []
            marker_axis = int(marker.get("axis_index", 0) or 0)
            if axes and 0 <= marker_axis < len(axes) and axes[marker_axis] is not ax:
                return None
            x, y, z = float(marker["x"]), float(marker["y"]), marker.get("z")
            if hasattr(ax, "get_zlim3d") and z is not None:
                from mpl_toolkits.mplot3d import proj3d
                xp, yp, _ = proj3d.proj_transform([x], [y], [float(z)], ax.get_proj())
                px, py = ax.transData.transform([[xp[0], yp[0]]])[0]
            else:
                px, py = ax.transData.transform([[x, y]])[0]
            return float(((px - event.x) ** 2 + (py - event.y) ** 2) ** 0.5)
        except Exception:
            return None

    def _nearest_marker_index(self, event) -> tuple[int | None, float]:
        best_idx, best_distance = None, float("inf")
        for idx, marker in enumerate(self._data_markers()):
            distance = self._marker_display_distance(event, marker)
            if distance is not None and distance < best_distance:
                best_idx, best_distance = idx, distance
        return best_idx, best_distance

    def _marker_hit(self, event, max_distance_px: float = 20.0) -> int | None:
        if event.inaxes is None or event.x is None or event.y is None:
            return None
        # Ask the rendered marker artists first.  Matplotlib's own hit test is
        # more reliable than hand-calculated transforms on HiDPI displays and
        # after deferred canvas resizes.
        for artist in reversed(self._live_marker_artists):
            try:
                if artist.contains(event)[0]:
                    gid = str(getattr(artist, "get_gid", lambda: "")() or "")
                    if gid.startswith("graphvis-data-marker-live-"):
                        return int(gid.rsplit("-", 1)[-1])
            except Exception:
                continue
        idx, distance = self._nearest_marker_index(event)
        return idx if idx is not None and distance <= float(max_distance_px) else None

    def _refresh_permanent_markers_live(self, *, draw: bool = True) -> None:
        for artist in self._live_marker_artists:
            try: artist.remove()
            except Exception: pass
        self._live_marker_artists = []
        if self.current_plot_widget is None or not self.current_plot_widget.figure.axes:
            return
        axes = self.current_plot_widget.figure.axes
        for idx, marker in enumerate(self._data_markers()):
            try:
                axis_index = int(marker.get("axis_index", 0) or 0)
                ax = axes[axis_index] if 0 <= axis_index < len(axes) else axes[0]
                x, y, z = float(marker["x"]), float(marker["y"]), marker.get("z")
                if hasattr(ax, "get_zlim3d") and z is not None:
                    artist = ax.scatter([x], [y], [float(z)], s=72, facecolors="none", edgecolors="#E53935", linewidths=2.0, zorder=31)
                else:
                    artist, = ax.plot([x], [y], marker="o", markersize=9, markerfacecolor="none",
                                      markeredgecolor="#E53935", markeredgewidth=2.0, linestyle="none", zorder=31)
                try: artist.set_gid(f"graphvis-data-marker-live-{idx}")
                except Exception: pass
                self._live_marker_artists.append(artist)
            except Exception:
                continue
        if draw and self.current_plot_widget is not None:
            self.current_plot_widget.draw_idle()

    def _toggle_permanent_marker(self, event) -> None:
        existing = self._marker_hit(event, max_distance_px=30.0)
        markers = self._data_markers()
        # The single-click preceding this double-click drew an inspector circle
        # at the same spot; clear it so add/remove never leaves a twin behind.
        self._clear_inspector_marker()
        if existing is not None:
            markers.pop(existing)
            self._refresh_permanent_markers_live()
            self.spec_changed.emit(self.spec)
            self.status_message.emit("Permanent marker removed")
            return
        point = self._cursor_axis_point(event)
        if point is None:
            return
        markers.append({"dataset": point["dataset"], "x": point["x"], "y": point["y"],
                        "z": point.get("z"), "axis_index": int(point.get("axis_index", 0) or 0)})
        self._refresh_permanent_markers_live()
        self.spec_changed.emit(self.spec)
        if point.get("z") is None:
            self.status_message.emit(
                f"Marker locked at cursor: X={point['x']:.12g}, Y={point['y']:.12g} — double-click to remove")
        else:
            self.status_message.emit("3-D marker locked to nearest displayed point — double-click it to remove")

    def _save_data_marker(self, idx: int) -> None:
        markers = self._data_markers()
        if idx < 0 or idx >= len(markers):
            return
        marker = dict(markers[idx])
        path, selected = QFileDialog.getSaveFileName(self, "Save marker", "graphvis_marker.json", "JSON (*.json);;CSV (*.csv)")
        if not path:
            return
        try:
            if path.lower().endswith(".csv") or "CSV" in selected:
                pd.DataFrame([marker]).to_csv(path, index=False)
            else:
                import json
                if not path.lower().endswith(".json"):
                    path += ".json"
                with open(path, "w", encoding="utf-8") as fh:
                    json.dump(marker, fh, indent=2, ensure_ascii=False)
            self.status_message.emit(f"Marker saved: {path}")
        except Exception as exc:
            QMessageBox.warning(self, "Save marker", str(exc))

    def _delete_data_marker(self, idx: int) -> None:
        markers = self._data_markers()
        if 0 <= idx < len(markers):
            markers.pop(idx)
            self._clear_inspector_marker()
            self._refresh_permanent_markers_live()
            self.spec_changed.emit(self.spec)
            self.status_message.emit("Permanent marker removed")

    def _delete_all_data_markers(self) -> None:
        markers = self._data_markers()
        self._clear_inspector_marker(redraw=True)
        if not markers:
            return
        markers.clear()
        self._refresh_permanent_markers_live()
        self.spec_changed.emit(self.spec)
        self.status_message.emit("All permanent markers removed")

    def _marker_context_menu(self, idx: int) -> None:
        menu = QMenu(self)
        a_delete = menu.addAction("Delete marker")
        a_delete_all = menu.addAction("Delete all markers")
        a_save = menu.addAction("Save marker")
        chosen = menu.exec(QCursor.pos())
        if chosen == a_delete:
            self._delete_data_marker(idx)
        elif chosen == a_delete_all:
            self._delete_all_data_markers()
        elif chosen == a_save:
            self._save_data_marker(idx)

    def _edit_canvas_text_if_hit(self, event) -> bool:
        if event.inaxes is None:
            return False
        ax = event.inaxes
        candidates = [(ax.title, "Main Title"), (ax.xaxis.label, "X-Axis Label"), (ax.yaxis.label, "Y-Axis Label")]
        if hasattr(ax, "zaxis"):
            candidates.append((ax.zaxis.label, "Z-Axis Label"))
        for artist, key in candidates:
            try:
                if artist.get_visible() and artist.contains(event)[0]:
                    self._start_inline_text_editor(artist, key, event)
                    return True
            except Exception:
                continue
        return False

    @staticmethod
    def _line_hit(ax, event):
        for line in reversed(list(ax.lines)):
            try:
                if line.get_visible() and line.contains(event)[0]:
                    return line
            except Exception:
                pass
        return None

    def _style_line_menu(self, line):
        menu = QMenu(self)
        a_width = menu.addAction("Line thickness…")
        a_colour = menu.addAction("Line colour…")
        a_marker = menu.addAction("Marker…")
        action = menu.exec(QCursor.pos())
        if action == a_width:
            val, ok = QInputDialog.getDouble(self, "Line thickness", "Width [pt]:", float(line.get_linewidth()), 0.2, 12.0, 1)
            if ok:
                self.spec.line_width = val; self.spin_line_width.setValue(val); line.set_linewidth(val)
        elif action == a_colour:
            c = QColorDialog.getColor(parent=self, title="Curve colour")
            if c.isValid():
                self.spec.series_color = c.name(); line.set_color(c.name())
        elif action == a_marker:
            marker, ok = QInputDialog.getItem(self, "Curve marker", "Marker:", ["None","o","s","^","D","v","x","+",".","*"], 0, False)
            if ok:
                self.spec.marker = "" if marker == "None" else marker
                self.cb_marker.setCurrentText(marker); line.set_marker(None if marker == "None" else marker)
        if action is not None:
            self.current_plot_widget.draw_idle(); self.spec_changed.emit(self.spec)

    def _show_performance_inspector(self) -> None:
        if self.last_result is None or not isinstance(self.last_result.summary, dict):
            QMessageBox.information(self, "Performance inspector", "Render a graph first.")
            return
        summary = self.last_result.summary
        perf = dict(summary.get("performance") or {})
        lines = [f"Chart: {summary.get('chart', self.spec.chart_type)}",
                 f"Total render: {summary.get('render_seconds', summary.get('seconds', '—'))} s",
                 f"Points/cells: {summary.get('n_points', 0):,}"]
        pre = perf.get("surface_precompute")
        if isinstance(pre, dict):
            lines.append(f"Surface precompute: {pre.get('seconds', 0)} s ({pre.get('surfaces', 0)} cached surface(s))")
        for key, value in perf.items():
            if key in {"surface_precompute", "surface_cache_bytes"}: continue
            if isinstance(value, (int, float)):
                lines.append(f"{key.replace('_', ' ').title()}: {float(value):.4f} s")
        cache_bytes = perf.get("surface_cache_bytes")
        if isinstance(cache_bytes, (int, float)):
            lines.append(f"Surface cache: {float(cache_bytes)/(1024*1024):.1f} MiB")
        diagnostics = summary.get("surface_diagnostics") or []
        if diagnostics:
            d = diagnostics[-1]
            lines.append(f"Surface: {d.get('method', '—')} {d.get('shape', '')}; finite={d.get('finite_cells', 0):,}; masked={d.get('masked_cells', 0):,}; imputed={d.get('imputed_cells', 0):,}")
        notes = summary.get("notes") or []
        if notes:
            lines.append("\nRecent pipeline notes:")
            lines.extend(f"• {n}" for n in notes[-8:])
        QMessageBox.information(self, "GraphVis Performance Inspector", "\n".join(lines))

    def _toggle_cross_section_mode(self, on: bool) -> None:
        self._slice_mode = bool(on)
        if self._slice_mode:
            self.status_message.emit("Cross-section mode active — click a 2-D surface/heatmap")
        else:
            self.status_message.emit("Cross-section mode off")

    def _update_cross_section(self, event) -> bool:
        if event.inaxes is None or event.xdata is None or event.ydata is None:
            return False
        payload = getattr(event.inaxes, "_graphvis_surface_payload", None)
        if not isinstance(payload, dict):
            return False
        try:
            if self._slice_dialog is None:
                self._slice_dialog = CrossSectionDialog(self)
            self._slice_dialog.update_slice(payload, float(event.xdata), float(event.ydata))
            self._slice_dialog.show(); self._slice_dialog.raise_()
            return True
        except Exception as exc:
            self.status_message.emit(f"Cross-section unavailable: {exc}")
            return False

    def _on_canvas_click(self, event):
        hit = self._annotation_hit(event) if event.inaxes is not None else None
        if event.button == 1:
            if self._slice_mode and self._update_cross_section(event):
                self._inspect_point(event)
                return
            if bool(getattr(event, "dblclick", False)) and event.inaxes is not None:
                self._toggle_permanent_marker(event)
                return
            if hit is not None:
                self._start_annotation_drag(hit[1], event)
                return
            if self._edit_canvas_text_if_hit(event):
                return
            self._inspect_point(event)
            return
        if event.button != 3:
            return

        # Every graph right-click exposes marker deletion.  Previously a line
        # hit could steal the context menu before marker handling, which is why
        # a visible red marker sometimes produced only Line thickness/colour.
        marker_hit = self._marker_hit(event, max_distance_px=22.0) if event.inaxes is not None else None
        nearest_marker, marker_distance = self._nearest_marker_index(event) if event.inaxes is not None else (None, float("inf"))
        target_marker = marker_hit if marker_hit is not None else nearest_marker
        markers = self._data_markers()
        line = self._line_hit(event.inaxes, event) if event.inaxes is not None else None

        menu = QMenu(self)
        act_delete_marker = menu.addAction("Delete marker")
        act_delete_marker.setEnabled(bool(markers) and target_marker is not None)
        if marker_hit is None and target_marker is not None and np.isfinite(marker_distance):
            act_delete_marker.setToolTip(f"Deletes the marker nearest this click ({marker_distance:.1f} px away)")
        act_delete_all_markers = menu.addAction("Delete all markers")
        act_delete_all_markers.setEnabled(bool(markers))
        act_save_marker = menu.addAction("Save marker…")
        act_save_marker.setEnabled(bool(markers) and target_marker is not None)
        menu.addSeparator()

        act_snap_nearest = menu.addAction("Show nearest point on click")
        act_snap_nearest.setCheckable(True)
        act_snap_nearest.setChecked(self._snap_nearest_enabled())
        act_snap_nearest.setToolTip("When ticked, clicking the graph snaps to the closest data point and shows its "
                                    "exact coordinates and source dataset in a tooltip.")
        menu.addSeparator()

        act_annotation_options = None
        if hit is not None:
            act_annotation_options = menu.addAction("Annotation options…")
        act_line_style = None
        if line is not None:
            act_line_style = menu.addAction("Line style…")

        annotation_menu = menu.addMenu("Add annotation")
        ann_actions = {}
        for label, kind in (("Text", "text"), ("Callout", "callout"), ("Arrow", "arrow"), ("Bracket", "bracket"), ("Rectangle", "rectangle"), ("Ellipse", "ellipse")):
            ann_actions[annotation_menu.addAction(label)] = kind
        act_manage = annotation_menu.addAction("Manage annotations…")
        act_reset = menu.addAction("Fit to Area")
        roi_menu = menu.addMenu("ROI gadget")
        act_roi = roi_menu.addAction("Toggle interactive ROI")
        act_roi_apply = roi_menu.addAction("Apply current ROI as filter")
        act_roi_export = roi_menu.addAction("Export current ROI rows…")
        menu.addSeparator()
        x_scale = menu.addMenu("X-axis scale")
        y_scale = menu.addMenu("Y-axis scale")
        z_scale = menu.addMenu("Z / response scale")
        x_actions = {x_scale.addAction(label): label for label in AXIS_SCALE_OPTIONS}
        y_actions = {y_scale.addAction(label): label for label in AXIS_SCALE_OPTIONS}
        z_actions = {z_scale.addAction(label): label for label in AXIS_SCALE_OPTIONS}
        for actions, current in ((x_actions, self.spec.x_scale), (y_actions, self.spec.y_scale), (z_actions, self.spec.z_scale)):
            for act, label in actions.items():
                act.setCheckable(True)
                act.setChecked(label == canonical_axis_scale(current))
        menu.addSeparator()
        act_copy = menu.addAction("Copy figure to clipboard")
        act_export = menu.addAction("Export…")
        action = menu.exec(QCursor.pos())
        if action == act_snap_nearest:
            enabled = act_snap_nearest.isChecked()
            app_settings().setValue("ui/snap_nearest_point", bool(enabled))
            if enabled:
                # Immediately demonstrate the feature on the point just clicked.
                self._inspect_point(event)
                self.status_message.emit("Nearest-point snapping enabled — clicks now lock onto the closest data point.")
            else:
                self.status_message.emit("Nearest-point snapping disabled — clicks report exact cursor coordinates.")
        elif action == act_delete_marker and target_marker is not None:
            self._delete_data_marker(target_marker)
        elif action == act_delete_all_markers:
            self._delete_all_data_markers()
        elif action == act_save_marker and target_marker is not None:
            self._save_data_marker(target_marker)
        elif act_annotation_options is not None and action == act_annotation_options and hit is not None:
            self._annotation_context_menu(hit[1])
        elif act_line_style is not None and action == act_line_style and line is not None:
            self._style_line_menu(line)
        elif action in ann_actions and event.inaxes is not None and event.xdata is not None:
            kind = ann_actions[action]
            text = ""
            if kind in ("text", "callout"):
                text, ok = QInputDialog.getText(self, "Annotate canvas", "Text (MathText/LaTeX supported):")
                if not ok: return
            self._add_annotation_at(text, event.xdata, event.ydata, "data", kind=kind)
        elif action == act_manage:
            self.open_annotation_manager()
        elif action == act_reset:
            self._reset_view()
        elif action == act_roi:
            self.toggle_roi_selector()
        elif action == act_roi_apply:
            self.apply_roi_filter()
        elif action == act_roi_export:
            self.export_roi_rows()
        elif action in x_actions:
            self.spec.x_scale = x_actions[action]
            self.spec_changed.emit(self.spec)
            self.request_render()
        elif action in y_actions:
            self.spec.y_scale = y_actions[action]
            self.spec_changed.emit(self.spec)
            self.request_render()
        elif action in z_actions:
            self.spec.z_scale = z_actions[action]
            self.spec_changed.emit(self.spec)
            self.request_render()
        elif action == act_copy and self.current_plot_widget is not None:
            QGuiApplication.clipboard().setPixmap(self.current_plot_widget.grab())
            self.status_message.emit("Figure copied to clipboard")
        elif action == act_export:
            self.export_figure()

    # ------------------------------------------------------- gradient
    def _focus_gradient_region(self):
        if self.current_plot_widget is None or not self.current_plot_widget.figure.axes:
            return
        ax = self.current_plot_widget.figure.axes[0]
        xmap, ymap = self.spec.mappings.get('x'), self.spec.mappings.get('y')
        zmap = self.spec.mappings.get('gradient') or self.spec.mappings.get('z')
        # Prefer a spatial 2-D gradient when X/Y/Z are available.
        if xmap and ymap and zmap:
            try:
                from scipy.ndimage import gaussian_filter
                from scipy.stats import binned_statistic_2d
                for ds in self.spec.datasets.values():
                    if all(c in ds.df.columns for c in (xmap, ymap, zmap)) and len({xmap,ymap,zmap}) == 3:
                        sub = ds.df[[xmap,ymap,zmap]].replace([np.inf,-np.inf],np.nan).dropna()
                        if len(sub) < 8: continue
                        bins = min(max(24, int(np.sqrt(len(sub)))), 100)
                        stat, xe, ye = binned_statistic_2d(sub[xmap], sub[ymap], sub[zmap], statistic='mean', bins=bins)[:3]
                        fill = float(np.nanmedian(sub[zmap]))
                        Zi = gaussian_filter(np.nan_to_num(stat.T, nan=fill), sigma=max(self.spec.smoothing,0.1))
                        gy,gx = np.gradient(Zi, ye[1]-ye[0], xe[1]-xe[0]); mag=np.hypot(gx,gy)
                        iy,ix=np.unravel_index(int(np.nanargmax(mag)),mag.shape)
                        cx=0.5*(xe[ix]+xe[ix+1]); cy=0.5*(ye[iy]+ye[iy+1])
                        sx=max(0.12*(xe[-1]-xe[0]), xe[1]-xe[0]); sy=max(0.12*(ye[-1]-ye[0]), ye[1]-ye[0])
                        ax.set_xlim(cx-sx,cx+sx); ax.set_ylim(cy-sy,cy+sy)
                        self.current_plot_widget.draw_idle(); self._on_view_changed()
                        self.status_message.emit(f"Focused maximum gradient region near X={cx:.5g}, Y={cy:.5g}")
                        return
            except Exception:
                pass
        # 1-D fallback: focus the steepest local |dY/dX| region.
        for ds in self.spec.datasets.values():
            cx = xmap if xmap in ds.df.columns else (ds.numeric_columns[0] if ds.numeric_columns else None)
            cy = ymap if ymap in ds.df.columns else (ds.numeric_columns[1] if len(ds.numeric_columns)>1 else None)
            if not cx or not cy: continue
            sub=ds.df[[cx,cy]].replace([np.inf,-np.inf],np.nan).dropna().sort_values(cx)
            if len(sub)<5: continue
            x=sub[cx].to_numpy(float); y=sub[cy].to_numpy(float)
            dx=np.gradient(x); dx[np.abs(dx)<np.finfo(float).eps]=np.nan
            grad=np.abs(np.gradient(y)/dx); k=int(np.nanargmax(grad))
            span=max(np.ptp(x)*0.12, np.finfo(float).eps); lo=x[k]-span; hi=x[k]+span
            mask=(x>=lo)&(x<=hi); yy=y[mask] if mask.any() else y
            yr=np.ptp(yy); pad=0.08*(yr if yr>0 else max(abs(float(np.nanmean(yy))),1.0))
            ax.set_xlim(lo,hi); ax.set_ylim(float(np.nanmin(yy)-pad),float(np.nanmax(yy)+pad))
            self.current_plot_widget.draw_idle(); self._on_view_changed()
            self.status_message.emit(f"Focused steepest 1-D gradient near {get_pretty_label(cx)}={x[k]:.5g}")
            return
        QMessageBox.information(self, "Find the Gradient", "No suitable numeric X/Y (or X/Y/Z) mapping is available.")

    def _compute_and_display_gradient(self):
        from scipy.ndimage import gaussian_filter
        from scipy.stats import binned_statistic_2d
        x, y = self.spec.mappings.get('x'), self.spec.mappings.get('y')
        z = self.spec.mappings.get('gradient') or self.spec.mappings.get('z')
        for name, ds in self.spec.datasets.items():
            cols = list(ds.df.columns)
            if len(cols) >= 3:
                cx = x if x in cols else cols[0]
                cy = y if y in cols else cols[1]
                cz = z if z in cols else cols[2]
                if len({cx, cy, cz}) < 3:
                    continue
                sub = ds.df[[cx, cy, cz]].replace([np.inf, -np.inf], np.nan).dropna()
                if sub.empty:
                    continue
                stat, x_edge, y_edge = binned_statistic_2d(sub[cx].to_numpy(float), sub[cy].to_numpy(float),
                                                           sub[cz].to_numpy(float), statistic='mean',
                                                           bins=min(self.spec.grid_resolution, 120))[:3]
                Zi = np.nan_to_num(stat.T, nan=float(np.nanmean(sub[cz])))
                Zi = gaussian_filter(Zi, sigma=max(self.spec.smoothing, 0.1))
                gy, gx = np.gradient(Zi, y_edge[1] - y_edge[0], x_edge[1] - x_edge[0])
                mag = np.hypot(gx, gy)
                iy, ix = np.unravel_index(np.argmax(mag), mag.shape)
                QMessageBox.information(self, "Spatial gradient analysis",
                                        f"Dataset: {clean_series_name(name)}\nField: {get_pretty_label(cz)}\n\n"
                                        f"• Max |∇| = {float(mag.max()):.4g} at ({get_pretty_label(cx)} ≈ {0.5*(x_edge[ix]+x_edge[ix+1]):.4g}, "
                                        f"{get_pretty_label(cy)} ≈ {0.5*(y_edge[iy]+y_edge[iy+1]):.4g})\n"
                                        f"• Mean |∇| = {float(mag.mean()):.4g}\n"
                                        f"• Mean ∂/∂x = {float(gx.mean()):.4g}, mean ∂/∂y = {float(gy.mean()):.4g}\n"
                                        f"• Grid {Zi.shape[1]}×{Zi.shape[0]}, σ = {self.spec.smoothing:.2f}")
                return
        QMessageBox.warning(self, "Gradient", "Select a dataset with three distinct X, Y and Z (or gradient field) mappings first.")

    # ------------------------------------------------------- export
    def export_figure(self):
        s = app_settings()
        default_dpi = int(getattr(self.spec, "export_dpi", 0) or s.value("export/dpi", 300, int))
        dlg = ExportDialog(self.spec.figsize, s.value("export/format", "PNG", str), default_dpi,
                           s.value("export/dir", "", str), self)
        if getattr(self.spec, "export_color_mode", "RGB") == "CMYK":
            dlg.cb_color_mode.setCurrentText("CMYK (TIFF/JPG)")
        if getattr(self.spec, "export_icc_profile", ""):
            dlg.txt_icc.setText(self.spec.export_icc_profile)
        if hasattr(dlg, "cb_vector_surface"):
            dlg.cb_vector_surface.setCurrentText(getattr(self.spec, "vector_surface_mode", "Hybrid (vector text + raster field)"))
        if not dlg.exec():
            return
        opts = dlg.options()
        if not opts["path"]:
            return
        spec = self.spec.clone()
        spec.vector_surface_mode = opts.get("vector_surface_mode", getattr(spec, "vector_surface_mode", "Hybrid (vector text + raster field)"))
        spec.view_state = self.capture_view_state()
        if spec.view_state and spec.view_state.get("legend_loc"):
            spec.legend_loc = tuple(spec.view_state["legend_loc"])
        s.setValue("export/format", opts["format"]); s.setValue("export/dpi", opts["dpi"])
        s.setValue("export/dir", os.path.dirname(opts["path"]))
        self.overlay.start(f"Exporting {opts['format']} at {opts['dpi']} dpi…")
        task = ExportTask(spec, opts["path"], opts["format"], opts["dpi"], opts["transparent"],
                          opts.get("color_mode", "RGB"), opts.get("icc_profile", ""))
        task.signals.progress.connect(lambda msg, pct: self.overlay.set_message(msg, pct))
        task.signals.finished.connect(self._on_export_done)
        task.signals.failed.connect(self._on_export_failed)
        HEAVY_POOL.start(task)

    def _on_export_done(self, info):
        self.overlay.stop()
        self.status_message.emit(f"Exported {info['format']} @ {info['dpi']} dpi → {info['path']} ({info['bytes']/1e6:.1f} MB)")
        for w in info.get("warnings", []):
            log_line(w, "EXPORT")

    def _on_export_failed(self, msg):
        self.overlay.stop()
        log_line(f"Export failed: {msg}", "EXPORT")
        QMessageBox.critical(self, "Export failed", msg.splitlines()[0])

    def export_csv(self):
        from PySide6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getSaveFileName(self, "Export CSV data", "dataset_export.csv", "CSV (*.csv)")
        if not path:
            return
        for ds in self.spec.datasets.values():
            ds.df.to_csv(path, index=False)
            self.status_message.emit(f"Exported dataset CSV to {path}")
            break
