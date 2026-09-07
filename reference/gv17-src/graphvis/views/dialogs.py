"""Reusable Qt View dialogs for the GraphVis MVC UI layer."""
from __future__ import annotations

from dataclasses import asdict
from typing import Iterable

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox, QDoubleSpinBox,
                               QFormLayout, QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem,
                               QPushButton, QSpinBox, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget)

from graphvis.core.publication import PublicationProfile


class ParameterNicknameDialog(QDialog):
    def __init__(self, raw_variables: Iterable[str], existing: dict[str, str], parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Parameter nicknames")
        self.resize(720, 560)
        root = QVBoxLayout(self)
        info = QLabel("Nicknames are non-destructive. Plot axes use <b>Nickname (original_parameter)</b>; raw column names remain unchanged.")
        info.setWordWrap(True); root.addWidget(info)
        variables = list(dict.fromkeys(map(str, raw_variables)))
        self.table = QTableWidget(len(variables), 2)
        self.table.setHorizontalHeaderLabels(["Raw parameter", "Nickname"])
        self.table.horizontalHeader().setStretchLastSection(True)
        for r, raw in enumerate(variables):
            a = QTableWidgetItem(raw); a.setFlags(a.flags() & ~Qt.ItemIsEditable)
            self.table.setItem(r, 0, a)
            self.table.setItem(r, 1, QTableWidgetItem(existing.get(raw, "")))
        root.addWidget(self.table, 1)
        buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept); buttons.rejected.connect(self.reject); root.addWidget(buttons)

    def mapping(self) -> dict[str, str]:
        out: dict[str, str] = {}
        for r in range(self.table.rowCount()):
            raw = self.table.item(r, 0).text().strip()
            nick_item = self.table.item(r, 1)
            nick = nick_item.text().strip() if nick_item else ""
            if nick:
                out[raw] = nick
        return out


class PublicationProfileDialog(QDialog):
    def __init__(self, profile: PublicationProfile | None = None, parent=None) -> None:
        super().__init__(parent)
        p = profile or PublicationProfile("Journal 1")
        self.setWindowTitle("Publication profile")
        self.resize(520, 560)
        form = QFormLayout(self)
        self.name = QLineEdit(p.name)
        self.dpi = QLineEdit(str(max(1, int(p.dpi))))
        self.dpi.setPlaceholderText("Any positive DPI")
        self.dpi.setToolTip("No fixed DPI ceiling. Extremely large raster outputs may consume substantial RAM and disk space.")
        self.font = QLineEdit(p.font_family)
        self.base = QSpinBox(); self.base.setRange(5, 36); self.base.setValue(p.base_font_size)
        self.title = QSpinBox(); self.title.setRange(5, 44); self.title.setValue(p.title_size)
        self.axis = QSpinBox(); self.axis.setRange(5, 36); self.axis.setValue(p.axis_label_size)
        self.tick = QSpinBox(); self.tick.setRange(5, 30); self.tick.setValue(p.tick_size)
        self.legend = QSpinBox(); self.legend.setRange(5, 30); self.legend.setValue(p.legend_size)
        self.line = QDoubleSpinBox(); self.line.setRange(0.2, 8.0); self.line.setSingleStep(0.1); self.line.setValue(p.line_width)
        self.marker = QDoubleSpinBox(); self.marker.setRange(0.5, 20.0); self.marker.setValue(p.marker_size)
        self.margin = QDoubleSpinBox(); self.margin.setRange(0.0, 0.5); self.margin.setSingleStep(0.01); self.margin.setValue(p.margin_padding)
        self.width = QDoubleSpinBox(); self.width.setRange(1.0, 20.0); self.width.setValue(p.figure_width_in); self.width.setSuffix(" in")
        self.height = QDoubleSpinBox(); self.height.setRange(1.0, 20.0); self.height.setValue(p.figure_height_in); self.height.setSuffix(" in")
        self.space = QComboBox(); self.space.addItems(["RGB", "CMYK"]); self.space.setCurrentText(p.color_space)
        self.grid = QCheckBox("Show grid"); self.grid.setChecked(p.grid_visible)
        self.notes = QLineEdit(p.notes)
        for label, widget in (("Profile name:", self.name), ("DPI:", self.dpi), ("Font family:", self.font),
                              ("Base font size:", self.base), ("Title size:", self.title), ("Axis-label size:", self.axis),
                              ("Tick size:", self.tick), ("Legend size:", self.legend), ("Line width [pt]:", self.line),
                              ("Marker size [pt]:", self.marker), ("Margin padding:", self.margin),
                              ("Figure width:", self.width), ("Figure height:", self.height), ("Color space:", self.space)):
            form.addRow(label, widget)
        form.addRow("", self.grid); form.addRow("Notes:", self.notes)
        buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept); buttons.rejected.connect(self.reject); form.addRow(buttons)

    def profile(self) -> PublicationProfile:
        try:
            dpi = max(1, int(self.dpi.text().strip()))
        except Exception:
            dpi = 300
        return PublicationProfile(name=self.name.text().strip() or "Journal 1", dpi=dpi,
            font_family=self.font.text().strip() or "Arial", base_font_size=self.base.value(), title_size=self.title.value(),
            axis_label_size=self.axis.value(), tick_size=self.tick.value(), legend_size=self.legend.value(),
            line_width=self.line.value(), marker_size=self.marker.value(), margin_padding=self.margin.value(),
            color_space=self.space.currentText(), figure_width_in=self.width.value(), figure_height_in=self.height.value(),
            grid_visible=self.grid.isChecked(), notes=self.notes.text().strip()).normalized()


class LayerManagerDialog(QDialog):
    def __init__(self, dataset_names: Iterable[str], selected: Iterable[str], parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Layer manager")
        self.resize(420, 480)
        root = QVBoxLayout(self)
        root.addWidget(QLabel("Toggle datasets/layers included in the active graph:"))
        self.list = QListWidget(); selected_set = set(selected)
        for name in dataset_names:
            item = QListWidgetItem(str(name)); item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
            item.setCheckState(Qt.Checked if name in selected_set else Qt.Unchecked)
            self.list.addItem(item)
        root.addWidget(self.list, 1)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept); buttons.rejected.connect(self.reject); root.addWidget(buttons)

    def active_names(self) -> list[str]:
        return [self.list.item(i).text() for i in range(self.list.count()) if self.list.item(i).checkState() == Qt.Checked]

class AnnotationManagerDialog(QDialog):
    """Editable annotation/layer list with persistent active/inactive state."""

    def __init__(self, annotations: list[dict], parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Annotation manager")
        self.resize(780, 520)
        self._annotations = [dict(a) for a in annotations]
        root = QVBoxLayout(self)
        info = QLabel(
            "Annotations stay in the native figure even when inactive. On-canvas: drag to move; "
            "Shift-drag shape handles to resize; Ctrl-drag to rotate. Right-click for styling."
        )
        info.setWordWrap(True); root.addWidget(info)
        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(["Active", "Type", "Text", "Coordinates", "Rotation"])
        self.table.horizontalHeader().setStretchLastSection(True)
        root.addWidget(self.table, 1)
        row = QHBoxLayout()
        for label, kind in (("+ Text", "text"), ("+ Callout", "callout"), ("+ Arrow", "arrow"),
                            ("+ Bracket", "bracket"), ("+ Rectangle", "rectangle"), ("+ Ellipse", "ellipse")):
            b = QPushButton(label); b.clicked.connect(lambda _=False, k=kind: self._add(k)); row.addWidget(b)
        b_del = QPushButton("Delete selected"); b_del.clicked.connect(self._delete_selected); row.addWidget(b_del)
        row.addStretch(1); root.addLayout(row)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self._accept_changes); buttons.rejected.connect(self.reject); root.addWidget(buttons)
        self._reload()

    def _default_annotation(self, kind: str) -> dict:
        ann = {"kind": kind, "active": True, "text": "Annotation" if kind in ("text", "callout") else "",
               "x": 0.5, "y": 0.5, "coords": "axes", "rotation": 0.0,
               "color": "#2C3E50", "fontsize": 10.0, "background": True,
               "background_color": "#FFF3B0", "background_alpha": 0.72,
               "pointer_style": "->"}
        if kind in ("arrow", "bracket", "rectangle", "ellipse", "callout"):
            ann.update({"x2": 0.72, "y2": 0.68})
        return ann

    def _reload(self) -> None:
        self.table.setRowCount(len(self._annotations))
        for r, ann in enumerate(self._annotations):
            chk = QCheckBox(); chk.setChecked(bool(ann.get("active", True)))
            holder = QWidget(); lay = QHBoxLayout(holder); lay.setContentsMargins(8, 0, 0, 0); lay.addWidget(chk); lay.addStretch(1)
            self.table.setCellWidget(r, 0, holder)
            typ = QTableWidgetItem(str(ann.get("kind", "text"))); typ.setFlags(typ.flags() & ~Qt.ItemIsEditable)
            self.table.setItem(r, 1, typ)
            self.table.setItem(r, 2, QTableWidgetItem(str(ann.get("text", ""))))
            coords = QComboBox(); coords.addItems(["axes", "data"]); coords.setCurrentText(str(ann.get("coords", "axes")))
            self.table.setCellWidget(r, 3, coords)
            rot = QDoubleSpinBox(); rot.setRange(-360.0, 360.0); rot.setValue(float(ann.get("rotation", 0.0))); rot.setSuffix("°")
            self.table.setCellWidget(r, 4, rot)

    def _sync_from_table(self) -> None:
        for r, ann in enumerate(self._annotations):
            holder = self.table.cellWidget(r, 0)
            chk = holder.findChild(QCheckBox) if holder is not None else None
            ann["active"] = bool(chk.isChecked()) if chk else True
            txt = self.table.item(r, 2); ann["text"] = txt.text() if txt else ""
            coords = self.table.cellWidget(r, 3); ann["coords"] = coords.currentText() if isinstance(coords, QComboBox) else "axes"
            rot = self.table.cellWidget(r, 4); ann["rotation"] = float(rot.value()) if isinstance(rot, QDoubleSpinBox) else 0.0

    def _add(self, kind: str) -> None:
        self._sync_from_table(); self._annotations.append(self._default_annotation(kind)); self._reload(); self.table.selectRow(len(self._annotations) - 1)

    def _delete_selected(self) -> None:
        rows = sorted({i.row() for i in self.table.selectedIndexes()}, reverse=True)
        self._sync_from_table()
        for r in rows:
            if 0 <= r < len(self._annotations): self._annotations.pop(r)
        self._reload()

    def _accept_changes(self) -> None:
        self._sync_from_table(); self.accept()

    def annotations(self) -> list[dict]:
        return [dict(a) for a in self._annotations]
