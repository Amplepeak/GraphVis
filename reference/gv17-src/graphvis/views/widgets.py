# =========================================================================
# widgets.py — custom controls
#   RangeSlider      dual-handle float range control
#   CollapsibleBox   animated show/hide drawer
#   LoadingOverlay   non-blocking spinner + status text over any widget
#   ColorButton      swatch button backed by QColorDialog
#   SidebarSplitter  QSplitter with a wide touch handle and a collapse toggle
# =========================================================================
from __future__ import annotations

import html
import math
import random
from PySide6.QtCore import QEvent, QPoint, QPointF, QRect, QRectF, QSize, Qt, QTimer, Signal
from PySide6.QtGui import QBrush, QColor, QFont, QPainter, QPalette, QPen, QFontMetrics, QPixmap, QIcon, QPolygon
from graphvis.core.paths import ASSETS_DIR

from PySide6.QtWidgets import (QAbstractItemView, QApplication, QCheckBox, QColorDialog, QDialog, QDialogButtonBox, QFrame, QHBoxLayout, QLabel,
                               QLineEdit, QListWidget, QListWidgetItem, QPushButton, QSizePolicy, QSplitter, QSplitterHandle, QToolButton,
                               QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget, QStyledItemDelegate, QStyle, QStyleOptionViewItem,
                               QDockWidget, QMainWindow, QTextBrowser)


# ------------------------------------------------------ MutedSuffixDelegate
class MutedSuffixDelegate(QStyledItemDelegate):
    """Draw a trailing ``(short_variable)`` in a muted colour.

    The item text itself remains plain text for accessibility/search.  On Qt
    versions that support QComboBox.LabelDrawingMode.UseDelegate the same
    rendering is also used for the closed combo label.
    """
    def paint(self, painter, option, index):
        text = str(index.data(Qt.DisplayRole) or "")
        split = text.rfind(" (")
        if split <= 0 or not text.endswith(")"):
            return super().paint(painter, option, index)
        main, suffix = text[:split], text[split:]
        opt = QStyleOptionViewItem(option)
        self.initStyleOption(opt, index)
        style = opt.widget.style() if opt.widget else QApplication.style()
        full_text = opt.text
        opt.text = ""
        style.drawControl(QStyle.CE_ItemViewItem, opt, painter, opt.widget)
        rect = style.subElementRect(QStyle.SE_ItemViewItemText, opt, opt.widget)
        opt.text = full_text
        painter.save()
        painter.setFont(opt.font)
        normal = opt.palette.highlightedText().color() if (opt.state & QStyle.State_Selected) else opt.palette.text().color()
        painter.setPen(normal)
        fm = QFontMetrics(opt.font)
        y = rect.y() + (rect.height() + fm.ascent() - fm.descent()) // 2
        x = rect.x()
        painter.drawText(x, y, main)
        x += fm.horizontalAdvance(main)
        muted = QColor(normal); muted.setAlpha(135)
        painter.setPen(muted)
        painter.drawText(x, y, suffix)
        painter.restore()



# ---------------------------------------------------------------- RangeSlider
class RangeSlider(QWidget):
    """Dual-handle slider over a float domain.
    valueChanged fires continuously while dragging; rangeChanged on release."""

    valueChanged = Signal(float, float)
    rangeChanged = Signal(float, float)
    HANDLE = 12  # compact but still mouse-friendly

    def __init__(self, minimum=0.0, maximum=1.0, parent=None):
        super().__init__(parent)
        self._min = float(minimum)
        self._max = float(maximum) if maximum > minimum else float(minimum) + 1.0
        self._lo, self._hi = self._min, self._max
        self._drag = None
        self.setMinimumHeight(28)
        self.setMinimumWidth(120)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.setMouseTracking(True)

    def setDomain(self, minimum, maximum, keep_selection=False):
        minimum, maximum = float(minimum), float(maximum)
        if maximum <= minimum:
            maximum = minimum + 1e-9
        old_lo, old_hi = self._lo, self._hi
        was_full = (old_lo <= self._min) and (old_hi >= self._max)
        self._min, self._max = minimum, maximum
        if keep_selection and not was_full:
            self._lo = min(max(old_lo, minimum), maximum)
            self._hi = min(max(old_hi, minimum), maximum)
        else:
            self._lo, self._hi = minimum, maximum
        self.update()

    def domain(self):
        return self._min, self._max

    def values(self):
        return self._lo, self._hi

    def isFullRange(self):
        return self._lo <= self._min and self._hi >= self._max

    def setValues(self, lo, hi, emit=False):
        lo, hi = float(lo), float(hi)
        if hi < lo:
            lo, hi = hi, lo
        self._lo = min(max(lo, self._min), self._max)
        self._hi = min(max(hi, self._min), self._max)
        self.update()
        if emit:
            self.valueChanged.emit(self._lo, self._hi)
            self.rangeChanged.emit(self._lo, self._hi)

    def reset(self):
        self.setValues(self._min, self._max, emit=True)

    def _track_rect(self):
        m = self.HANDLE
        return QRect(m, self.height() // 2 - 3, max(self.width() - 2 * m, 1), 6)

    def _to_px(self, value):
        tr = self._track_rect()
        frac = (value - self._min) / (self._max - self._min)
        return tr.left() + frac * tr.width()

    def _to_value(self, px):
        tr = self._track_rect()
        frac = (px - tr.left()) / max(tr.width(), 1)
        return self._min + min(max(frac, 0.0), 1.0) * (self._max - self._min)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        pal = self.palette()
        track_col = pal.color(QPalette.Mid)
        accent = pal.color(QPalette.Highlight)
        base = pal.color(QPalette.Base)
        text = pal.color(QPalette.Text)
        tr = self._track_rect()
        p.setPen(Qt.NoPen)
        p.setBrush(QBrush(track_col))
        p.drawRoundedRect(tr, 3, 3)
        x_lo, x_hi = self._to_px(self._lo), self._to_px(self._hi)
        p.setBrush(QBrush(accent))
        p.drawRoundedRect(QRect(int(x_lo), tr.top(), max(int(x_hi - x_lo), 1), tr.height()), 3, 3)
        for x, active in ((x_lo, self._drag == 'lo'), (x_hi, self._drag == 'hi')):
            handle = QColor(accent) if active else QColor(base)
            p.setBrush(QBrush(handle))
            p.setPen(QPen(accent, 2))
            p.drawEllipse(int(x) - self.HANDLE // 2, self.height() // 2 - self.HANDLE // 2, self.HANDLE, self.HANDLE)
        p.setPen(QPen(text))
        f = p.font(); f.setPointSize(8); p.setFont(f)
        p.drawText(QRect(0, 0, self.width(), 12), Qt.AlignLeft, self._fmt(self._lo))
        p.drawText(QRect(0, 0, self.width(), 12), Qt.AlignRight, self._fmt(self._hi))

    @staticmethod
    def _fmt(v):
        a = abs(v)
        if a and (a < 1e-3 or a >= 1e5):
            return f"{v:.2e}"
        return f"{v:.4g}"

    def mousePressEvent(self, ev):
        if ev.button() != Qt.LeftButton:
            return
        x = ev.position().x()
        d_lo, d_hi = abs(x - self._to_px(self._lo)), abs(x - self._to_px(self._hi))
        self._drag = 'lo' if d_lo <= d_hi else 'hi'
        self._apply(x)

    def mouseMoveEvent(self, ev):
        if self._drag:
            self._apply(ev.position().x())

    def mouseReleaseEvent(self, _):
        if self._drag:
            self._drag = None
            self.update()
            self.rangeChanged.emit(self._lo, self._hi)

    def _apply(self, px):
        v = self._to_value(px)
        if self._drag == 'lo':
            self._lo = min(v, self._hi)
        else:
            self._hi = max(v, self._lo)
        self.update()
        self.valueChanged.emit(self._lo, self._hi)

    def sizeHint(self):
        return QSize(180, 28)


# CollapsibleBox was superseded by DetachablePanel and removed in the
# GraphVis 16.3 hygiene pass (it had no remaining call sites).

def install_typing_commit(spin, delay_ms: int = 650) -> None:
    """Make manually typed numbers apply themselves after a short pause.

    Keyboard tracking stays off (so half-typed values like the '2' in '200'
    never fire), but once the user stops typing for ``delay_ms`` the text is
    interpreted and committed — no Enter or focus change required.
    """
    editor = spin.lineEdit() if hasattr(spin, "lineEdit") else None
    if editor is None or getattr(spin, "_graphvis_typing_commit", None) is not None:
        return
    timer = QTimer(spin)
    timer.setSingleShot(True)
    timer.setInterval(max(150, int(delay_ms)))

    def _commit():
        try:
            if editor.hasFocus():
                spin.interpretText()   # emits valueChanged if the value changed
        except Exception:
            pass

    timer.timeout.connect(_commit)
    editor.textEdited.connect(lambda _t: timer.start())
    spin._graphvis_typing_commit = timer

# ------------------------------------------------------------ LoadingOverlay
class LoadingOverlay(QWidget):
    """Theme-aware cancellable loading overlay using the GraphVis otter.

    Thirty variants randomise the swim motion, fish, bubbles and coral scene
    while preserving the exact high-resolution GraphVis otter artwork.

    The otter gently bobs/tilts while a rotating water-arrow arc behind it
    signals activity.  This replaces the generic circular spinner and makes
    long copy/OCR/import operations visibly cancellable.
    """

    cancel_requested = Signal()

    def __init__(self, parent: QWidget, mode: str = "center"):
        super().__init__(parent)
        # "center" = legacy full-parent blocking scene; "corner" = compact
        # bottom-right badge that leaves the canvas interactive while a
        # background render/queue is active.
        self._mode = "corner" if str(mode).lower() == "corner" else "center"
        self._angle = 0
        self._message = "Working…"
        self._progress = -1
        self._display_progress = -1.0
        self._stall_ticks = 0
        self._timer = QTimer(self)
        self._timer.setInterval(30)
        self._timer.timeout.connect(self._tick)
        # Expensive operations should not flash a loading overlay when they
        # finish quickly.  The overlay is therefore armed immediately but is
        # only made visible after the global five-second UX threshold.
        self._delay_timer = QTimer(self)
        self._delay_timer.setSingleShot(True)
        self._delay_timer.timeout.connect(self._show_now)
        self._pending_cancellable = False
        self.setAttribute(Qt.WA_TransparentForMouseEvents, False)
        self.setAttribute(Qt.WA_NoSystemBackground)
        self.setAttribute(Qt.WA_TranslucentBackground, True)
        # Use the exact GraphVis icon artwork so the loading overlay matches
        # the otter branding colours/style seen in shortcuts and the app icon.
        self._otter_pixmap = QPixmap(str(ASSETS_DIR / 'graphvis_icon.png'))
        self._otter_variant = random.randrange(30)
        _otter_rng = random.Random(7301 + self._otter_variant * 131)
        self._otter_spots = [_otter_rng.random() for _ in range(9)]
        self.cancel_button = QPushButton("Cancel", self)
        self.cancel_button.setObjectName("LoadingCancelButton")
        self.cancel_button.setMinimumSize(96, 30)
        self.cancel_button.clicked.connect(self.cancel_requested.emit)
        self.cancel_button.hide()
        self.hide()
        parent.installEventFilter(self)
        self._sync_geometry()

    def eventFilter(self, obj, ev):
        if obj is self.parent() and ev.type() in (QEvent.Resize, QEvent.Move, QEvent.Show):
            self._sync_geometry()
        return False

    def _sync_geometry(self):
        p = self.parentWidget()
        if not p:
            return
        if self._mode == "corner":
            # Bottom-right badge: activity stays visible without covering the
            # figure, and the rest of the canvas remains interactive.
            w = min(340, max(220, p.width() - 24))
            h = 84
            self.setGeometry(max(0, p.width() - w - 12), max(0, p.height() - h - 12), w, h)
            if hasattr(self, "cancel_button"):
                self.cancel_button.setMinimumSize(72, 24)
                self.cancel_button.resize(72, 24)
                self.cancel_button.move(self.width() - 80, self.height() - 30)
            return
        self.setGeometry(p.rect())
        if hasattr(self, "cancel_button"):
            x = max(8, (self.width() - self.cancel_button.width()) // 2)
            y = max(8, int(self.height() / 2 + 130))
            self.cancel_button.move(x, y)

    def start(self, message: str = "Working…", progress: int = -1, *, cancellable: bool = False,
              delay_ms: int = 5000, immediate: bool = False):
        """Arm the activity indicator.

        By default the otter animation appears only after five seconds.  This
        keeps fast actions visually quiet while guaranteeing feedback for
        genuinely long work.  ``immediate=True`` remains available for flows
        where the caller explicitly wants instant blocking feedback.
        """
        self._message = message
        # Each long operation gets a different one of thirty mascot motions.
        # Avoid an immediate repeat so repeated imports do not look frozen.
        previous = self._otter_variant
        self._otter_variant = random.randrange(30)
        if self._otter_variant == previous:
            self._otter_variant = (self._otter_variant + random.randrange(1, 30)) % 30
        rnd = random.Random(7301 + self._otter_variant * 131)
        self._otter_spots = [rnd.random() for _ in range(9)]
        self._progress = int(progress) if progress is not None else -1
        self._display_progress = float(self._progress)
        self._stall_ticks = 0
        self._pending_cancellable = bool(cancellable)
        self.cancel_button.setEnabled(True)
        self.cancel_button.setText("Cancel")
        self._delay_timer.stop()
        self._timer.stop()
        self.hide()
        self.cancel_button.hide()
        delay = 0 if immediate else max(0, int(delay_ms))
        if delay == 0:
            self._show_now()
        else:
            self._delay_timer.start(delay)

    def _show_now(self):
        self._sync_geometry()
        self.cancel_button.setVisible(self._pending_cancellable)
        self.raise_()
        self.show()
        if not self._timer.isActive():
            self._timer.start()
        self.update()

    def is_armed(self) -> bool:
        return self.isVisible() or self._delay_timer.isActive() or self._timer.isActive()

    def set_cancellable(self, on: bool = True):
        self._pending_cancellable = bool(on)
        self.cancel_button.setVisible(bool(on) and self.isVisible())
        self._sync_geometry()

    def set_message(self, message: str, progress: int | None = None):
        self._message = message
        if progress is not None:
            self.set_progress(progress)
        else:
            self.update()

    def set_progress(self, value: int):
        self._progress = int(value) if value is not None else -1
        if self._progress < 0:
            self._display_progress = -1.0
        else:
            self._display_progress = max(float(self._progress), self._display_progress if self._display_progress >= 0 else 0.0)
        self._stall_ticks = 0
        self.update()

    def mark_cancelling(self):
        if self.cancel_button.isVisible():
            self.cancel_button.setText("Cancelling…")
            self.cancel_button.setEnabled(False)
        self._message = "Cancelling — waiting for the current safe checkpoint…"
        self.update()

    def stop(self):
        self._delay_timer.stop()
        self._timer.stop()
        self._pending_cancellable = False
        self.cancel_button.hide()
        self.hide()

    def _tick(self):
        self._angle = (self._angle + 7) % 360
        # Some parsers cannot expose byte-level progress. Keep an explicitly
        # approximate visual estimate moving rather than appearing frozen at
        # 20%; never claim completion before the worker reports it.
        self._stall_ticks += 1
        if 0 <= self._progress < 95 and self._stall_ticks > 18:
            target = min(92.0, max(float(self._progress) + 8.0, 92.0))
            if self._display_progress < target:
                self._display_progress = min(target, self._display_progress + 0.08)
        self.update()

    def _draw_otter_activity(self, p: QPainter, cx: float, cy: float, accent: QColor, track_col: QColor):
        # Transparent-water loading scene: the program behind stays visible
        # while the exact GraphVis otter icon swims with fish, bubbles and coral.
        bob = math.sin(math.radians(self._angle * (1.5 + (self._otter_variant % 4) * 0.10))) * 4.2
        tilt = math.sin(math.radians(self._angle + self._otter_variant * 9)) * (2.4 + (self._otter_variant % 5) * 0.18)
        drift = math.sin(math.radians(self._angle * 0.65 + self._otter_variant * 12)) * (18 + (self._otter_variant % 6) * 2)
        fish_cols = [QColor(81, 175, 243, 220), QColor(255, 206, 84, 212), QColor(149, 111, 228, 210), QColor(113, 231, 214, 210)]
        for i in range(7):
            lane = cy - 52 + i * 16
            phase = math.radians(self._angle * (0.42 + i * 0.035) + self._otter_variant * (8 + i))
            fx = (cx - 160) + ((phase * 92 + i * 58) % 332)
            fy = lane + math.sin(phase * 2.3) * (5 + (i % 3) * 1.7)
            size = 7 + (i % 3) * 1.7
            body = fish_cols[(i + self._otter_variant) % len(fish_cols)]
            p.setPen(QPen(QColor(10, 35, 49, 170), 1.0))
            p.setBrush(body)
            p.drawEllipse(QRectF(fx - size * 0.70, fy - size * 0.40, size * 1.12, size * 0.80))
            p.drawPolygon(QPolygon([QPoint(int(fx - size * 0.95), int(fy)), QPoint(int(fx - size * 1.40), int(fy - size * 0.42)), QPoint(int(fx - size * 1.40), int(fy + size * 0.42))]))
        # Draw the exact GraphVis icon sprite rather than a simplified vector otter.
        if not self._otter_pixmap.isNull():
            p.save()
            p.translate(cx + drift, cy + bob)
            p.rotate(tilt)
            w = 108 + (self._otter_variant % 5) * 4
            h = w * self._otter_pixmap.height() / max(1, self._otter_pixmap.width())
            target = QRectF(-w / 2.0, -h / 2.0, w, h)
            p.drawPixmap(target, self._otter_pixmap, QRectF(0.0, 0.0, float(self._otter_pixmap.width()), float(self._otter_pixmap.height())))
            p.restore()
        coral_y = cy + 56
        coral_cols = [QColor('#E16B7B'), QColor('#F0A15A'), QColor('#A56DD2'), QColor('#65D1BA')]
        for i in range(6):
            x = cx - 78 + i * 30 + ((self._otter_variant + i * 3) % 9 - 4)
            h = 10 + ((self._otter_variant * (i + 2)) % 11)
            p.setPen(QPen(coral_cols[(i + self._otter_variant) % len(coral_cols)], 3, Qt.SolidLine, Qt.RoundCap))
            p.drawLine(QPointF(x, coral_y), QPointF(x, coral_y - h))
            p.drawLine(QPointF(x, coral_y - h * 0.5), QPointF(x - 5, coral_y - h * 0.78))
            p.drawLine(QPointF(x, coral_y - h * 0.35), QPointF(x + 5, coral_y - h * 0.70))
        for i in range(9):
            bx = cx - 96 + ((i * 29 + self._otter_variant * 13) % 190)
            by = cy + 34 - ((i * 18 + int(self._angle * 0.34)) % 94)
            rr = 1.9 + (i % 3) * 0.7
            p.setPen(QPen(QColor(220, 248, 255, 150), 1.0))
            p.setBrush(Qt.NoBrush)
            p.drawEllipse(QPointF(bx, by), rr, rr)

    def _paint_corner(self, p: QPainter, accent: QColor, track_col: QColor, text_col: QColor):
        """Compact bottom-right processing badge: logo, message, progress bar."""
        r = self.rect().adjusted(1, 1, -1, -1)
        p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 190), 1.2))
        p.setBrush(QBrush(QColor(20, 32, 40, 215)))
        p.drawRoundedRect(QRectF(r), 10, 10)
        # Bobbing otter logo at the left edge of the badge.
        bob = math.sin(math.radians(self._angle * 1.6)) * 2.6
        tilt = math.sin(math.radians(self._angle + self._otter_variant * 9)) * 3.0
        if not self._otter_pixmap.isNull():
            p.save()
            p.translate(34, self.height() / 2 + bob)
            p.rotate(tilt)
            w = 44.0
            h = w * self._otter_pixmap.height() / max(1, self._otter_pixmap.width())
            p.drawPixmap(QRectF(-w / 2, -h / 2, w, h), self._otter_pixmap,
                         QRectF(0.0, 0.0, float(self._otter_pixmap.width()), float(self._otter_pixmap.height())))
            p.restore()
        text_left = 62
        text_right = self.width() - (86 if self.cancel_button.isVisible() else 12)
        p.setPen(QPen(QColor(232, 246, 252)))
        f = QFont(); f.setPointSize(8); f.setBold(True); p.setFont(f)
        metrics_rect = QRect(text_left, 10, max(20, text_right - text_left), 30)
        p.drawText(metrics_rect, Qt.AlignLeft | Qt.AlignTop | Qt.TextWordWrap, self._message)
        # Progress bar along the bottom of the badge.
        bar_h = 8
        bx, by = float(text_left), float(self.height() - 22)
        bar_w = float(max(40, text_right - text_left))
        track = QRectF(bx, by, bar_w, bar_h)
        p.setPen(QPen(QColor(track_col.red(), track_col.green(), track_col.blue(), 150), 1.0))
        p.setBrush(QBrush(QColor(14, 24, 30, 120)))
        p.drawRoundedRect(track, 4, 4)
        shown = self._display_progress if self._display_progress >= 0 else float(self._progress)
        if 0 <= shown <= 100:
            fill = QRectF(bx, by, bar_w * shown / 100.0, bar_h)
        else:  # indeterminate sweep while the queue is busy
            frac = (self._angle % 360) / 360.0
            seg = bar_w * 0.30
            x = bx + (bar_w + seg) * frac - seg
            fill = QRectF(max(bx, x), by, min(seg, bx + bar_w - max(bx, x)), bar_h)
        p.setPen(Qt.NoPen)
        p.setBrush(QBrush(accent))
        if fill.width() > 0:
            p.drawRoundedRect(fill, 4, 4)
        if 0 <= shown <= 100:
            p.setPen(QPen(QColor(214, 234, 244)))
            f2 = QFont(); f2.setPointSize(7); p.setFont(f2)
            approximate = self._progress >= 0 and shown > float(self._progress) + 0.5
            label = f"~{int(shown)}%" if approximate else f"{int(shown)}%"
            p.drawText(QRectF(bx, by - 13, bar_w, 12), Qt.AlignRight | Qt.AlignVCenter, label)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        pal = self.palette()
        accent = pal.color(QPalette.Highlight)
        track_col = pal.color(QPalette.Mid)
        text_col = pal.color(QPalette.WindowText)
        if self._mode == "corner":
            self._paint_corner(p, accent, track_col, text_col)
            return
        cx, cy = self.width() / 2, self.height() / 2 - 52
        self._draw_otter_activity(p, cx, cy, accent, track_col)
        p.setPen(QPen(text_col))
        f = QFont(); f.setPointSize(10); f.setBold(True); p.setFont(f)
        msg_rect = QRect(20, int(cy + 72), max(20, self.width()-40), 52)
        p.drawText(msg_rect, Qt.AlignHCenter | Qt.AlignTop | Qt.TextWordWrap, self._message)
        bar_w = min(max(self.width() * 0.42, 180), 420)
        bar_h = 10
        bx = (self.width() - bar_w) / 2
        by = cy + 126
        track = QRectF(bx, by, bar_w, bar_h)
        p.setPen(QPen(QColor(track_col.red(), track_col.green(), track_col.blue(), 155), 1.0))
        p.setBrush(QBrush(QColor(18, 29, 36, 92)))
        p.drawRoundedRect(track, 5, 5)
        shown_progress = self._display_progress if self._display_progress >= 0 else float(self._progress)
        if 0 <= shown_progress <= 100:
            fill = QRectF(bx, by, bar_w * shown_progress / 100.0, bar_h)
        else:
            frac = (self._angle % 360) / 360.0
            seg = bar_w * 0.28
            x = bx + (bar_w + seg) * frac - seg
            fill = QRectF(max(bx, x), by, min(seg, bx + bar_w - max(bx, x)), bar_h)
        p.setPen(Qt.NoPen)
        p.setBrush(QBrush(accent))
        if fill.width() > 0:
            p.drawRoundedRect(fill, 5, 5)
        if 0 <= shown_progress <= 100:
            p.setPen(QPen(text_col)); f2 = QFont(); f2.setPointSize(8); p.setFont(f2)
            approximate = self._progress >= 0 and shown_progress > float(self._progress) + 0.5
            label = f"~{int(shown_progress)}%" if approximate else f"{int(shown_progress)}%"
            p.drawText(QRectF(bx, by + 13, bar_w, 18), Qt.AlignCenter, label)

    def mousePressEvent(self, ev):
        ev.accept()



# ----------------------------------------------------- compact studio chrome
class GripDots(QWidget):
    """Tiny six-dot grip used as quiet visual affordance for movable panels."""
    def __init__(self, parent=None, *, horizontal: bool = False):
        super().__init__(parent)
        self._horizontal = bool(horizontal)
        self.setFixedSize(18 if horizontal else 12, 18 if horizontal else 22)
        self.setAttribute(Qt.WA_TransparentForMouseEvents, True)

    def paintEvent(self, _event):
        p = QPainter(self); p.setRenderHint(QPainter.Antialiasing)
        c = self.palette().color(QPalette.Mid); c.setAlpha(180)
        p.setPen(Qt.NoPen); p.setBrush(c)
        if self._horizontal:
            xs, ys = (5, 9, 13), (7, 11)
        else:
            xs, ys = (4, 8), (5, 9, 13)
        for x in xs:
            for y in ys:
                p.drawEllipse(QPoint(x, y), 1, 1)


class CompactSplitterHandle(QSplitterHandle):
    """Low-profile splitter handle with a Figma-like dotted centre grip."""
    def __init__(self, orientation, parent):
        super().__init__(orientation, parent)
        self.setObjectName("GraphVisCompactSplitterHandle")

    def paintEvent(self, _event):
        p = QPainter(self); p.setRenderHint(QPainter.Antialiasing)
        base = self.palette().color(QPalette.Mid); base.setAlpha(105)
        dot = self.palette().color(QPalette.Text); dot.setAlpha(95)
        p.setPen(QPen(base, 1))
        if self.orientation() == Qt.Vertical:
            y = self.height() // 2
            p.drawLine(0, y, self.width(), y)
            p.setPen(Qt.NoPen); p.setBrush(dot)
            cx = self.width() // 2
            for dx in (-6, 0, 6): p.drawEllipse(QPoint(cx + dx, y), 1, 1)
        else:
            x = self.width() // 2
            p.drawLine(x, 0, x, self.height())
            p.setPen(Qt.NoPen); p.setBrush(dot)
            cy = self.height() // 2
            for dy in (-6, 0, 6): p.drawEllipse(QPoint(x, cy + dy), 1, 1)


class CompactSplitter(QSplitter):
    """Native QSplitter behaviour with a discreet 7 px dotted handle."""
    def __init__(self, orientation=Qt.Horizontal, parent=None):
        super().__init__(orientation, parent)
        self.setHandleWidth(7)
        self.setChildrenCollapsible(True)
        # Rubber-band resizing avoids forcing Matplotlib/complex docks through
        # a full layout and redraw for every mouse pixel while dragging.
        self.setOpaqueResize(False)

    def createHandle(self):
        return CompactSplitterHandle(self.orientation(), self)


def compact_tool_icon(kind: str, size: int = 18) -> QIcon:
    """Small neutral vector icon set for the non-intrusive studio toolbars."""
    k = str(kind or "").lower()
    pix = QPixmap(size, size); pix.fill(Qt.transparent)
    p = QPainter(pix); p.setRenderHint(QPainter.Antialiasing)
    fg = QColor("#90A4B7"); accent = QColor("#5AA9E6")
    p.setPen(QPen(fg, 1.5, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin)); p.setBrush(Qt.NoBrush)
    w = float(size)
    if k in {"layers", "stack"}:
        for off in (0, 3, 6): p.drawRoundedRect(QRectF(3+off/3, 4+off, w-8, 6), 1.2, 1.2)
    elif k == "roi":
        p.drawRect(QRectF(3.5, 3.5, w-7, w-7)); p.setPen(QPen(accent, 1.5)); p.drawLine(6, 6, 10, 6); p.drawLine(6, 6, 6, 10)
    elif k == "fit":
        pts=[QPoint(2,int(w-4)), QPoint(6,int(w-9)), QPoint(10,int(w-7)), QPoint(15,3)]
        p.drawPolyline(QPolygon(pts)); p.setBrush(accent); p.setPen(Qt.NoPen)
        for pt in pts: p.drawEllipse(pt,1,1)
    elif k == "smooth":
        pts=[QPoint(2,11),QPoint(5,7),QPoint(8,10),QPoint(11,6),QPoint(15,8)]
        p.drawPolyline(QPolygon(pts)); p.setPen(QPen(accent,1.4)); p.drawLine(2,13,15,5)
    elif k == "style":
        for y,x in ((5,10),(9,6),(13,12)):
            p.drawLine(3,y,15,y); p.setBrush(accent); p.setPen(Qt.NoPen); p.drawEllipse(QPoint(x,y),2,2); p.setPen(QPen(fg,1.5)); p.setBrush(Qt.NoBrush)
    elif k in {"annotate", "text"}:
        p.setFont(QFont("Arial", 10, QFont.Weight.Bold)); p.drawText(QRectF(1,1,w-2,w-2), Qt.AlignCenter, "T")
    elif k in {"publish", "document"}:
        p.drawRoundedRect(QRectF(4,2.5,10,13),1,1); p.drawLine(6,7,12,7); p.drawLine(6,10,12,10); p.setPen(QPen(accent,1.5)); p.drawLine(8,13,12,13)
    elif k in {"gpu", "cube", "3d"}:
        p.drawPolygon(QPolygon([QPoint(9,2),QPoint(15,6),QPoint(9,10),QPoint(3,6)])); p.drawLine(3,6,3,12); p.drawLine(15,6,15,12); p.drawLine(3,12,9,16); p.drawLine(15,12,9,16); p.drawLine(9,10,9,16)
    elif k in {"digitize", "crosshair"}:
        p.drawEllipse(QRectF(5,5,8,8)); p.drawLine(9,1,9,6); p.drawLine(9,12,9,17); p.drawLine(1,9,6,9); p.drawLine(12,9,17,9)
    elif k in {"controls", "sliders"}:
        for y,x in ((5,7),(9,12),(13,5)):
            p.drawLine(2,y,16,y); p.setBrush(accent); p.setPen(Qt.NoPen); p.drawEllipse(QPoint(x,y),2,2); p.setPen(QPen(fg,1.5)); p.setBrush(Qt.NoBrush)
    elif k in {"objects", "tree"}:
        p.drawRect(QRectF(2.5,3,4,4)); p.drawRect(QRectF(11.5,2,4,4)); p.drawRect(QRectF(11.5,11,4,4)); p.drawLine(6.5,5,9,5); p.drawLine(9,5,9,13); p.drawLine(9,13,11.5,13)
    elif k in {"reset", "home"}:
        p.drawArc(QRectF(3,3,12,12), 35*16, 285*16); p.setBrush(accent); p.setPen(Qt.NoPen); p.drawPolygon(QPolygon([QPoint(4,3),QPoint(8,3),QPoint(5,7)]))
    elif k in {"export", "save"}:
        p.drawRoundedRect(QRectF(3,9,12,6),1,1); p.drawLine(9,2,9,11); p.drawLine(6,5,9,2); p.drawLine(12,5,9,2)
    elif k in {"play", "preview"}:
        p.setBrush(accent); p.setPen(Qt.NoPen); p.drawPolygon(QPolygon([QPoint(5,3),QPoint(15,9),QPoint(5,15)]))
    elif k == "grid":
        for x in (4,9,14): p.drawLine(x,3,x,15)
        for y in (4,9,14): p.drawLine(3,y,15,y)
    elif k in {"left", "previous"}:
        p.setPen(QPen(accent, 2.0, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
        p.drawLine(int(w*0.63), 3, int(w*0.34), int(w*0.5))
        p.drawLine(int(w*0.34), int(w*0.5), int(w*0.63), int(w-3))
    elif k in {"right", "next"}:
        p.setPen(QPen(accent, 2.0, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
        p.drawLine(int(w*0.37), 3, int(w*0.66), int(w*0.5))
        p.drawLine(int(w*0.66), int(w*0.5), int(w*0.37), int(w-3))
    elif k in {"more", "dots"}:
        p.setBrush(fg); p.setPen(Qt.NoPen)
        for x in (4,9,14): p.drawEllipse(QPoint(x,9),1,1)
    else:
        p.drawRoundedRect(QRectF(3,3,w-6,w-6),2,2)
    p.end(); return QIcon(pix)

# ---------------------------------------------------------- dock title bars
def _stacked_rectangles_icon() -> QIcon:
    """Small outlined/stacked rectangles used consistently for pop-out."""
    pix = QPixmap(18, 18); pix.fill(Qt.transparent)
    p = QPainter(pix); p.setRenderHint(QPainter.Antialiasing)
    pen = QPen(QColor("#8FA7BC"), 1.5); p.setPen(pen); p.setBrush(Qt.NoBrush)
    p.drawRoundedRect(QRectF(3.5, 5.5, 9.5, 8.0), 1.0, 1.0)
    p.drawRoundedRect(QRectF(6.0, 3.0, 8.5, 7.5), 1.0, 1.0)
    p.end(); return QIcon(pix)


def stacked_rectangles_icon() -> QIcon:
    return _stacked_rectangles_icon()


class DockTitleBar(QFrame):
    """Ultra-compact dock chrome: grip, title, pop-out and hide."""
    def __init__(self, dock: QDockWidget, title: str):
        super().__init__(dock)
        self._dock = dock
        self.setObjectName("GraphVisDockTitleBar")
        self.setFixedHeight(24)
        lay = QHBoxLayout(self); lay.setContentsMargins(3, 1, 2, 1); lay.setSpacing(2)
        lay.addWidget(GripDots(self, horizontal=True), 0, Qt.AlignVCenter)
        lbl = QLabel(str(title)); lbl.setObjectName("GraphVisDockTitle"); lbl.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        lay.addWidget(lbl, 1)
        self.btn_float = QToolButton(); self.btn_float.setIcon(_stacked_rectangles_icon())
        self.btn_float.setObjectName("CompactChromeButton"); self.btn_float.setToolTip("Pop out / dock this window")
        self.btn_float.setFixedSize(20, 20); self.btn_float.clicked.connect(self.toggle_floating)
        lay.addWidget(self.btn_float)
        self.btn_hide = QToolButton(); self.btn_hide.setText("×"); self.btn_hide.setObjectName("CompactChromeButton")
        self.btn_hide.setToolTip("Hide / return panel"); self.btn_hide.setFixedSize(20, 20); self.btn_hide.clicked.connect(dock.close); lay.addWidget(self.btn_hide)

    def toggle_floating(self) -> None:
        self._dock.setFloating(not self._dock.isFloating())
        self._dock.show(); self._dock.raise_(); self._dock.activateWindow()

    def mouseDoubleClickEvent(self, event) -> None:
        self.toggle_floating(); event.accept()


def install_dock_titlebar(dock: QDockWidget, title: str | None = None) -> DockTitleBar:
    bar = DockTitleBar(dock, title or dock.windowTitle())
    dock.setTitleBarWidget(bar)
    return bar


# ---------------------------------------------------------- DetachablePanel
class DetachableDockWidget(QDockWidget):
    """Floating/dockable shell that returns panel content only on explicit close.

    Using ``visibilityChanged(False)`` as a redock trigger makes a dock vanish
    when it is tabified, temporarily covered, or the main window changes state.
    An explicit close signal keeps the panel genuinely dockable.
    """

    redockRequested = Signal()

    def closeEvent(self, event) -> None:
        event.ignore()
        self.redockRequested.emit()


class DetachablePanel(QWidget):
    """Slim collapsible panel with Figma-like chrome and optional floating dock.

    The header is deliberately only 24 px high.  A chevron controls expansion,
    six subtle grip dots communicate movability, and the outlined rectangles
    icon toggles a genuine QDockWidget.  Content uses compact margins so the
    scientific canvas, not the controls, remains visually dominant.
    """

    detachedChanged = Signal(bool)

    def __init__(self, title: str, expanded: bool = True, parent=None):
        super().__init__(parent)
        self._title = str(title)
        self._expanded = bool(expanded)
        self._float_dock: QDockWidget | None = None
        self.setObjectName("GraphVisDetachablePanel")

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)
        self._root = root

        self.header = QFrame(); self.header.setObjectName("DetachablePanelHeader"); self.header.setFixedHeight(24)
        self.header.setToolTip("Double-click or use the rectangles button to pop out; floating panels can be docked on any window edge")
        self.header.installEventFilter(self)
        h = QHBoxLayout(self.header); h.setContentsMargins(2, 1, 2, 1); h.setSpacing(2)
        self.toggle = QToolButton(); self.toggle.setObjectName("CompactPanelChevron")
        self.toggle.setCheckable(True); self.toggle.setChecked(self._expanded)
        self.toggle.setArrowType(Qt.DownArrow if self._expanded else Qt.RightArrow)
        self.toggle.setToolTip("Collapse / expand")
        self.toggle.setFixedSize(20, 20); self.toggle.clicked.connect(self.setExpanded)
        h.addWidget(self.toggle)
        h.addWidget(GripDots(self.header, horizontal=True), 0, Qt.AlignVCenter)
        self.title_label = QLabel(self._title); self.title_label.setObjectName("CompactPanelTitle")
        self.title_label.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        h.addWidget(self.title_label, 1)
        self.detach_button = QToolButton(); self.detach_button.setObjectName("CompactChromeButton")
        self.detach_button.setIcon(self._popout_icon()); self.detach_button.setToolTip("Pop out / dock this panel")
        self.detach_button.setFixedSize(20, 20); self.detach_button.clicked.connect(self.detach)
        h.addWidget(self.detach_button)
        root.addWidget(self.header)

        self.content = QWidget(); self.content.setObjectName("DetachablePanelContent")
        self.content_layout = QVBoxLayout(self.content)
        self.content_layout.setContentsMargins(4, 3, 4, 4); self.content_layout.setSpacing(4)
        self.content.setVisible(self._expanded); root.addWidget(self.content)

    @staticmethod
    def _popout_icon() -> QIcon:
        return _stacked_rectangles_icon()

    def setTitle(self, title: str) -> None:
        self._title = str(title); self.title_label.setText(self._title)
        if self._float_dock is not None: self._float_dock.setWindowTitle(self._title)

    def addWidget(self, widget: QWidget, stretch: int = 0) -> None:
        self.content_layout.addWidget(widget, stretch)

    def addLayout(self, layout, stretch: int = 0) -> None:
        self.content_layout.addLayout(layout, stretch)

    def isExpanded(self) -> bool:
        return self._expanded

    def setExpanded(self, expanded: bool) -> None:
        self._expanded = bool(expanded)
        self.toggle.blockSignals(True); self.toggle.setChecked(self._expanded); self.toggle.blockSignals(False)
        self.toggle.setArrowType(Qt.DownArrow if self._expanded else Qt.RightArrow)
        if self._float_dock is None:
            self.content.setVisible(self._expanded)
        else:
            self.content.setVisible(True)

    def _main_window(self):
        w = self.window()
        return w if isinstance(w, QMainWindow) else None

    def detach(self) -> None:
        if self._float_dock is not None:
            if self._float_dock.isFloating():
                self._redock()
            else:
                self._float_dock.setFloating(True); self._float_dock.raise_(); self._float_dock.activateWindow()
            return
        main = self._main_window()
        if main is None:
            return
        dock = DetachableDockWidget(self._title, main)
        dock.setObjectName("GraphVisPanel_" + "".join(ch if ch.isalnum() else "_" for ch in self._title))
        dock.setAllowedAreas(Qt.AllDockWidgetAreas)
        dock.setFeatures(QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetFloatable | QDockWidget.DockWidgetClosable)
        install_dock_titlebar(dock, self._title)
        self.content.setParent(dock); self.content.setVisible(True); dock.setWidget(self.content)
        self._float_dock = dock
        main.addDockWidget(Qt.RightDockWidgetArea, dock); dock.setFloating(True)
        dock.resize(max(min(self.width(), 520), 340), max(min(self.content.sizeHint().height()+34, 720), 220))
        dock.redockRequested.connect(self._redock)
        dock.show(); dock.raise_(); self.detachedChanged.emit(True)

    def eventFilter(self, obj, event):
        if obj is self.header and event.type() == QEvent.MouseButtonDblClick and event.button() == Qt.LeftButton:
            self.detach(); event.accept(); return True
        return super().eventFilter(obj, event)

    def _redock(self, *_args) -> None:
        dock = self._float_dock
        if dock is None:
            return
        main = self._main_window()
        try:
            dock.redockRequested.disconnect(self._redock)
        except Exception:
            pass
        dock.setWidget(None)
        self.content.setParent(self); self._root.addWidget(self.content); self.content.setVisible(self._expanded)
        self._float_dock = None
        if main is not None:
            main.removeDockWidget(dock)
        dock.deleteLater(); self.detachedChanged.emit(False)


# ---------------------------------------------------------- ControlSidebarDock
class ControlSidebarDock(QDockWidget):
    """Native resizable/floating replacement for the old constrained splitter.

    The main window keeps the historical ``self.splitter`` attribute for
    session compatibility, but the implementation is now a real QDockWidget so
    the divider uses Qt's normal drag behaviour and the whole panel can float on
    another monitor.
    """

    sidebarToggled = Signal(bool)
    DEFAULT_WIDTH = 360
    MIN_WIDTH = 250
    MAX_WIDTH = 720

    def __init__(self, title: str = "Controls", parent=None):
        super().__init__(title, parent)
        self._preferred_width = self.DEFAULT_WIDTH
        self.setObjectName("GraphVisControlSidebar")
        self.setAttribute(Qt.WA_StaticContents, True)
        self.setAllowedAreas(Qt.LeftDockWidgetArea | Qt.RightDockWidgetArea)
        self.setFeatures(QDockWidget.DockWidgetFeature.DockWidgetMovable | QDockWidget.DockWidgetFeature.DockWidgetFloatable | QDockWidget.DockWidgetFeature.DockWidgetClosable)
        install_dock_titlebar(self, title)
        self.visibilityChanged.connect(self.sidebarToggled.emit)
        self.setMinimumWidth(self.MIN_WIDTH)

    def _bounded(self, width: int) -> int:
        return max(self.MIN_WIDTH, min(int(width), self.MAX_WIDTH))

    def restoreSidebar(self, width: int | None = None, collapsed: bool = False) -> None:
        self._preferred_width = self._bounded(width or self.DEFAULT_WIDTH)
        self.setVisible(not bool(collapsed))
        QTimer.singleShot(0, self._apply_preferred_width)

    def _apply_preferred_width(self) -> None:
        parent = self.parentWidget()
        if parent is not None and hasattr(parent, "resizeDocks") and not self.isFloating() and self.isVisible():
            try:
                parent.resizeDocks([self], [self._preferred_width], Qt.Horizontal)
                return
            except Exception:
                pass
        if self.isFloating():
            self.resize(self._preferred_width, max(self.height(), 640))

    def resetSidebarWidth(self) -> None:
        self._preferred_width = self.DEFAULT_WIDTH
        self.setVisible(True)
        self._apply_preferred_width()

    def toggleSidebar(self) -> None:
        self.setVisible(not self.isVisible())

    def setSidebarCollapsed(self, collapsed: bool) -> None:
        self.setVisible(not bool(collapsed))

    def isSidebarCollapsed(self) -> bool:
        return not self.isVisible()

    def currentSidebarWidth(self) -> int:
        if self.isVisible():
            self._preferred_width = self._bounded(self.width())
        return self._preferred_width

    def sizes(self) -> list[int]:
        parent = self.parentWidget()
        total = parent.width() if parent is not None else self.currentSidebarWidth() + 1000
        w = 0 if self.isSidebarCollapsed() else self.currentSidebarWidth()
        return [w, max(total - w, 0)]

# ---------------------------------------------------------- GraphLibraryDialog
class GraphLibraryDialog(QDialog):
    _ICON_CACHE: dict[str, QIcon] = {}
    """Categorized, fuzzy-searchable GraphVis plot library.

    Capability tuples are keyed by GraphVis engine name and contain
    ``(available, reason)``. Unsupported/niche entries remain visible and are
    greyed out instead of disappearing.
    """

    def __init__(self, capabilities: dict | None = None, parent=None):
        super().__init__(parent)
        from graphvis.rendering.graph_library import GRAPH_LIBRARY, search_entries
        self._all_entries = GRAPH_LIBRARY
        self._search_entries = search_entries
        self._caps = capabilities or {}
        self.selected_entry = None
        self.setWindowTitle("GraphVis Graph Library")
        self.resize(760, 680)
        root = QVBoxLayout(self)
        head = QLabel("<b>Graph library</b> — search by name or purpose. Hover an item for a preview and requirements.")
        head.setWordWrap(True)
        root.addWidget(head)
        row = QHBoxLayout()
        self.search = QLineEdit()
        self.search.setPlaceholderText("Search plots (fuzzy matching, e.g. 'scater', 'surface', 'distribution')…")
        self.search.textChanged.connect(self._refresh)
        row.addWidget(self.search, 1)
        self.advanced = QCheckBox("Advanced View")
        self.advanced.toggled.connect(self._refresh)
        row.addWidget(self.advanced)
        root.addLayout(row)
        self.tree = QTreeWidget()
        self.tree.setHeaderLabels(["Plot", "Preview", "Availability"])
        compact = QApplication.instance().property("graphvis_compact_previews") if QApplication.instance() else True
        self.tree.setIconSize(QSize(48, 30) if compact is not False else QSize(66, 40))
        self.tree.setColumnWidth(0, 280)
        self.tree.setColumnWidth(1, 110)
        self.tree.itemDoubleClicked.connect(lambda *_: self._accept_current())
        self.tree.currentItemChanged.connect(self._current_changed)
        root.addWidget(self.tree, 1)
        self.info = QLabel("")
        self.info.setWordWrap(True)
        self.info.setObjectName("GraphLibraryInfo")
        root.addWidget(self.info)
        self.buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        self.buttons.button(QDialogButtonBox.Ok).setText("Select plot")
        self.buttons.button(QDialogButtonBox.Ok).setToolTip("Stage this graph in the main Graph Library. It is not rendered until you press the ▶ Apply button there.")
        self.buttons.button(QDialogButtonBox.Ok).setEnabled(False)
        self.buttons.accepted.connect(self._accept_current)
        self.buttons.rejected.connect(self.reject)
        root.addWidget(self.buttons)
        self._refresh()

    @staticmethod
    def _preview_icon(entry):
        """Return a cached, lazily decoded preview icon.

        Using ``QIcon(path)`` avoids synchronously decoding hundreds of PNGs
        whenever the graph tree is rebuilt.  Qt decodes the image only when a
        view actually needs to paint it, while the process-wide cache prevents
        repeated filesystem work during dataset/theme changes.
        """
        try:
            from graphvis.rendering.graph_library import preview_asset_path
            original = preview_asset_path(entry)
            app = QApplication.instance()
            compact = app.property("graphvis_compact_previews") if app is not None else True
            merge = app.property("graphvis_merge_previews") if app is not None else True
            if merge is not False:
                candidate = ASSETS_DIR / "graph_previews_merged" / original.name
            elif compact is not False:
                candidate = ASSETS_DIR / "graph_previews_compact" / original.name
            else:
                candidate = original
            path = candidate if candidate.exists() else original
            if path.exists():
                key = str(path)
                icon = GraphLibraryDialog._ICON_CACHE.get(key)
                if icon is None:
                    icon = QIcon(key)
                    GraphLibraryDialog._ICON_CACHE[key] = icon
                return icon
        except Exception:
            pass
        key = "__graphvis_preview_fallback__"
        icon = GraphLibraryDialog._ICON_CACHE.get(key)
        if icon is not None:
            return icon
        pix = QPixmap(112, 64)
        app = QApplication.instance()
        merge = app.property("graphvis_merge_previews") if app is not None else True
        pix.fill(Qt.transparent if merge is not False else QColor("#FFFFFF"))
        p = QPainter(pix); p.setRenderHint(QPainter.Antialiasing)
        p.setPen(QPen(QColor("#D5DBDB"), 1)); p.drawRect(0, 0, 111, 63)
        p.setPen(QPen(QColor("#2980B9"), 2))
        pts=[QPoint(5,52),QPoint(22,42),QPoint(42,46),QPoint(62,24),QPoint(84,32),QPoint(106,11)]
        p.drawPolyline(QPolygon(pts)); p.end()
        icon = QIcon(pix); GraphLibraryDialog._ICON_CACHE[key] = icon
        return icon

    def _refresh(self):
        self.tree.clear()
        query = self.search.text().strip()
        entries = self._search_entries(query, include_advanced=self.advanced.isChecked())
        cats = {}
        for entry in entries:
            cat = entry["category"]
            if cat not in cats:
                parent = QTreeWidgetItem([cat, "", ""])
                parent.setFirstColumnSpanned(True)
                f = parent.font(0); f.setBold(True); parent.setFont(0, f)
                self.tree.addTopLevelItem(parent)
                cats[cat] = parent
            engine = entry["engine"]
            if engine is None:
                ok, why = False, "Backend/data model not available"
            else:
                ok, why = self._caps.get(engine, (True, ""))
            item = QTreeWidgetItem([entry["name"], entry["preview"], "Ready" if ok else (why or "Unavailable")])
            item.setIcon(1, self._preview_icon(entry))
            item.setData(0, Qt.UserRole, entry)
            tip = (f"<b>{entry['name']}</b><br>{entry['description']}<br><br>"
                   f"Preview: <code>{entry['preview']}</code><br>"
                   f"GraphVis engine: {engine or 'not implemented'}"
                   + (f"<br><span style='color:#C0392B'>{why}</span>" if not ok and why else ""))
            for c in range(3):
                item.setToolTip(c, tip)
            if not ok:
                item.setForeground(0, QColor("#8A99A8"))
            cats[cat].addChild(item)
        for i in range(self.tree.topLevelItemCount()):
            self.tree.topLevelItem(i).setExpanded(False)
        self.buttons.button(QDialogButtonBox.Ok).setEnabled(False)
        self.info.setText(f"{len(entries)} matching plot definitions")

    def _current_changed(self, item, _prev):
        entry = item.data(0, Qt.UserRole) if item else None
        self.selected_entry = entry if isinstance(entry, dict) else None
        self.buttons.button(QDialogButtonBox.Ok).setEnabled(self.selected_entry is not None)
        if self.selected_entry:
            e = self.selected_entry
            self.info.setText(f"<b>{e['name']}</b> — {e['description']} &nbsp; <code>{e['preview']}</code>")

    def _accept_current(self):
        item = self.tree.currentItem()
        entry = item.data(0, Qt.UserRole) if item else None
        if isinstance(entry, dict):
            self.selected_entry = entry
            self.accept()


# ---------------------------------------------------------- InstructionsDialog
class InstructionsDialog(QDialog):
    """Searchable GraphVis guide generated from the live graph catalogue.

    The catalogue itself is the source of truth, so every graph exposed by the
    picker automatically receives a help entry instead of relying on a second
    hand-maintained list that can become stale.
    """

    TOPICS = [
        ("Getting a graph to render", "Start by selecting a dataset and a graph. Use the blue play/Auto Render button in the graph toolbar when you do not know which mappings the graph needs. GraphVis will select compatible variables for the required X/Y/Z or matrix slots and then render. If the active dataset cannot satisfy the graph, the message explains what is missing."),
        ("Auto Render play button", "The play-shaped Auto Render control is different from ordinary Preview. Preview renders exactly the mappings currently shown. Auto Render first checks the selected chart's variable roles, re-maps required axes to compatible data, uses cached Smart Suite recommendations when available, and then renders."),
        ("Variable mapping", "X/Y/Z are the primary axes. W is normally colour, V is normally marker size/opacity/geometry, Matrix/volume is for array data, and Gradient is used by vector/field overlays. Disabled mapping boxes are not used by the selected graph."),
        ("Surface, contour and heatmap graphs", "Tabular surface-style plots normally need three distinct numeric variables: X and Y locate samples and Z is the response. GraphVis interpolates the irregular samples to a grid. Increase grid resolution only when needed; high smoothing values can remove real structure."),
        ("Vector fields and stream plots", "Vector/stream plots need spatial coordinates plus field information. For GraphVis scalar-field workflows, X/Y locate the field and Z (or Gradient) supplies the field used to derive/display direction. Some specialised 3-D vector plots need more independent components; Auto Render will fill every role that the active renderer exposes."),
        ("Matrix and volume plots", "Matrix, volume, slice, isosurface and related charts need array-like data rather than only unrelated scalar columns. Choose a Matrix/volume mapping when one is available. If the dataset contains only a flat table, GraphVis will not invent a 3-D volume because that could be scientifically misleading."),
        ("Function and implicit graphs", "Function plots do not require a dataset. Enter expressions such as sin(x), x^2+y^2, or x^2+y^2+z^2=1 and set the domain. Auto Render keeps the built-in safe example when the expression is empty."),
        ("Markers and exact coordinates", "Single-click inspects the exact 2-D axis coordinate under the cursor. Double-click creates a permanent marker; double-click the marker again to delete it. Right-click always exposes marker deletion commands. On 3-D axes, a screen pixel does not map to one unique XYZ point, so GraphVis snaps to the nearest displayed 3-D point and reports that explicitly."),
        ("Smoothing", "Gaussian smoothing sigma is available from 0.1 to 100. Large values are intentionally allowed but can heavily flatten structure and can increase processing time on large grids. Keep smoothing low when quantitative local features matter."),
        ("Smart Suite and literature scans", "Smart Suite can use long time budgets and saves reusable checkpoints. Literature extraction also caches recovered text. Re-running with a larger budget can continue from saved work rather than repeating every completed scan step."),
        ("Graph background", "View > Graph background controls only the scientific canvas. Light, White, Dark, Theme and Custom modes have independent brightness and optional theme blending. Axis/tick colours are adjusted for contrast; publication export styling remains separate."),
        ("Docking and resizing", "Graph Controls and Interactive Controls can be popped out and docked to a main-window edge. Main sidebar and toolbar docks use deferred/low-redraw paths so moving them should not repeatedly trigger full Matplotlib renders."),
        ("High-DPI export", "Raster export accepts any positive DPI typed by the user. GraphVis warns about estimated memory and uncompressed image size at extreme settings but does not impose a 3000-DPI ceiling. Vector PDF/SVG/EPS is usually better for line art."),
        ("Errors, crashes and debug reports", "Runtime diagnostics are stored in Documents/GraphVis/Logs/Debug. Errors and crash reports are stored in Documents/GraphVis/Logs/Errors. A session journal records the active operation; an unclean shutdown is converted to an unexpected-exit report on the next launch."),
    ]

    def __init__(self, initial_query: str = "", parent=None):
        super().__init__(parent)
        self.setWindowTitle("GraphVis Instructions & Graph Guide")
        self.resize(980, 720)
        self._records = self._build_records()

        root = QVBoxLayout(self)
        intro = QLabel("Search GraphVis features or any graph in the catalogue. Each graph entry shows what it does and which mappings it consumes.")
        intro.setWordWrap(True)
        root.addWidget(intro)
        self.search = QLineEdit()
        self.search.setPlaceholderText("Search instructions, graph names, categories, mappings...")
        self.search.setClearButtonEnabled(True)
        self.search.textChanged.connect(self._refresh)
        root.addWidget(self.search)

        body = QHBoxLayout()
        self.results = QListWidget()
        self.results.setMinimumWidth(300)
        self.results.setMaximumWidth(380)
        self.results.currentItemChanged.connect(self._show_item)
        body.addWidget(self.results)
        self.detail = QTextBrowser()
        self.detail.setOpenExternalLinks(False)
        body.addWidget(self.detail, 1)
        root.addLayout(body, 1)

        buttons = QDialogButtonBox(QDialogButtonBox.Close)
        buttons.rejected.connect(self.reject)
        root.addWidget(buttons)
        self.search.setText(initial_query or "")
        self._refresh(self.search.text())
        self.search.setFocus(Qt.OtherFocusReason)
        self.search.selectAll()

    @classmethod
    def search_terms(cls) -> list[str]:
        from graphvis.rendering.graph_library import GRAPH_LIBRARY
        terms = [title for title, _ in cls.TOPICS]
        terms.extend(str(e.get("name") or e.get("engine") or "") for e in GRAPH_LIBRARY)
        terms.extend(str(e.get("category") or "") for e in GRAPH_LIBRARY)
        return sorted({t for t in terms if t}, key=str.casefold)

    @classmethod
    def _build_records(cls) -> list[dict]:
        from graphvis.rendering.graph_library import GRAPH_LIBRARY
        records = [{"kind": "topic", "title": title, "body": body, "category": "Instructions"}
                   for title, body in cls.TOPICS]
        for entry in GRAPH_LIBRARY:
            records.append({"kind": "graph", "title": str(entry.get("name") or entry.get("engine") or "Graph"),
                            "category": str(entry.get("category") or "Other"), "entry": dict(entry)})
        return records

    @staticmethod
    def _mapping_summary(engine: str) -> str:
        try:
            from graphvis.rendering.render_core import axis_roles
            roles = axis_roles(engine)
        except Exception:
            roles = {}
        used = []
        for key in ("x", "y", "z", "w", "v", "matrix", "gradient"):
            role = roles.get(key)
            if role:
                used.append(f"<b>{html.escape(key.upper())}</b>: {html.escape(str(role))}")
        return "<br>".join(used) if used else "No dataset mapping is required or the renderer chooses data automatically."

    def _record_html(self, rec: dict) -> str:
        title = html.escape(str(rec.get("title", "")))
        category = html.escape(str(rec.get("category", "")))
        if rec.get("kind") == "topic":
            body = html.escape(str(rec.get("body", ""))).replace("\n", "<br>")
            return f"<h2>{title}</h2><p><i>{category}</i></p><p>{body}</p>"
        entry = rec.get("entry") or {}
        engine = str(entry.get("engine") or "Not mapped to a renderer")
        desc = html.escape(str(entry.get("description") or ""))
        scale = entry.get("scale")
        advanced = "Yes" if entry.get("advanced") else "No"
        scale_html = f"<p><b>Preferred scale:</b> {html.escape(str(scale))}</p>" if scale else ""
        return (
            f"<h2>{title}</h2><p><i>{category}</i></p>"
            f"<p>{desc}</p><p><b>Renderer:</b> {html.escape(engine)} &nbsp; <b>Advanced:</b> {advanced}</p>"
            f"{scale_html}<h3>Mappings used</h3><p>{self._mapping_summary(engine)}</p>"
            "<h3>How to start</h3><p>Select this graph in the library, then press the play-shaped <b>Auto Render</b> button if the mapping is unclear. "
            "GraphVis will fill compatible roles from the active dataset and report any data type/dimensionality that is still missing.</p>"
        )

    def _refresh(self, text: str = "") -> None:
        query = str(text or "").strip().casefold()
        self.results.blockSignals(True)
        self.results.clear()
        for rec in self._records:
            hay = " ".join([str(rec.get("title", "")), str(rec.get("category", "")),
                            str((rec.get("entry") or {}).get("description", "")),
                            str((rec.get("entry") or {}).get("engine", "")), str(rec.get("body", ""))]).casefold()
            if query and query not in hay:
                continue
            label = f"{rec.get('title', '')}  -  {rec.get('category', '')}"
            item = QListWidgetItem(label)
            item.setData(Qt.UserRole, rec)
            self.results.addItem(item)
        self.results.blockSignals(False)
        if self.results.count():
            self.results.setCurrentRow(0)
            self._show_item(self.results.currentItem(), None)
        else:
            self.detail.setHtml("<h3>No matching instruction</h3><p>Try a graph name, category, axis role, 'marker', 'surface', 'Smart Suite', or 'export'.</p>")

    def _show_item(self, current, _previous) -> None:
        if current is None:
            return
        rec = current.data(Qt.UserRole)
        if isinstance(rec, dict):
            self.detail.setHtml(self._record_html(rec))



# ---------------------------------------------------------------- GraphPicker
class GraphPicker(QWidget):
    """Inline GraphVis plot picker used by the main control sidebar.

    This deliberately mirrors the compact graph-selection workflow from the
    earlier GraphVis UI: a typo-tolerant search box shows suggestions while the
    user types, and the complete library sits underneath in collapsible
    categories.  The view passes capability information in; the picker never
    owns or mutates scientific data.
    """

    entrySelected = Signal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        from graphvis.rendering.graph_library import GRAPH_LIBRARY
        self._entries = list(GRAPH_LIBRARY)
        self._advanced = True
        self._caps: dict[str, tuple[bool, str]] = {}
        self._recommendations: list[str] = []
        self._current_entry: dict | None = None
        self._items: dict[tuple[str, str | None], QTreeWidgetItem] = {}
        app = QApplication.instance()
        self._preview_compact = (app.property("graphvis_compact_previews") if app is not None else True) is not False

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(5)

        self.search = QLineEdit()
        self.search.setPlaceholderText("Search graphs — try 'scater', 'contor', 'violin', 'surface'…")
        self.search.setClearButtonEnabled(True)
        self.search.setMinimumHeight(31)
        self.search.textChanged.connect(self._on_search)
        self.search.returnPressed.connect(self._accept_first_suggestion)
        root.addWidget(self.search)
        self.count_label = QLabel("")
        self.count_label.setWordWrap(True)
        root.addWidget(self.count_label)

        self.suggestions = QListWidget()
        self.suggestions.setMaximumHeight(148)
        self.suggestions.setAlternatingRowColors(True)
        self.suggestions.hide()
        self.suggestions.itemClicked.connect(self._suggestion_clicked)
        self.suggestions.itemEntered.connect(self._hover_suggestion)
        self.suggestions.setMouseTracking(True)
        self.suggestions.viewport().installEventFilter(self)
        root.addWidget(self.suggestions)

        self.tree = QTreeWidget()
        self.tree.setHeaderHidden(True)
        self.tree.setIconSize(QSize(46, 28) if self._preview_compact else QSize(62, 38))
        self.tree.setIndentation(14)
        self.tree.setMinimumHeight(250)
        self.tree.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.tree.setMouseTracking(True)
        self.tree.viewport().setAttribute(Qt.WA_StaticContents, True)
        self.tree.setUniformRowHeights(False)
        self.tree.setAnimated(False)
        self.tree.viewport().installEventFilter(self)
        self.tree.itemClicked.connect(self._tree_clicked)
        self.tree.itemDoubleClicked.connect(self._tree_clicked)
        self.tree.itemEntered.connect(self._hover_item)
        self.tree.itemExpanded.connect(self._populate_category)
        root.addWidget(self.tree, 1)

        self.hover_card = QFrame()
        self.hover_card.setObjectName("GraphHoverCard")
        hc = QHBoxLayout(self.hover_card)
        hc.setContentsMargins(7, 7, 7, 7)
        self.hover_preview = QLabel()
        self.hover_preview.setFixedSize(QSize(126, 78) if self._preview_compact else QSize(150, 92))
        self.hover_preview.setAlignment(Qt.AlignCenter)
        hc.addWidget(self.hover_preview)
        self.hover_text = QLabel()
        self.hover_text.setObjectName("GraphHoverText")
        self.hover_text.setWordWrap(True)
        self.hover_text.setMinimumHeight(72)
        hc.addWidget(self.hover_text, 1)
        self.hover_card.hide()
        root.addWidget(self.hover_card)

        # Selected-graph details are intentionally not pinned below the tree.
        # The richer card appears only while an item is hovered, preserving
        # sidebar space once the pointer moves away.
        self.info = QLabel("")
        self.info.setWordWrap(True)
        self.info.setObjectName("GraphPickerInfo")
        self.info.hide()
        self.rebuild()

    def applyPreviewPreferences(self) -> None:
        app = QApplication.instance()
        compact = (app.property("graphvis_compact_previews") if app is not None else True) is not False
        self._preview_compact = compact
        self.tree.setIconSize(QSize(46, 28) if compact else QSize(62, 38))
        self.hover_preview.setFixedSize(QSize(126, 78) if compact else QSize(150, 92))
        self.rebuild()

    def setAdvanced(self, enabled: bool) -> None:
        enabled = bool(enabled)
        if enabled == self._advanced:
            return
        self._advanced = enabled
        self.rebuild()
        if self.search.text().strip():
            self._on_search(self.search.text())

    def setCapabilities(self, capabilities: dict | None) -> None:
        caps = dict(capabilities or {})
        if caps == self._caps:
            return
        self._caps = caps
        self.rebuild()

    def setRecommendations(self, names: list[str] | tuple[str, ...] | None) -> None:
        recommendations = [str(n) for n in (names or []) if n]
        if recommendations == self._recommendations:
            return
        self._recommendations = recommendations
        self.rebuild()

    def currentEntry(self) -> dict | None:
        return dict(self._current_entry) if self._current_entry else None

    def currentEngine(self) -> str | None:
        return self._current_entry.get("engine") if self._current_entry else None

    def setCurrentEngine(self, engine: str, *, scale: str | None = None) -> None:
        if not engine:
            return
        candidates = [e for e in self._entries if e.get("engine") == engine]
        if scale is not None:
            exact = [e for e in candidates if e.get("scale") == scale]
            if exact:
                candidates = exact
        if not candidates:
            return
        self._select_entry(candidates[0], emit=False)

    def _compatible(self, entry: dict) -> tuple[bool, str]:
        engine = entry.get("engine")
        if not engine:
            return False, "Rendering backend not available"
        return self._caps.get(engine, (True, ""))

    @staticmethod
    def _key(entry: dict) -> tuple[str, str | None]:
        return str(entry.get("name", "")), entry.get("scale")

    @staticmethod
    def _tooltip(entry: dict, reason: str = "") -> str:
        preview = entry.get("preview") or ""
        engine = entry.get("engine") or "not available"
        warning = f"<br><span style='color:#C0392B'><b>{reason}</b></span>" if reason else ""
        return (f"<b>{entry.get('name','')}</b><br>{entry.get('description','')}"
                f"<br><br>Preview: <code>{preview}</code><br>Renderer: {engine}{warning}")

    def rebuild(self) -> None:
        current_key = self._key(self._current_entry) if self._current_entry else None
        self.tree.blockSignals(True)
        try:
            self.tree.clear()
            self._items = {}

            # Recommendation group uses engine names returned by the scientific
            # advisor.  It is intentionally open; all normal categories remain
            # individually collapsible via the native Qt arrow controls.
            recommended_entries: list[dict] = []
            for name in self._recommendations:
                recommended_entries.extend([e for e in self._entries if e.get("name") == name or e.get("engine") == name])
            seen: set[tuple[str, str | None]] = set()
            recommended_entries = [e for e in recommended_entries if not (self._key(e) in seen or seen.add(self._key(e)))]
            if recommended_entries:
                top = QTreeWidgetItem(["★ Recommended for this dataset"])
                font = top.font(0); font.setBold(True); top.setFont(0, font)
                top.setExpanded(False)
                self.tree.addTopLevelItem(top)
                for entry in recommended_entries[:10]:
                    self._add_entry_item(top, entry, recommended=True)

            categories: list[str] = []
            for entry in self._entries:
                cat = str(entry.get("category") or "Other")
                if cat not in categories:
                    categories.append(cat)
            for category in categories:
                entries = [e for e in self._entries if e.get("category") == category and (self._advanced or not e.get("advanced"))]
                if not entries:
                    continue
                top = QTreeWidgetItem([category])
                font = top.font(0); font.setBold(True); top.setFont(0, font)
                # Start compact and lazy: do not create/decode 200+ child rows
                # at application startup when every category is closed.
                top.setExpanded(False)
                top.setData(0, Qt.UserRole + 1, entries)
                dummy = QTreeWidgetItem([""])
                dummy.setDisabled(True)
                top.addChild(dummy)  # preserves the native expansion arrow
                self.tree.addTopLevelItem(top)
                if current_key and any(self._key(e) == current_key for e in entries):
                    self._populate_category(top)

            visible_count = sum(1 for e in self._entries if self._advanced or not e.get("advanced"))
            total_count = len(self._entries)
            suffix = "" if self._advanced else f" • Advanced View reveals all {total_count}"
            self.count_label.setText(f"{visible_count} graph definitions available{suffix}")
            if current_key and current_key in self._items:
                self.tree.setCurrentItem(self._items[current_key])
        finally:
            self.tree.blockSignals(False)

    def _populate_category(self, item: QTreeWidgetItem | None) -> None:
        if item is None:
            return
        entries = item.data(0, Qt.UserRole + 1)
        if not isinstance(entries, list):
            return
        item.setData(0, Qt.UserRole + 1, None)
        item.takeChildren()
        for entry in entries:
            if isinstance(entry, dict):
                self._add_entry_item(item, entry)

    def _ensure_entry_item(self, entry: dict) -> QTreeWidgetItem | None:
        key = self._key(entry)
        existing = self._items.get(key)
        if existing is not None:
            return existing
        for i in range(self.tree.topLevelItemCount()):
            top = self.tree.topLevelItem(i)
            pending = top.data(0, Qt.UserRole + 1)
            if isinstance(pending, list) and any(isinstance(e, dict) and self._key(e) == key for e in pending):
                self._populate_category(top)
                return self._items.get(key)
        return None

    def _add_entry_item(self, parent: QTreeWidgetItem, entry: dict, recommended: bool = False) -> None:
        ok, reason = self._compatible(entry)
        prefix = "★ " if recommended else ""
        item = QTreeWidgetItem([prefix + str(entry.get("name") or entry.get("engine") or "Plot")])
        item.setData(0, Qt.UserRole, entry)
        # Distinct breathing room between plot choices without making the
        # catalogue tall or card-like.
        item.setSizeHint(0, QSize(0, 40 if self._preview_compact else 50))
        item.setToolTip(0, self._tooltip(entry, "" if ok else reason))
        try:
            item.setIcon(0, GraphLibraryDialog._preview_icon(entry))
        except Exception:
            pass
        if not ok:
            item.setForeground(0, QColor("#8A99A8"))
        parent.addChild(item)
        self._items[self._key(entry)] = item

    def _show_hover_entry(self, entry: dict | None) -> None:
        if not isinstance(entry, dict):
            self.hover_card.hide()
            return
        ok, reason = self._compatible(entry)
        try:
            icon = GraphLibraryDialog._preview_icon(entry)
            target = self.hover_preview.size()
            self.hover_preview.setPixmap(icon.pixmap(max(1, target.width() - 2), max(1, target.height() - 2)))
        except Exception:
            self.hover_preview.clear()
        engine = entry.get("engine") or "Not mapped"
        status = "Ready" if ok else (reason or "Unavailable for current data")
        self.hover_text.setText(
            f"<b>{entry.get('name','')}</b> &nbsp; <span style='opacity:0.7'>{entry.get('category','')}</span><br>"
            f"{entry.get('description','')}<br>"
            f"<small>Renderer: {engine} &nbsp; • &nbsp; {status}</small>"
        )
        self.hover_card.show()

    def _hover_item(self, item: QTreeWidgetItem | None, *_args) -> None:
        self._show_hover_entry(item.data(0, Qt.UserRole) if item is not None else None)

    def _hover_suggestion(self, item: QListWidgetItem | None) -> None:
        self._show_hover_entry(item.data(Qt.UserRole) if item is not None else None)

    def _tree_clicked(self, item: QTreeWidgetItem | None, *_args) -> None:
        if item is None:
            return
        entry = item.data(0, Qt.UserRole)
        if isinstance(entry, dict):
            self._select_entry(entry, emit=True)

    def _suggestion_clicked(self, item: QListWidgetItem) -> None:
        entry = item.data(Qt.UserRole)
        if isinstance(entry, dict):
            self._select_entry(entry, emit=True)
            self.search.clear()

    def _select_entry(self, entry: dict, *, emit: bool) -> None:
        ok, reason = self._compatible(entry)
        self._current_entry = dict(entry)
        # Do not pin descriptive content after selection; hover owns details.
        # Selection and hover are intentionally independent: the large preview
        # card is ephemeral and disappears as soon as the pointer leaves the
        # graph-library list.
        item = self._ensure_entry_item(entry)
        if item is not None:
            self.tree.blockSignals(True)
            parent = item.parent()
            if parent is not None:
                parent.setExpanded(True)
            self.tree.clearSelection()
            self.tree.setCurrentItem(item)
            item.setSelected(True)
            self.tree.scrollToItem(item, QAbstractItemView.PositionAtCenter)
            self.tree.blockSignals(False)
            self.suggestions.hide()
            self.tree.setFocus(Qt.OtherFocusReason)
        if emit:
            self.entrySelected.emit(dict(entry))

    def _accept_first_suggestion(self) -> None:
        """Enter in the fuzzy-search field selects the top suggestion."""
        if self.suggestions.count() <= 0:
            return
        item = self.suggestions.item(0)
        if item is not None:
            self._suggestion_clicked(item)

    def _on_search(self, text: str) -> None:
        from graphvis.rendering.graph_library import search_entries
        query = text.strip()
        self.suggestions.clear()
        if not query:
            self.suggestions.hide()
            self.hover_card.hide()
            return
        hits = search_entries(query, include_advanced=self._advanced)[:12]
        for entry in hits:
            ok, reason = self._compatible(entry)
            label = f"{entry.get('name','')}  —  {str(entry.get('description',''))[:72]}"
            if not ok:
                label += f"  ({reason})"
            item = QListWidgetItem(label)
            item.setData(Qt.UserRole, entry)
            item.setToolTip(self._tooltip(entry, "" if ok else reason))
            if not ok:
                item.setForeground(QColor("#8A99A8"))
            self.suggestions.addItem(item)
        self.suggestions.setVisible(self.suggestions.count() > 0)

    def eventFilter(self, obj, event):
        # Do not leave the comparatively large preview/details card pinned to
        # the sidebar after the cursor has left the graph list.  This also
        # reduces repaints while the user is working elsewhere in the UI.
        if obj in (self.tree.viewport(), self.suggestions.viewport()):
            if event.type() == QEvent.Leave:
                self.hover_card.hide()
        return super().eventFilter(obj, event)

    def leaveEvent(self, event) -> None:
        self.hover_card.hide()
        super().leaveEvent(event)

# ---------------------------------------------------------------- ColorButton
class ColorButton(QPushButton):
    colorChanged = Signal(str)

    def __init__(self, color="#2980B9", title="Select colour", parent=None):
        super().__init__(parent)
        self._title = title
        self.setMinimumHeight(28)
        self.setColor(color, emit=False)
        self.clicked.connect(self._pick)

    def color(self) -> str:
        return self._color

    def setColor(self, color: str, emit=True):
        self._color = QColor(color).name() if QColor(color).isValid() else "#000000"
        c = QColor(self._color)
        fg = "#FFFFFF" if (0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue()) < 150 else "#000000"
        self.setText(self._color.upper())
        self.setStyleSheet(f"QPushButton{{background:{self._color};color:{fg};border:1px solid #95A5A6;border-radius:4px;font-weight:600;}}")
        if emit:
            self.colorChanged.emit(self._color)

    def _pick(self):
        c = QColorDialog.getColor(QColor(self._color), self, self._title)
        if c.isValid():
            self.setColor(c.name())


# ------------------------------------------------------------ SidebarSplitter
class _ToggleHandle(QSplitterHandle):
    """Wide splitter handle with a tap target that collapses/expands the sidebar."""

    def __init__(self, orientation, parent):
        super().__init__(orientation, parent)
        self._hover = False
        self.setMouseTracking(True)

    def _button_rect(self):
        w, h = self.width(), self.height()
        return QRect(0, max(h // 2 - 28, 0), w, 56)

    def paintEvent(self, ev):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        pal = self.palette()
        bg = pal.color(QPalette.Midlight if not self._hover else QPalette.Light)
        accent = pal.color(QPalette.Highlight)
        accent_text = pal.color(QPalette.HighlightedText)
        p.fillRect(self.rect(), bg)
        br = self._button_rect()
        p.setBrush(QBrush(accent))
        p.setPen(Qt.NoPen)
        p.drawRoundedRect(br.adjusted(1, 0, -1, 0), 4, 4)
        p.setPen(QPen(accent_text, 2))
        cx, cy = br.center().x(), br.center().y()
        collapsed = self.splitter().isSidebarCollapsed()
        d = 4 if collapsed else -4
        p.drawLine(cx - d // 2 - 1, cy - 6, cx + d // 2 + 1, cy)
        p.drawLine(cx + d // 2 + 1, cy, cx - d // 2 - 1, cy + 6)
        # grip dots
        p.setPen(QPen(pal.color(QPalette.Mid), 2))
        for off in (-40, -32, 32, 40):
            p.drawPoint(self.width() // 2, cy + off)

    def enterEvent(self, ev):
        self._hover = True
        self.update()

    def leaveEvent(self, ev):
        self._hover = False
        self.update()

    def mousePressEvent(self, ev):
        if self._button_rect().contains(ev.position().toPoint()):
            self._toggle_pending = True
            ev.accept()
            return
        self._toggle_pending = False
        super().mousePressEvent(ev)

    def mouseReleaseEvent(self, ev):
        if getattr(self, "_toggle_pending", False) and self._button_rect().contains(ev.position().toPoint()):
            self._toggle_pending = False
            self.splitter().toggleSidebar()
            ev.accept()
            return
        self._toggle_pending = False
        super().mouseReleaseEvent(ev)


class SidebarSplitter(QSplitter):
    """Horizontal splitter with a stable, bounded control sidebar.

    Dragging is intentionally constrained so the sidebar cannot accidentally
    become a few pixels wide or consume nearly the entire application window.
    Full collapse is still available through the blue handle button / Ctrl+B.
    """

    sidebarToggled = Signal(bool)
    MIN_SIDEBAR_WIDTH = 320
    DEFAULT_SIDEBAR_WIDTH = 420
    MAX_SIDEBAR_WIDTH = 680
    MAX_SIDEBAR_FRACTION = 0.48

    def __init__(self, parent=None):
        super().__init__(Qt.Horizontal, parent)
        self._last_sidebar_width = self.DEFAULT_SIDEBAR_WIDTH
        self._collapsed = False
        self._adjusting = False
        self.setHandleWidth(14)
        # QSplitter itself may technically collapse a child, but ordinary drag
        # motion is clamped by _on_moved. This keeps the dedicated toggle usable.
        self.setChildrenCollapsible(True)
        # Match the bottom graph splitter: move a lightweight rubber band while
        # dragging and apply the expensive sidebar/canvas relayout once on
        # release.  This avoids repeated Matplotlib exposure/repaint work and
        # the white flashing seen during horizontal live resize.
        self.setOpaqueResize(False)
        self.splitterMoved.connect(self._on_moved)

    def createHandle(self):
        return _ToggleHandle(self.orientation(), self)

    def _maximum_sidebar_width(self) -> int:
        total = max(sum(self.sizes()), self.width(), 1)
        fraction_limit = int(total * self.MAX_SIDEBAR_FRACTION)
        return max(self.MIN_SIDEBAR_WIDTH, min(self.MAX_SIDEBAR_WIDTH, fraction_limit))

    def _clamp_width(self, width: int) -> int:
        return max(self.MIN_SIDEBAR_WIDTH, min(int(width), self._maximum_sidebar_width()))

    def isSidebarCollapsed(self) -> bool:
        sizes = self.sizes()
        return self._collapsed or (bool(sizes) and sizes[0] <= 1)

    def currentSidebarWidth(self) -> int:
        if self.isSidebarCollapsed():
            return int(self._last_sidebar_width)
        sizes = self.sizes()
        return int(sizes[0]) if sizes else int(self._last_sidebar_width)

    def restoreSidebar(self, width: int | None = None, collapsed: bool = False) -> None:
        """Restore a persistent width while safely ignoring pathological values."""
        requested = self.DEFAULT_SIDEBAR_WIDTH if width is None else int(width)
        self._last_sidebar_width = self._clamp_width(requested)
        # Use a large relative second pane so this also behaves before show().
        self._adjusting = True
        try:
            if collapsed:
                self._collapsed = True
                self.setSizes([0, 1260])
            else:
                self._collapsed = False
                self.setSizes([self._last_sidebar_width, 1260])
        finally:
            self._adjusting = False
        self.handle(1).update()

    def resetSidebarWidth(self) -> None:
        """Return the sidebar to a comfortable default width."""
        self._last_sidebar_width = self._clamp_width(self.DEFAULT_SIDEBAR_WIDTH)
        self._collapsed = False
        total = max(sum(self.sizes()), self.width(), 900)
        self._adjusting = True
        try:
            self.setSizes([self._last_sidebar_width, max(total - self._last_sidebar_width, 400)])
        finally:
            self._adjusting = False
        self.handle(1).update()
        self.sidebarToggled.emit(True)

    def _on_moved(self, pos, index):
        if self._adjusting:
            return
        sizes = self.sizes()
        if not sizes:
            return
        # A user drag is never allowed to create an unusably small/large panel.
        width = self._clamp_width(sizes[0])
        if sizes[0] != width:
            total = max(sum(sizes), width + 200)
            self._adjusting = True
            try:
                self.setSizes([width, max(total - width, 200)])
            finally:
                self._adjusting = False
        self._collapsed = False
        self._last_sidebar_width = width
        self.handle(1).update()

    def toggleSidebar(self):
        self.setSidebarCollapsed(not self.isSidebarCollapsed())

    def setSidebarCollapsed(self, collapsed: bool):
        sizes = self.sizes()
        if len(sizes) < 2:
            return
        total = max(sum(sizes), self.width(), 900)
        self._adjusting = True
        try:
            if collapsed:
                if sizes[0] > 1:
                    self._last_sidebar_width = self._clamp_width(sizes[0])
                self._collapsed = True
                self.setSizes([0, total])
            else:
                self._collapsed = False
                width = self._clamp_width(self._last_sidebar_width)
                self._last_sidebar_width = width
                self.setSizes([width, max(total - width, 200)])
        finally:
            self._adjusting = False
        self.handle(1).update()
        self.sidebarToggled.emit(not collapsed)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self.isSidebarCollapsed() or self._adjusting:
            return
        sizes = self.sizes()
        if not sizes:
            return
        width = self._clamp_width(sizes[0])
        if width != sizes[0]:
            total = max(sum(sizes), self.width(), width + 200)
            self._adjusting = True
            try:
                self.setSizes([width, max(total - width, 200)])
            finally:
                self._adjusting = False
            self._last_sidebar_width = width


# ------------------------------------------------------------- misc helpers
def hline():
    f = QFrame()
    f.setFrameShape(QFrame.HLine)
    f.setFrameShadow(QFrame.Sunken)
    return f


class StatusPill(QLabel):
    """Small coloured status indicator for the status bar."""

    def __init__(self, text="", color="#27AE60", parent=None):
        super().__init__(text, parent)
        self.set_state(text, color)

    def set_state(self, text, color):
        self.setText(f"  {text}  ")
        self.setStyleSheet(f"background:{color};color:white;border-radius:8px;font-weight:600;padding:1px 4px;")
