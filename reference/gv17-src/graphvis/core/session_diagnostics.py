"""Crash/session diagnostics for GraphVis.

This module is intentionally Qt-free so it can start before PySide6 or the
scientific stack is imported.  It keeps a tiny active-session journal in the
user Debug folder.  If the process disappears without a clean shutdown, the
next launch turns that journal into an unexpected-exit report under Errors.
"""
from __future__ import annotations

from collections import deque
from datetime import datetime
import faulthandler
import json
import os
from pathlib import Path
import threading
import traceback
from typing import Any

from graphvis.core.paths import DEBUG_LOG_DIR, ERROR_LOG_DIR, LOG_FILE, ensure_layout

ensure_layout()

SESSION_STATE_FILE = DEBUG_LOG_DIR / "active_session.json"
LAST_SESSION_FILE = DEBUG_LOG_DIR / "last_session.json"
ACTIVITY_TRACE_FILE = DEBUG_LOG_DIR / "activity_trace.log"
ERROR_LOG_FILE = ERROR_LOG_DIR / "errors.log"
NATIVE_FAULT_LOG = ERROR_LOG_DIR / "native_faults.log"

_LOCK = threading.RLock()
_RECENT: deque[str] = deque(maxlen=80)
_NATIVE_HANDLE = None


def _stamp() -> str:
    return datetime.now().isoformat(timespec="seconds")


def _safe_json_read(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else {}
    except Exception:
        return {}


def _atomic_json_write(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(value, indent=2, ensure_ascii=False, default=str), encoding="utf-8")
    os.replace(tmp, path)


def _tail(path: Path, max_chars: int = 50000) -> str:
    try:
        with path.open("r", encoding="utf-8", errors="replace") as fh:
            fh.seek(0, os.SEEK_END)
            size = fh.tell()
            fh.seek(max(0, size - max_chars))
            return fh.read()
    except Exception:
        return ""


def _state() -> dict[str, Any]:
    return _safe_json_read(SESSION_STATE_FILE)


def _write_error_line(text: str) -> None:
    try:
        ERROR_LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
        with ERROR_LOG_FILE.open("a", encoding="utf-8") as fh:
            fh.write(f"[{_stamp()}] {text.rstrip()}\n")
    except Exception:
        pass


def _report_path(prefix: str) -> Path:
    token = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    return ERROR_LOG_DIR / f"{prefix}-{token}.txt"


def _write_crash_report(reason: str, *, state: dict[str, Any] | None = None,
                        traceback_text: str = "", prefix: str = "crash") -> Path | None:
    try:
        ensure_layout()
        snap = dict(state or _state())
        path = _report_path(prefix)
        activity_tail = _tail(ACTIVITY_TRACE_FILE, 30000)
        debug_tail = _tail(LOG_FILE, 50000)
        native_tail = _tail(NATIVE_FAULT_LOG, 20000)
        body = [
            "GraphVis diagnostic report",
            "=" * 72,
            f"Created: {_stamp()}",
            f"Reason: {reason}",
            "",
            "Last recorded session state",
            "-" * 72,
            json.dumps(snap, indent=2, ensure_ascii=False, default=str),
        ]
        if traceback_text:
            body += ["", "Captured Python traceback", "-" * 72, traceback_text.rstrip()]
        if activity_tail:
            body += ["", "Recent activity trace", "-" * 72, activity_tail.rstrip()]
        if debug_tail:
            body += ["", "Recent debug log", "-" * 72, debug_tail.rstrip()]
        if native_tail:
            body += ["", "Recent native fault log", "-" * 72, native_tail.rstrip()]
        path.write_text("\n".join(body) + "\n", encoding="utf-8")
        _write_error_line(f"{reason} | report={path}")
        return path
    except Exception:
        return None


def _enable_faulthandler() -> None:
    global _NATIVE_HANDLE
    if _NATIVE_HANDLE is not None:
        return
    try:
        NATIVE_FAULT_LOG.parent.mkdir(parents=True, exist_ok=True)
        _NATIVE_HANDLE = NATIVE_FAULT_LOG.open("a", encoding="utf-8")
        _NATIVE_HANDLE.write(f"\n[{_stamp()}] Native fault capture enabled for pid {os.getpid()}\n")
        _NATIVE_HANDLE.flush()
        faulthandler.enable(file=_NATIVE_HANDLE, all_threads=True)
    except Exception:
        _NATIVE_HANDLE = None


def start_session() -> None:
    """Start a new diagnostic session and recover an unclean previous one."""
    with _LOCK:
        ensure_layout()
        previous = _state()
        if previous.get("status") == "running":
            reason = (
                "Previous GraphVis session ended without a clean shutdown. "
                "No normal exit was recorded; this can indicate a native crash, "
                "forced termination, operating-system shutdown, or power loss."
            )
            _write_crash_report(reason, state=previous, prefix="unexpected-exit")
        new_state: dict[str, Any] = {
            "status": "running",
            "pid": os.getpid(),
            "started_at": _stamp(),
            "updated_at": _stamp(),
            "last_activity": "Launcher starting",
            "last_details": "",
            "thread": threading.current_thread().name,
            "history": [],
        }
        _atomic_json_write(SESSION_STATE_FILE, new_state)
        _enable_faulthandler()
        record_activity("Launcher starting", f"pid={os.getpid()}")


def record_activity(activity: str, details: str = "", **extra: Any) -> None:
    """Persist the most recent operation so crash reports have useful context."""
    activity = str(activity or "Activity")
    details = str(details or "")
    with _LOCK:
        now = _stamp()
        line = f"[{now}] [{threading.current_thread().name}] {activity}"
        if details:
            line += f" | {details}"
        if extra:
            compact = ", ".join(f"{k}={v}" for k, v in extra.items())
            line += f" | {compact}"
        _RECENT.append(line)
        try:
            with ACTIVITY_TRACE_FILE.open("a", encoding="utf-8") as fh:
                fh.write(line + "\n")
        except Exception:
            pass
        state = _state()
        if not state:
            state = {"status": "running", "pid": os.getpid(), "started_at": now, "history": []}
        history = state.get("history") if isinstance(state.get("history"), list) else []
        history.append({"at": now, "activity": activity, "details": details, **extra})
        state.update({
            "status": "running",
            "pid": os.getpid(),
            "updated_at": now,
            "last_activity": activity,
            "last_details": details,
            "thread": threading.current_thread().name,
            "history": history[-20:],
        })
        try:
            _atomic_json_write(SESSION_STATE_FILE, state)
        except Exception:
            pass


def record_exception(reason: str, *, exc: BaseException | None = None,
                     traceback_text: str | None = None, fatal: bool = True) -> Path | None:
    """Write a detailed Python error/crash report immediately."""
    if traceback_text is None:
        if exc is not None:
            traceback_text = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
        else:
            traceback_text = traceback.format_exc()
    with _LOCK:
        state = _state()
        state["last_error"] = str(reason)
        state["last_error_at"] = _stamp()
        state["fatal_error"] = bool(fatal)
        try:
            _atomic_json_write(SESSION_STATE_FILE, state)
        except Exception:
            pass
        return _write_crash_report(str(reason), state=state, traceback_text=traceback_text or "", prefix="crash" if fatal else "error")


def end_session(exit_code: int = 0, reason: str = "Normal application exit") -> None:
    """Mark the active journal clean so the next launch does not report a crash."""
    with _LOCK:
        state = _state()
        if not state:
            return
        state.update({
            "status": "clean" if int(exit_code or 0) == 0 else "error-exit",
            "exit_code": int(exit_code or 0),
            "ended_at": _stamp(),
            "updated_at": _stamp(),
            "last_activity": reason,
        })
        try:
            _atomic_json_write(LAST_SESSION_FILE, state)
            SESSION_STATE_FILE.unlink(missing_ok=True)
        except Exception:
            pass


def current_session_summary() -> dict[str, Any]:
    return _state()
