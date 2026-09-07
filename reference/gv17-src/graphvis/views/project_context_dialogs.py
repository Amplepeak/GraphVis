from __future__ import annotations

import os
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QAbstractItemView, QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFormLayout,
                               QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem, QMessageBox,
                               QPlainTextEdit, QPushButton, QSplitter, QVBoxLayout, QWidget)

from graphvis.core.project_context import ProjectContextStore, ProjectGroup


def _check_item(text: str, value: str, checked: bool) -> QListWidgetItem:
    item = QListWidgetItem(text)
    item.setData(Qt.UserRole, value)
    item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
    item.setCheckState(Qt.Checked if checked else Qt.Unchecked)
    item.setToolTip(str(value))
    return item


class ProjectGroupsDialog(QDialog):
    """Edit named groups linking literature, datasets and scripts."""
    def __init__(self, store: ProjectContextStore, dataset_names: list[str], parent=None):
        super().__init__(parent)
        self.store = store
        self.dataset_names = sorted(set(map(str, dataset_names)), key=str.lower)
        self.setWindowTitle("Project groups — literature + datasets + scripts")
        self.resize(940, 680)
        root = QVBoxLayout(self)
        info = QLabel(
            "A project group tells GraphVis which thesis/papers, datasets and analysis scripts belong together. "
            "Smart Map Suite uses the active group as project context. The same file may be checked in any number of groups; "
            "group membership never moves, duplicates or deletes the underlying file."
        )
        info.setWordWrap(True); root.addWidget(info)

        top = QHBoxLayout()
        top.addWidget(QLabel("Group:"))
        self.cb_group = QComboBox(); self.cb_group.setEditable(True); self.cb_group.setMinimumWidth(300)
        top.addWidget(self.cb_group, 1)
        self.btn_new = QPushButton("New")
        self.btn_delete = QPushButton("Delete")
        top.addWidget(self.btn_new); top.addWidget(self.btn_delete)
        root.addLayout(top)

        splitter = QSplitter(Qt.Horizontal)
        self.lit_list = self._column("Literature / thesis",
                                     "Project documents that provide scientific context. "
                                     "Double-click a paper to edit its profile tags; hover for its cross-links.")
        self.data_list = self._column("Datasets", "Project datasets that belong to this study/group")
        self.script_list = self._column("Scripts / parameter files", "MATLAB, Python, R, Julia or text analysis instructions")
        splitter.addWidget(self.lit_list.parentWidget())
        splitter.addWidget(self.data_list.parentWidget())
        splitter.addWidget(self.script_list.parentWidget())
        splitter.setSizes([310, 310, 310]); root.addWidget(splitter, 1)

        self.notes = QPlainTextEdit(); self.notes.setPlaceholderText("Optional study notes, objectives, response variables, experimental conditions…")
        self.notes.setMaximumHeight(115)
        root.addWidget(QLabel("Project/group notes:")); root.addWidget(self.notes)

        buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Close)
        buttons.button(QDialogButtonBox.Save).setText("Save & make active")
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)
        root.addWidget(buttons)

        self.cb_group.currentTextChanged.connect(self._load_current)
        self.btn_new.clicked.connect(self._new_group)
        self.btn_delete.clicked.connect(self._delete_group)
        self.lit_list.itemDoubleClicked.connect(self._edit_literature_tags)
        self._refresh_names()

    def _column(self, title: str, help_text: str) -> QListWidget:
        holder = QWidget(); lay = QVBoxLayout(holder); lay.setContentsMargins(3,3,3,3)
        label = QLabel(f"<b>{title}</b><br><span style='font-size:9pt'>{help_text}</span>")
        label.setWordWrap(True); lay.addWidget(label)
        tools = QHBoxLayout()
        all_btn = QPushButton("All"); none_btn = QPushButton("None")
        all_btn.setToolTip(f"Include every available {title.lower()} item in this group")
        none_btn.setToolTip(f"Remove every {title.lower()} item from this group only")
        tools.addWidget(all_btn); tools.addWidget(none_btn); tools.addStretch(1); lay.addLayout(tools)
        lst = QListWidget(holder); lst.setSelectionMode(QAbstractItemView.NoSelection); lay.addWidget(lst, 1)
        all_btn.clicked.connect(lambda _=False, l=lst: self._set_all(l, True))
        none_btn.clicked.connect(lambda _=False, l=lst: self._set_all(l, False))
        return lst

    def _refresh_names(self):
        names = [g.name for g in self.store.groups()]
        active = self.store.active_group_name or (names[0] if names else "Project Overview")
        self.cb_group.blockSignals(True); self.cb_group.clear(); self.cb_group.addItems(names)
        self.cb_group.setEditText(active); self.cb_group.blockSignals(False)
        self._load_current(active)

    def _available_literature(self):
        root = self.store.project.literature_dir
        return [str(p) for p in sorted(root.glob("**/*")) if p.is_file() and not p.name.endswith(".graphvis.part")]

    def _available_scripts(self):
        root = self.store.project.scripts_dir
        return [str(p) for p in sorted(root.glob("**/*")) if p.is_file() and not p.name.endswith(".graphvis.part")]

    def _literature_row_text(self, path: str) -> str:
        tags = self.store.literature_tags(path)
        return f"{Path(path).name}   [{', '.join(tags)}]" if tags else Path(path).name

    def _literature_tooltip(self, path: str) -> str:
        links = self.store.literature_cross_links(path)
        parts = [str(path)]
        if links.get("tags"):
            parts.append("Tags: " + ", ".join(links["tags"]))
        if links.get("groups"):
            parts.append("In groups: " + ", ".join(links["groups"]))
        if links.get("datasets"):
            parts.append("Cross-linked datasets: " + ", ".join(links["datasets"]))
        if links.get("extracted_tables"):
            parts.append("Extracted tables: " + ", ".join(map(str, links["extracted_tables"])))
        return "\n".join(parts)

    def _edit_literature_tags(self, item: QListWidgetItem) -> None:
        from PySide6.QtWidgets import QInputDialog
        path = str(item.data(Qt.UserRole))
        current = ", ".join(self.store.literature_tags(path))
        text, ok = QInputDialog.getText(
            self, "Literature profile tags",
            f"Comma-separated tags for:\n{Path(path).name}\n\nExisting tags in this project: "
            f"{', '.join(self.store.all_literature_tags()) or '(none yet)'}",
            text=current)
        if not ok:
            return
        self.store.set_literature_tags(path, [t for t in text.split(",")])
        item.setText(self._literature_row_text(path))
        item.setToolTip(self._literature_tooltip(path))

    def _load_current(self, name: str):
        group = self.store.get_group(name) or ProjectGroup(name=name or "Project Overview")
        self.notes.setPlainText(group.notes)
        self.lit_list.clear()
        for p in self._available_literature():
            item = _check_item(self._literature_row_text(p), p, p in group.literature)
            item.setToolTip(self._literature_tooltip(p))
            self.lit_list.addItem(item)
        self.data_list.clear()
        for name_ in self.dataset_names:
            self.data_list.addItem(_check_item(name_, name_, name_ in group.datasets))
        self.script_list.clear()
        for p in self._available_scripts():
            self.script_list.addItem(_check_item(Path(p).name, p, p in group.scripts))

    @staticmethod
    def _set_all(lst: QListWidget, checked: bool) -> None:
        state = Qt.Checked if checked else Qt.Unchecked
        for i in range(lst.count()):
            lst.item(i).setCheckState(state)

    @staticmethod
    def _checked(lst: QListWidget) -> list[str]:
        return [str(lst.item(i).data(Qt.UserRole)) for i in range(lst.count()) if lst.item(i).checkState() == Qt.Checked]

    def _save(self):
        name = self.cb_group.currentText().strip()
        if not name:
            return QMessageBox.warning(self, "Project group", "Enter a group name first.")
        group = ProjectGroup(name=name, literature=self._checked(self.lit_list), datasets=self._checked(self.data_list),
                             scripts=self._checked(self.script_list), notes=self.notes.toPlainText().strip())
        self.store.save_group(group, True)
        self._refresh_names()
        QMessageBox.information(self, "Project group", f"'{name}' is now the active project group.")

    def _new_group(self):
        self.cb_group.setEditText("New Study Group")
        self.notes.clear()
        for lst in (self.lit_list, self.data_list, self.script_list):
            for i in range(lst.count()):
                lst.item(i).setCheckState(Qt.Unchecked)

    def _delete_group(self):
        name = self.cb_group.currentText().strip()
        if not self.store.get_group(name):
            return
        if QMessageBox.question(self, "Delete group", f"Delete project group '{name}'?\n\nThe underlying files will NOT be deleted.") == QMessageBox.Yes:
            self.store.delete_group(name); self._refresh_names()


class ScriptImportDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Import / paste analysis script or parameter file")
        self.resize(820, 620)
        self.source_path: str | None = None
        root = QVBoxLayout(self)
        info = QLabel("Paste MATLAB/Python/R/Julia code or parameter text. GraphVis extracts variable assignments, axis labels, comments and plotting calls, then uses them as Smart Map context. Nothing is executed.")
        info.setWordWrap(True); root.addWidget(info)
        row = QHBoxLayout()
        self.name = QLineEdit("analysis_context.m"); self.name.setPlaceholderText("Filename, e.g. analysis.m")
        browse = QPushButton("Load script file…"); browse.clicked.connect(self._browse)
        row.addWidget(QLabel("Save as:")); row.addWidget(self.name, 1); row.addWidget(browse)
        root.addLayout(row)
        self.text = QPlainTextEdit(); self.text.setPlaceholderText("Paste script / parameters here…")
        root.addWidget(self.text, 1)
        buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
        buttons.button(QDialogButtonBox.Save).setText("Save context to project")
        buttons.accepted.connect(self._accept); buttons.rejected.connect(self.reject); root.addWidget(buttons)

    def _browse(self):
        path, _ = QFileDialog.getOpenFileName(self, "Select analysis script", "", "Scripts (*.m *.py *.R *.r *.jl *.txt *.md);;All files (*.*)")
        if path:
            self.source_path = path; self.name.setText(Path(path).name)
            self.text.setPlainText(Path(path).read_text(encoding="utf-8", errors="ignore"))

    def _accept(self):
        if not self.text.toPlainText().strip():
            return QMessageBox.warning(self, "Script context", "Paste or load some script/parameter text first.")
        if not self.name.text().strip():
            return QMessageBox.warning(self, "Script context", "Enter a filename.")
        self.accept()


class AIAdvisorSettingsDialog(QDialog):
    """Provider-neutral OpenAI-compatible chat endpoint settings.

    The API key is intentionally not persisted by this dialog. The user must
    opt in each time or provide GRAPHVIS_AI_API_KEY in the environment.
    """
    def __init__(self, endpoint: str = "", model: str = "", parent=None):
        super().__init__(parent)
        self.setWindowTitle("AI Project Advisor — optional API")
        self.resize(650, 360)
        root = QVBoxLayout(self)
        warning = QLabel("Optional: send a compact project-context summary to an OpenAI-compatible chat API to refine graph recommendations. Data is sent only when you click Run AI Advisor. The API key is not saved to the project.")
        warning.setWordWrap(True); root.addWidget(warning)
        form = QFormLayout()
        self.endpoint = QLineEdit(endpoint or "http://localhost:1234/v1/chat/completions")
        self.model = QLineEdit(model); self.model.setPlaceholderText("Model name required by your provider")
        self.key = QLineEdit(os.environ.get("GRAPHVIS_AI_API_KEY", "")); self.key.setEchoMode(QLineEdit.Password)
        form.addRow("Chat-completions URL:", self.endpoint); form.addRow("Model:", self.model); form.addRow("API key (session only):", self.key)
        root.addLayout(form)
        self.preview = QPlainTextEdit(); self.preview.setReadOnly(True); self.preview.setPlaceholderText("AI response will be shown here after the request completes.")
        root.addWidget(self.preview, 1)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.button(QDialogButtonBox.Ok).setText("Run AI Advisor")
        buttons.accepted.connect(self.accept); buttons.rejected.connect(self.reject); root.addWidget(buttons)
