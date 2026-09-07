# =========================================================================
# diagnostics.py — silent crash watchdog, exception hooks, diagnostics drawer
#
# The watchdog runs on its *own* thread (a QTimer on the GUI thread cannot
# fire while the GUI thread is frozen, which is why the old version only
# reported freezes after they were over — and popped the drawer open at
# startup). It never opens UI on its own; it only appends to
# graphvis.log and emits a signal the main window uses to update a
# status-bar indicator. The drawer is opened manually from Settings.
# =========================================================================
from __future__ import annotations

import datetime as _dt
import os
import sys
import threading
import time
import traceback

from PySide6.QtCore import QObject, QSettings, QTimer, QUrl, Qt, Signal, qInstallMessageHandler
from PySide6.QtGui import QCursor, QDesktopServices, QGuiApplication
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox, QDoubleSpinBox, QFormLayout,
                               QHBoxLayout, QLabel, QLineEdit, QPushButton, QTextEdit, QVBoxLayout, QWidget, QSpinBox)

APP_DIR = os.path.dirname(os.path.abspath(__file__))
from graphvis.core.logging import LOG_FILE as _PERSISTENT_LOG_FILE, get_logger
from graphvis.core.paths import DEBUG_LOG_DIR, ERROR_LOG_DIR
from graphvis.core.session_diagnostics import record_exception, record_activity
LOG_FILE = str(_PERSISTENT_LOG_FILE)

_log_lock = threading.Lock()


def log_line(text: str, tag: str = "INFO") -> str:
    stamp = _dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{stamp}] [{tag}] {text}"
    level = str(tag).upper()
    logger = get_logger("diagnostics")
    if level in ("CRASH", "ERROR", "FATAL", "CRITICAL"):
        logger.error("[%s] %s", tag, text)
    elif level in ("WARN", "WARNING", "WATCHDOG"):
        logger.warning("[%s] %s", tag, text)
    else:
        logger.info("[%s] %s", tag, text)
    return line


def read_log_tail(max_chars: int = 60000) -> str:
    try:
        with open(LOG_FILE, "r", encoding="utf-8", errors="ignore") as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            f.seek(max(size - max_chars, 0))
            return f.read()
    except FileNotFoundError:
        return "(graphvis.log does not exist yet — nothing has been logged)"
    except Exception as exc:
        return f"(could not read log: {exc})"


def install_exception_hooks():
    def handle_exception(exc_type, exc_value, exc_traceback):
        if issubclass(exc_type, KeyboardInterrupt):
            sys.__excepthook__(exc_type, exc_value, exc_traceback)
            return
        tb_str = "".join(traceback.format_exception(exc_type, exc_value, exc_traceback))
        log_line(f"Uncaught exception on thread {threading.current_thread().name}:\n{tb_str}", "CRASH")
        record_exception(f"Uncaught exception on {threading.current_thread().name}", traceback_text=tb_str, fatal=True)
        sys.__excepthook__(exc_type, exc_value, exc_traceback)
    sys.excepthook = handle_exception

    def thread_hook(args):
        tb_str = "".join(traceback.format_exception(args.exc_type, args.exc_value, args.exc_traceback))
        log_line(f"Uncaught exception in worker thread {getattr(args.thread, 'name', '?')}:\n{tb_str}", "CRASH")
        record_exception(f"Uncaught worker exception on {getattr(args.thread, 'name', '?')}", traceback_text=tb_str, fatal=True)
    threading.excepthook = thread_hook

    def qt_handler(mode, context, message):
        try:
            name = str(mode).split(".")[-1]
        except Exception:
            name = "QtMsg"
        if "Warning" in name or "Critical" in name or "Fatal" in name:
            log_line(message, name.replace("Msg", "").upper())
    try:
        qInstallMessageHandler(qt_handler)
    except Exception:
        pass


def safe_window_geometry(window, default_w=1680, default_h=1000):
    cursor_pos = QCursor.pos()
    screen = QGuiApplication.screenAt(cursor_pos) or QGuiApplication.primaryScreen()
    geom = screen.availableGeometry()
    w, h = min(default_w, geom.width() - 40), min(default_h, geom.height() - 60)
    x = geom.x() + max(0, (geom.width() - w) // 2)
    y = geom.y() + max(0, (geom.height() - h) // 2)
    window.setGeometry(x, y, w, h)


# ---------------------------------------------------------------- watchdog
class FreezeWatchdog(QObject):
    """Background heartbeat monitor.

    * The GUI thread bumps a timestamp every 200 ms via a QTimer.
    * A daemon thread checks that timestamp 4× a second. When the GUI thread
      has been silent for longer than `threshold` seconds it writes a
      diagnostic block (duration + the GUI thread's current Python stack) to
      graphvis.log and emits `frozen_detected`. When the heartbeat
      resumes it logs the recovery and emits `recovered`.
    * Nothing here shows a window. Ever.
    """

    frozen_detected = Signal(str)
    recovered = Signal(str)

    def __init__(self, threshold_sec: float = 3.0, parent=None):
        super().__init__(parent)
        self.threshold = float(threshold_sec)
        self._last_beat = time.monotonic()
        self._main_ident = threading.main_thread().ident
        self._stop = threading.Event()
        self._enabled = True
        self._reported = False
        self._frozen_since: float | None = None
        self._beat_timer = QTimer(self)
        self._beat_timer.setInterval(200)
        self._beat_timer.setTimerType(Qt.PreciseTimer)
        self._beat_timer.timeout.connect(self.pulse)
        self._thread: threading.Thread | None = None
        self.freeze_count = 0

    # -- control
    def start(self):
        self.pulse()
        self._beat_timer.start()
        if self._thread is None or not self._thread.is_alive():
            self._stop.clear()
            self._thread = threading.Thread(target=self._loop, name="FreezeWatchdog", daemon=True)
            self._thread.start()

    def stop(self):
        self._stop.set()
        self._beat_timer.stop()

    def set_enabled(self, on: bool):
        self._enabled = bool(on)
        self.pulse()

    def set_threshold(self, sec: float):
        self.threshold = max(0.5, float(sec))

    def pulse(self):
        self._last_beat = time.monotonic()

    # -- monitor thread
    def _loop(self):
        while not self._stop.is_set():
            time.sleep(0.25)
            if not self._enabled:
                continue
            gap = time.monotonic() - self._last_beat
            if gap > self.threshold and not self._reported:
                self._reported = True
                self._frozen_since = self._last_beat
                self.freeze_count += 1
                stack = self._main_stack()
                msg = (f"GUI thread unresponsive for {gap:.1f}s (threshold {self.threshold:.1f}s). "
                       f"Main-thread stack at detection:\n{stack}")
                log_line(msg, "WATCHDOG")
                record_activity("GUI freeze detected", f"stall={gap:.1f}s threshold={self.threshold:.1f}s")
                self.frozen_detected.emit(f"Freeze #{self.freeze_count}: GUI thread stalled {gap:.1f}s — see graphvis.log")
            elif gap <= self.threshold and self._reported:
                self._reported = False
                total = time.monotonic() - (self._frozen_since or time.monotonic())
                log_line(f"GUI thread recovered after ≈{total:.1f}s stall.", "WATCHDOG")
                record_activity("GUI recovered", f"stall={total:.1f}s")
                self.recovered.emit(f"Recovered after ≈{total:.1f}s stall")

    def _main_stack(self) -> str:
        try:
            frame = sys._current_frames().get(self._main_ident)
            if frame is None:
                return "(main thread frame unavailable)"
            return "".join(traceback.format_stack(frame, limit=25))
        except Exception as exc:
            return f"(stack capture failed: {exc})"


# ---------------------------------------------------------------- drawer
class DebugDrawer(QWidget):
    """Diagnostics panel: live log view + buttons to open/reload the log file."""

    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        bar = QHBoxLayout()
        self.lbl = QLabel(f"<b>Diagnostics</b> — {os.path.basename(LOG_FILE)}")
        bar.addWidget(self.lbl)
        bar.addStretch(1)
        b_reload = QPushButton("Reload log")
        b_reload.clicked.connect(self.reload)
        b_open = QPushButton("Open graphvis.log")
        b_open.clicked.connect(self.open_log_file)
        b_debug = QPushButton("Debug folder")
        b_debug.clicked.connect(self.open_debug_folder)
        b_errors = QPushButton("Errors folder")
        b_errors.clicked.connect(self.open_errors_folder)
        b_clear = QPushButton("Clear view")
        b_clear.clicked.connect(lambda: self.console.clear())
        for b in (b_reload, b_open, b_debug, b_errors, b_clear):
            bar.addWidget(b)
        lay.addLayout(bar)
        self.console = QTextEdit()
        self.console.setReadOnly(True)
        self.console.setStyleSheet("background:#1E1E1E;color:#D4D4D4;font-family:Consolas,monospace;font-size:10pt;")
        lay.addWidget(self.console)

    def log(self, text: str, tag: str = "INFO"):
        self.console.append(log_line(text, tag))

    def reload(self):
        self.console.setPlainText(read_log_tail())
        self.console.moveCursor(self.console.textCursor().MoveOperation.End)

    def open_log_file(self):
        if not os.path.exists(LOG_FILE):
            log_line("Log file created from diagnostics drawer.", "INFO")
        QDesktopServices.openUrl(QUrl.fromLocalFile(LOG_FILE))

    def open_debug_folder(self):
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(DEBUG_LOG_DIR)))

    def open_errors_folder(self):
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(ERROR_LOG_DIR)))


# ---------------------------------------------------------------- settings
SETTINGS_ORG, SETTINGS_APP = "GraphVis", "GraphVis"


def app_settings() -> QSettings:
    return QSettings(SETTINGS_ORG, SETTINGS_APP)


class SettingsDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Settings & Diagnostics")
        self.setMinimumWidth(460)
        s = app_settings()
        form = QFormLayout(self)

        self.chk_watchdog = QCheckBox("Run silent freeze watchdog in the background")
        self.chk_watchdog.setChecked(s.value("watchdog/enabled", True, bool))
        form.addRow(self.chk_watchdog)

        self.spin_threshold = QDoubleSpinBox()
        self.spin_threshold.setRange(1.0, 30.0)
        self.spin_threshold.setSingleStep(0.5)
        self.spin_threshold.setSuffix(" s")
        self.spin_threshold.setValue(s.value("watchdog/threshold", 3.0, float))
        form.addRow("Freeze threshold (log only):", self.spin_threshold)

        self.chk_drawer = QCheckBox("Show diagnostics drawer under the canvas")
        self.chk_drawer.setChecked(s.value("ui/show_diagnostics", False, bool))
        form.addRow(self.chk_drawer)

        self.lbl_startup = QLabel("GraphVis starts with an empty canvas. Saved graph tabs can be restored manually from File.")
        self.lbl_startup.setWordWrap(True)
        form.addRow("Startup:", self.lbl_startup)

        self.chk_fast_preview = QCheckBox("Fast interactive preview (full quality is preserved for export)")
        self.chk_fast_preview.setChecked(s.value("render/fast_preview", True, bool))
        form.addRow(self.chk_fast_preview)

        self.cb_low_power = QComboBox()
        self.cb_low_power.addItems(["Auto (detect hardware)", "On", "Off"])
        stored_low_power = str(s.value("render/low_power_mode", "Auto") or "Auto")
        self.cb_low_power.setCurrentText({"On": "On", "Off": "Off"}.get(stored_low_power, "Auto (detect hardware)"))
        self.cb_low_power.setToolTip(
            "Low-Power Mode reduces rendering overhead for weaker computers: lower interactive DPI (≤72), smaller "
            "point/grid budgets, halved fast-preview threshold, slower mascot animation, and aggressive 3-D "
            "detail reduction while dragging. Auto enables it on machines with ≤4 cores or <7.5 GB RAM. "
            "Export quality is never affected.")
        form.addRow("Low-Power Mode:", self.cb_low_power)

        self.spin_preview_points = QSpinBox()
        self.spin_preview_points.setRange(3000, 20000); self.spin_preview_points.setSingleStep(1000)
        self.spin_preview_points.setValue(s.value("render/interactive_max_points", 10000, int))
        form.addRow("Interactive point budget:", self.spin_preview_points)

        self.cb_preview_dpi = QComboBox(); self.cb_preview_dpi.addItems(["72", "84", "96", "110", "120"])
        self.cb_preview_dpi.setCurrentText(str(s.value("render/interactive_dpi", 84, int)))
        form.addRow("Interactive render DPI:", self.cb_preview_dpi)

        self.chk_disk_cache = QCheckBox("Enable cross-session Matplotlib figure cache (uses more disk/I/O)")
        self.chk_disk_cache.setChecked(s.value("cache/figures", False, bool))
        form.addRow(self.chk_disk_cache)

        self.chk_touch = QCheckBox("Enable touch / tablet gestures on plot canvases")
        self.chk_touch.setChecked(s.value("ui/touch_gestures", True, bool))
        form.addRow(self.chk_touch)

        self.cb_apply_mode = QComboBox()
        self.cb_apply_mode.addItems(["Adaptive (reactive; heavy renders wait for Apply)",
                                     "Manual (▶ Apply buttons for everything)",
                                     "Instant (debounced auto-apply for everything)"])
        stored_mode = str(s.value("ui/dropdown_apply_mode", "Adaptive")).lower()
        self.cb_apply_mode.setCurrentIndex({"manual": 1, "instant": 2}.get(stored_mode, 0))
        self.cb_apply_mode.setToolTip("Adaptive keeps standard tools fully reactive but queues changes behind ▶ Apply "
                                      "for expensive surface/volume/3-D renders so the UI can never hang mid-tweak. "
                                      "Manual queues every dropdown; Instant commits everything after a short debounce.")
        form.addRow("Apply mode:", self.cb_apply_mode)

        self.chk_clamp_pan = QCheckBox("Clamp panning/zooming to the data bounding box")
        self.chk_clamp_pan.setChecked(s.value("ui/clamp_pan", False, bool))
        self.chk_clamp_pan.setToolTip("Unchecked (default): drag the scene freely in any direction across the viewport.")
        form.addRow(self.chk_clamp_pan)

        self.chk_auto_expand = QCheckBox("Auto-expand axes when plotted content exceeds the current bounds")
        self.chk_auto_expand.setChecked(s.value("ui/auto_expand_bounds", True, bool))
        form.addRow(self.chk_auto_expand)

        self.cb_export_fmt = QComboBox()
        self.cb_export_fmt.addItems(["PNG", "JPG", "TIFF", "PDF", "EPS", "SVG"])
        self.cb_export_fmt.setCurrentText(s.value("export/format", "PNG", str))
        form.addRow("Default export format:", self.cb_export_fmt)

        self.txt_export_dpi = QLineEdit(str(max(1, s.value("export/dpi", 300, int))))
        self.txt_export_dpi.setPlaceholderText("Any positive DPI")
        self.txt_export_dpi.setToolTip("No fixed maximum. Very high raster DPI can require substantial RAM, disk space and render time.")
        form.addRow("Default export DPI:", self.txt_export_dpi)

        log_row = QHBoxLayout()
        b_open = QPushButton("Open graphvis.log")
        b_open.clicked.connect(lambda: QDesktopServices.openUrl(QUrl.fromLocalFile(LOG_FILE)))
        b_debug = QPushButton("Debug folder")
        b_debug.clicked.connect(lambda: QDesktopServices.openUrl(QUrl.fromLocalFile(str(DEBUG_LOG_DIR))))
        b_errors = QPushButton("Errors folder")
        b_errors.clicked.connect(lambda: QDesktopServices.openUrl(QUrl.fromLocalFile(str(ERROR_LOG_DIR))))
        log_row.addWidget(b_open); log_row.addWidget(b_debug); log_row.addWidget(b_errors)
        form.addRow("Logs:", log_row)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)

    def save(self):
        s = app_settings()
        s.setValue("watchdog/enabled", self.chk_watchdog.isChecked())
        s.setValue("watchdog/threshold", self.spin_threshold.value())
        s.setValue("ui/show_diagnostics", self.chk_drawer.isChecked())
        s.setValue("render/fast_preview", self.chk_fast_preview.isChecked())
        low_power_text = self.cb_low_power.currentText()
        s.setValue("render/low_power_mode", "On" if low_power_text == "On" else ("Off" if low_power_text == "Off" else "Auto"))
        s.setValue("render/interactive_max_points", self.spin_preview_points.value())
        s.setValue("render/interactive_dpi", int(self.cb_preview_dpi.currentText()))
        s.setValue("cache/figures", self.chk_disk_cache.isChecked())
        s.setValue("ui/touch_gestures", self.chk_touch.isChecked())
        s.setValue("ui/dropdown_apply_mode", {1: "Manual", 2: "Instant"}.get(self.cb_apply_mode.currentIndex(), "Adaptive"))
        s.setValue("ui/clamp_pan", self.chk_clamp_pan.isChecked())
        s.setValue("ui/auto_expand_bounds", self.chk_auto_expand.isChecked())
        s.setValue("export/format", self.cb_export_fmt.currentText())
        try:
            export_dpi = max(1, int(self.txt_export_dpi.text().strip()))
        except Exception:
            export_dpi = 300
        s.setValue("export/dpi", export_dpi)
        s.sync()
