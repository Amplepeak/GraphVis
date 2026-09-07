"""Embedded Python console widget used by the GraphVis Automation menu."""
from __future__ import annotations

import code
import contextlib
import io
import shutil
import subprocess
from typing import Any

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QDialog,QHBoxLayout,QLineEdit,QPlainTextEdit,QPushButton,QVBoxLayout


class PythonConsoleDialog(QDialog):
    def __init__(self,namespace: dict[str,Any] | None = None,parent=None) -> None:
        super().__init__(parent); self.setWindowTitle("GraphVis Python Console"); self.resize(900,600)
        self.output=QPlainTextEdit(); self.output.setReadOnly(True); self.input=QLineEdit(); self.input.setPlaceholderText("Python expression / statement.  Variables: app, model, controller, datasets, pd, np …")
        run=QPushButton("Run"); clear=QPushButton("Clear")
        row=QHBoxLayout(); row.addWidget(self.input,1); row.addWidget(run); row.addWidget(clear)
        root=QVBoxLayout(self); root.addWidget(self.output,1); root.addLayout(row)
        self.console=code.InteractiveConsole(namespace or {}); self.history: list[str]=[]
        run.clicked.connect(self.execute); self.input.returnPressed.connect(self.execute); clear.clicked.connect(self.output.clear)
        self.output.appendPlainText("GraphVis embedded Python console. Commands execute in the local desktop process.\n")

    def execute(self) -> None:
        text=self.input.text().rstrip();
        if not text: return
        self.history.append(text); self.output.appendPlainText(f">>> {text}"); self.input.clear(); stream=io.StringIO()
        with contextlib.redirect_stdout(stream), contextlib.redirect_stderr(stream):
            try: more=self.console.push(text)
            except Exception as exc: print(f"{type(exc).__name__}: {exc}"); more=False
        content=stream.getvalue();
        if content: self.output.appendPlainText(content.rstrip())
        if more: self.output.appendPlainText("... continuation expected")


class RConsoleDialog(QDialog):
    def __init__(self,parent=None) -> None:
        super().__init__(parent); self.setWindowTitle("GraphVis R Console"); self.resize(850,560); self.output=QPlainTextEdit(); self.output.setReadOnly(True); self.input=QLineEdit(); run=QPushButton("Run R expression"); root=QVBoxLayout(self); root.addWidget(self.output,1); row=QHBoxLayout(); row.addWidget(self.input,1); row.addWidget(run); root.addLayout(row); run.clicked.connect(self.execute); self.input.returnPressed.connect(self.execute)
        if shutil.which("Rscript") is None: self.output.setPlainText("Rscript was not found on PATH. Install R to use this optional console.")
    def execute(self) -> None:
        expr=self.input.text().strip();
        if not expr or shutil.which("Rscript") is None: return
        proc=subprocess.run(["Rscript","-e",expr],capture_output=True,text=True,timeout=60); self.output.appendPlainText(f"> {expr}\n{proc.stdout}{proc.stderr}"); self.input.clear()
