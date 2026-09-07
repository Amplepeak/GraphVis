"""GraphVis application bootstrap.

Dependency installation is intentionally NOT performed here. Windows runtime
creation/update belongs exclusively to update.bat.

The heavy MVC/UI imports are delayed until after QApplication and the startup
splash exist, so users get immediate visual feedback while GraphVis loads.
"""
from __future__ import annotations

import os
import sys
import math
import random
from typing import TYPE_CHECKING, Callable

from PySide6.QtCore import Qt, QTimer, QRectF, QPointF, QSettings
from PySide6.QtGui import QIcon, QPixmap, QPainter, QColor, QPen, QBrush, QFont, QPolygonF
from PySide6.QtWidgets import QApplication, QSplashScreen

from graphvis.core.logging import get_logger
from graphvis.core.paths import ASSETS_DIR

if TYPE_CHECKING:
    from graphvis.controllers.application_controller import GraphVisApplicationController
    from graphvis.models.application_model import GraphVisApplicationModel
    from graphvis.views.main_window import GraphVisWindow

APP_NAME = "GraphVis"
LOGO_FILE = str(ASSETS_DIR / "graphvis_logo.png")
ICON_FILE = str(ASSETS_DIR / "graphvis_icon.png")
LOG = get_logger("bootstrap")


class SwimmingOtterSplash(QSplashScreen):
    """Thirty procedural swimming-otter startup variants.

    One of 30 deterministic scene variants is selected on every launch.  They
    vary swim phase/speed, wave spacing, bubbles and coral layout while keeping
    the GraphVis otter branding consistent and omitting the wordmark.
    """
    def __init__(self, icon_path: str):
        base = QPixmap(560, 360); base.fill(Qt.transparent)
        super().__init__(base)
        # Thirty procedural variants are available and consecutive launches
        # deliberately avoid repeating the same one.  The first draw remains
        # random; only an immediate duplicate is shifted to another variant.
        settings = QSettings(APP_NAME, APP_NAME)
        last_variant = settings.value("startup/last_otter_variant", -1, int)
        self.variant = random.randrange(30)
        if self.variant == last_variant:
            self.variant = (self.variant + random.randrange(1, 30)) % 30
        settings.setValue("startup/last_otter_variant", self.variant)
        rnd = random.Random(1487 + self.variant * 7919)
        self.phase = rnd.uniform(0.0, math.tau)
        self.speed = rnd.uniform(0.055, 0.095)
        self.wave_gap = rnd.randint(24, 38)
        self.coral_seed = [rnd.uniform(0.0, 1.0) for _ in range(12)]
        self.otter_spot_seed = [rnd.uniform(0.0, 1.0) for _ in range(10)]
        self.message = "Starting GraphVis…"
        self.progress = 2
        # Use the exact GraphVis otter icon artwork as the swimmer sprite so
        # the loading scene matches the shortcut/taskbar branding precisely.
        self.otter_pixmap = QPixmap(icon_path)
        self.otter_scale = rnd.uniform(0.86, 1.08)
        self.swim_arc = rnd.uniform(16.0, 32.0)
        self.tilt_arc = rnd.uniform(2.2, 4.2)
        self.setWindowFlag(Qt.WindowStaysOnTopHint, False)
        self.setWindowFlag(Qt.WindowDoesNotAcceptFocus, True)
        self.setAttribute(Qt.WA_ShowWithoutActivating, True)
        self.setWindowFlag(Qt.FramelessWindowHint, True)
        self.setAttribute(Qt.WA_TranslucentBackground, True)
        self.timer = QTimer(self); self.timer.setInterval(40); self.timer.timeout.connect(self._tick); self.timer.start()

    def _tick(self):
        self.phase = (self.phase + self.speed) % math.tau
        self.update()

    def set_stage(self, message: str, pct: int):
        self.message = str(message); self.progress = max(0, min(100, int(pct))); self.update()

    def _draw_swimming_otter(self, p: QPainter, cx: float, cy: float, scale: float, tilt: float) -> None:
        """Draw the exact GraphVis icon artwork as a swimming sprite."""
        if self.otter_pixmap.isNull():
            return
        p.save()
        p.translate(cx, cy)
        p.rotate(tilt)
        sprite = self.otter_pixmap
        tw = max(108.0, min(188.0, sprite.width() * scale * 0.27 * self.otter_scale))
        th = tw * sprite.height() / max(1, sprite.width())
        target = QRectF(-tw / 2.0, -th / 2.0, tw, th)
        p.drawPixmap(target, sprite, QRectF(0.0, 0.0, float(sprite.width()), float(sprite.height())))
        p.restore()

    def drawContents(self, p: QPainter) -> None:
        p.setRenderHint(QPainter.Antialiasing)
        r = self.rect()
        # Intentionally do not paint a window background: the splash remains
        # visually transparent so the desktop or other windows stay visible
        # behind the swimmer scene.
        floor_y = r.height() - 106
        # Coral bed
        sand = QColor(10, 36, 44, 72)
        p.setPen(Qt.NoPen); p.setBrush(sand)
        p.drawRoundedRect(QRectF(20, floor_y + 34, r.width() - 40, 42), 18, 18)
        coral_cols = [QColor(232,106,119,210), QColor(243,156,90,210), QColor(190,107,214,200), QColor(240,201,90,208), QColor(96,208,183,205)]
        for i in range(10):
            x = 26 + i * 49 + self.coral_seed[i % len(self.coral_seed)] * 18
            h = 18 + self.coral_seed[(i+3)%len(self.coral_seed)] * 30
            col = coral_cols[(i + self.variant) % len(coral_cols)]
            p.setPen(QPen(col, 4.5, Qt.SolidLine, Qt.RoundCap))
            p.drawLine(QPointF(x, floor_y+42), QPointF(x, floor_y+42-h))
            p.drawLine(QPointF(x, floor_y+30-h*0.35), QPointF(x-9, floor_y+18-h*0.60))
            p.drawLine(QPointF(x, floor_y+26-h*0.22), QPointF(x+10, floor_y+14-h*0.52))
        # Small animated fish and bubbles around the mascot.
        fish_cols = [QColor(81, 175, 243, 215), QColor(255, 206, 84, 210), QColor(149, 111, 228, 210), QColor(113, 231, 214, 210)]
        for i in range(7):
            lane = 96 + i * 18
            phase = self.phase * (0.8 + 0.05 * i) + (self.variant + i) * 0.41
            fx = 38 + ((phase * 92 + i * 67) % (r.width() + 90)) - 45
            fy = lane + math.sin(phase * 1.7) * (6 + (i % 3) * 2)
            size = 7 + (i % 3) * 2
            body = fish_cols[(i + self.variant) % len(fish_cols)]
            p.setPen(QPen(QColor(8, 38, 56, 165), 1.2))
            p.setBrush(body)
            p.drawEllipse(QRectF(fx - size * 0.72, fy - size * 0.42, size * 1.15, size * 0.84))
            p.drawPolygon(QPolygonF([QPointF(fx - size * 0.95, fy), QPointF(fx - size * 1.45, fy - size * 0.45), QPointF(fx - size * 1.45, fy + size * 0.45)]))
            p.setPen(Qt.NoPen); p.setBrush(QColor(255,255,255,180))
            p.drawEllipse(QPointF(fx + size * 0.15, fy - size * 0.08), 0.9, 0.9)
        for i in range(10):
            bx = 35 + ((i*57 + self.variant*17) % max(60, r.width()-70))
            by = 252 - ((i*36 + int(self.phase*31) + self.variant*11) % 176)
            rr = 2.1 + (i % 3)
            p.setPen(QPen(QColor(210, 248, 255, 140), 1.1))
            p.setBrush(Qt.NoBrush)
            p.drawEllipse(QPointF(bx, by), rr, rr)
        # Mascot itself.  The water is intentionally implied, not filled.
        bob = math.sin(self.phase * (1.8 + (self.variant % 5) * 0.05)) * (6.0 + self.variant % 3)
        glide = math.sin(self.phase * 0.7 + self.variant) * self.swim_arc
        tilt = math.sin(self.phase * 1.25 + self.variant * 0.2) * self.tilt_arc
        self._draw_swimming_otter(p, r.width()/2 + glide, 136 + bob, 1.08, tilt)
        # Status and progress bar beneath the swimmer.
        p.setPen(QColor(236, 248, 252))
        font = QFont(); font.setPointSize(10); font.setBold(True); p.setFont(font)
        p.drawText(QRectF(28, r.height()-72, r.width()-56, 22), Qt.AlignCenter, self.message)
        bx, by, bw, bh = 82.0, r.height()-40.0, r.width()-164.0, 10.0
        p.setPen(QPen(QColor(223, 244, 248, 155), 1.0))
        p.setBrush(QColor(20, 42, 50, 86))
        p.drawRoundedRect(QRectF(bx, by, bw, bh), 5.0, 5.0)
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(88, 213, 229, 235))
        p.drawRoundedRect(QRectF(bx, by, bw*self.progress/100.0, bh), 5.0, 5.0)
        p.setPen(QColor(226, 249, 253))
        f2 = QFont(); f2.setPointSize(8); p.setFont(f2)
        p.drawText(QRectF(bx, by+14, bw, 16), Qt.AlignCenter, f"{self.progress}%")


def create_mvc(progress: Callable[[str, int], None] | None = None) -> tuple[
    "GraphVisApplicationModel", "GraphVisWindow", "GraphVisApplicationController"
]:
    """Construct MVC components with imports deferred until after splash creation."""
    def stage(message: str, pct: int) -> None:
        if progress is not None:
            progress(message, pct)

    stage("Loading project model…", 12)
    from graphvis.core.workspace import active_project
    from graphvis.models.application_model import GraphVisApplicationModel

    stage("Loading interface and graph library…", 34)
    from graphvis.views.main_window import GraphVisWindow

    stage("Loading application controller…", 58)
    from graphvis.controllers.application_controller import GraphVisApplicationController

    stage("Opening workspace…", 72)
    model = GraphVisApplicationModel(active_project())
    view = GraphVisWindow(model=model)
    stage("Restoring GraphVis workspace…", 88)
    controller = GraphVisApplicationController(model, view)
    view.attach_controller(controller)
    return model, view, controller


def main() -> None:
    # Windows groups the taskbar/shortcut under this stable identity instead of
    # python.exe, allowing the GraphVis icon to appear on the taskbar.
    if os.name == "nt":
        try:
            import ctypes
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("GraphVis.ScientificStudio")
        except Exception:
            pass
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    app.setOrganizationName(APP_NAME)
    app.setStyle("Fusion")
    app.setAttribute(Qt.AA_SynthesizeMouseForUnhandledTouchEvents, True)
    if os.path.exists(ICON_FILE):
        app.setWindowIcon(QIcon(ICON_FILE))

    splash: SwimmingOtterSplash | None = None
    if os.path.exists(ICON_FILE):
        splash = SwimmingOtterSplash(ICON_FILE)
        # Kept explicit here as well as inside the splash class so regressions
        # can verify that startup never steals focus during Alt-Tab.
        splash.setWindowFlag(Qt.WindowStaysOnTopHint, False)
        splash.setWindowFlag(Qt.WindowDoesNotAcceptFocus, True)
        splash.setAttribute(Qt.WA_ShowWithoutActivating, True)
        splash.set_stage("Starting GraphVis…", 4)
        splash.show(); app.processEvents()

    def progress(message: str, pct: int) -> None:
        print(f"[GraphVis] {message}", flush=True)
        LOG.info("Startup %s%%: %s", pct, message)
        if splash is not None:
            splash.set_stage(message, pct); app.processEvents()

    progress("Loading scientific components…", 7)
    _model, view, _controller = create_mvc(progress)
    progress("Preparing main window…", 96)
    if os.path.exists(ICON_FILE):
        # Explicit per-window assignment is important on Windows when the app
        # is launched through pythonw.exe/batch files: it reinforces the stable
        # AppUserModelID icon for the taskbar as well as child window shells.
        view.setWindowIcon(QIcon(ICON_FILE))
    # If the user switched to another application while GraphVis was loading,
    # finishing startup must not steal focus back.  The main window is still
    # created and available on the taskbar; it activates when the user selects
    # GraphVis normally.
    app_was_active = app.applicationState() == Qt.ApplicationState.ApplicationActive
    if not app_was_active:
        view.setAttribute(Qt.WA_ShowWithoutActivating, True)
    view.show()
    app.processEvents()
    if splash is not None:
        splash.set_stage("GraphVis ready", 100); app.processEvents()
        # QSplashScreen.finish(view) may activate ``view`` on some window
        # managers.  If the user deliberately Alt-Tabbed away during startup,
        # simply close the mascot and leave GraphVis waiting on the taskbar.
        if app_was_active:
            splash.finish(view)
        else:
            splash.close()
    LOG.info("GraphVis MVC bootstrap completed")
    raise SystemExit(app.exec())


if __name__ == "__main__":
    main()
