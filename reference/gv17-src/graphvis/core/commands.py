# =========================================================================
# commands.py — Ctrl+Z / Ctrl+Shift+Z (Cmd on macOS) command history.
#
# Every user-visible control change becomes a StateCommand holding the full
# UI-state snapshot before and after it, so undo/redo restores *everything*
# (chart type, mappings, styling, clipping, limits, canvas-local controls)
# rather than only the chart type as before. Snapshots are compared before
# pushing so no-op changes don't pollute the stack.
# =========================================================================
from __future__ import annotations

import json

from PySide6.QtGui import QUndoCommand, QUndoStack


def _same(a: dict, b: dict) -> bool:
    return json.dumps(a, sort_keys=True, default=str) == json.dumps(b, sort_keys=True, default=str)


class StateCommand(QUndoCommand):
    def __init__(self, window, before: dict, after: dict, label: str):
        super().__init__(label)
        self._win = window
        self._before = before
        self._after = after
        self._first = True

    def redo(self):
        if self._first:         # QUndoStack.push() calls redo() immediately; state is already applied
            self._first = False
            return
        self._win.apply_ui_state(self._after)

    def undo(self):
        self._win.apply_ui_state(self._before)


class CallbackCommand(QUndoCommand):
    """Undo command for data mutations already applied by a tool dialog.

    The first redo is skipped because QUndoStack.push() invokes redo()
    immediately. Subsequent redo calls execute the supplied callback.
    """
    def __init__(self, do_callback, undo_callback, label: str):
        super().__init__(label)
        self._do = do_callback
        self._undo = undo_callback
        self._first = True

    def redo(self):
        if self._first:
            self._first = False
            return
        self._do()

    def undo(self):
        self._undo()


class CommandHistory:
    """Thin wrapper the main window talks to."""

    def __init__(self, window, limit=200):
        self.window = window
        self.stack = QUndoStack(window)
        self.stack.setUndoLimit(limit)
        self._last: dict | None = None
        self._suspended = 0

    def suspend(self):
        self._suspended += 1

    def resume(self):
        self._suspended = max(0, self._suspended - 1)

    def baseline(self, state: dict):
        self._last = state

    def record(self, state: dict, label: str = "Change"):
        if self._suspended:
            self._last = state
            return
        if self._last is None:
            self._last = state
            return
        if _same(self._last, state):
            return
        self.stack.push(StateCommand(self.window, self._last, state, label))
        self._last = state

    def push_applied(self, do_callback, undo_callback, label: str = "Data operation"):
        if self._suspended:
            return
        self.stack.push(CallbackCommand(do_callback, undo_callback, label))
        if hasattr(self.window, "snapshot_ui_state"):
            try:
                self._last = self.window.snapshot_ui_state()
            except Exception:
                pass

    def create_actions(self, parent):
        undo = self.stack.createUndoAction(parent, "Undo")
        redo = self.stack.createRedoAction(parent, "Redo")
        return undo, redo
