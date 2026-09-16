#!/usr/bin/env python3
"""The passive audit in a window of its own.

Why this is not just the console
--------------------------------
The console version is fine and still exists. What it cannot do is carry its
own taskbar icon: on Windows 11 the console host is Windows Terminal, every
console it hosts is grouped under the Terminal icon, and a shortcut's icon is
ignored. Three attempts at fixing that from the shortcut all produced the same
generic prompt icon, because the icon was never the shortcut's to set.

A real window has its own icon, its own title and its own taskbar entry - and
it can ask before it closes, which is the actual point: this is meant to be
left running for hours, which makes it exactly the window somebody tidies away
without looking.

The audit itself runs in a thread and is the same code the console uses, so
there is one definition of what an audit is.

    pythonw tools/audit_window.py          watch, and a window
    python  tools/audit_window.py --once   one pass into the window
"""
from __future__ import annotations

import importlib.util
import os
import queue
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRAND = ROOT / "assets" / "branding"
ICON = BRAND / "graphvis-audit.ico"
ICON_PNG = BRAND / "graphvis-audit.png"
BADGE = BRAND / "graphvis-audit-badge.png"

BG = "#0d1b2a"
PANEL = "#08121d"

# UNDER pythonw THERE IS NO STDOUT, AND print() RAISES.
#
# sys.stdout is None in a pythonw process, so any stray print anywhere below -
# or in the audit, or in a library it calls - takes the window down with an
# AttributeError on NoneType.write. The audit is asked to stay quiet, but
# "asked to" is not "guaranteed to", and a debugger that dies because
# something printed is worse than useless.
class _Sink:
    def write(self, _s):
        return 0

    def flush(self):
        pass


if sys.stdout is None:
    sys.stdout = _Sink()
if sys.stderr is None:
    sys.stderr = _Sink()

try:
    import tkinter as tk
    from tkinter import messagebox, scrolledtext
except ImportError:                      # a Python built without Tk
    print("tkinter is not available in this Python, so the window cannot open.")
    print("Use PASSIVE-AUDIT.bat instead - it does the same work in a console.")
    raise SystemExit(1)


def claim_taskbar_identity() -> str:
    """Tell Windows this process is its own application, not Python.

    THIS, not the .ico, is why the taskbar icon was wrong.

    A pythonw process inherits pythonw.exe's application identity, so Windows
    groups the window under Python and draws PYTHON'S icon on the taskbar -
    however valid the icon we set on the window. Three rounds went into the
    .ico (a shortcut that could not carry one, then PNG-compressed entries Tk
    refuses) and the .ico is now correct, verified entry by entry. It was
    never the whole answer.

    Setting an explicit AppUserModelID gives the process its own taskbar
    identity, and the window's own icon is then the one shown. It has to
    happen before any window exists, which is why this is called first.
    """
    if os.name != "nt":
        return ""
    try:
        import ctypes
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(
            "GraphVis.PassiveAudit")
        return ""
    except Exception as exc:
        return f"taskbar identity not set ({exc})"


AUDIT_SOURCES = (ROOT / "tools" / "passive_audit.py",
                 ROOT / "tools" / "audit")


def audit_stamp() -> int:
    """One number that changes when the AUDIT'S OWN code changes."""
    total = 0
    for path in AUDIT_SOURCES:
        files = [path] if path.is_file() else sorted(path.rglob("*.py"))
        for f in files:
            try:
                st = f.stat()
                total += st.st_mtime_ns ^ st.st_size
            except OSError:
                # a file removed mid-walk simply does not count towards the stamp
                pass
    return total


def load_audit():
    """The audit module, loaded by path so this works from anywhere.

    RELOADED, not cached. This window is meant to be left running for hours,
    which means it happily goes on running whatever version of the audit it
    imported when it started - so a fixed check does nothing until somebody
    thinks to close the window, and the report quietly disagrees with the
    code on disk. That is a worse failure than a crash, because it looks
    like it is working.

    Dropping the package from sys.modules first matters: passive_audit
    imports `audit.checks_*` at module level, and re-executing the top-level
    file alone would re-use the already-imported submodules - which are
    exactly the ones that change.
    """
    for name in [n for n in list(sys.modules)
                 if n == "passive_audit" or n == "audit"
                 or n.startswith("audit.")]:
        del sys.modules[name]
    spec = importlib.util.spec_from_file_location(
        "passive_audit", ROOT / "tools" / "passive_audit.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["passive_audit"] = module
    spec.loader.exec_module(module)
    return module


class AuditWindow:
    def __init__(self, root: tk.Tk, once: bool, notes: list) -> None:
        self.root = root
        self.once = once
        self.lines: queue.Queue = queue.Queue()
        self.stop = threading.Event()
        self.passes = 0
        self.notes = list(notes)

        root.title("GraphVis Passive Audit")
        root.geometry("960x560")
        root.configure(bg=BG)
        note = self.apply_icon(root)
        if note:
            self.notes.append(note)

        head = tk.Frame(root, bg=BG)
        head.pack(fill="x", padx=14, pady=(12, 4))

        # THE MARK, TOP RIGHT. Packed before the text and with side="right",
        # so it reserves its corner and the title wraps against it rather than
        # under it.
        self.badge = self.load_badge()
        if self.badge is not None:
            tk.Label(head, image=self.badge, bg=BG,
                     borderwidth=0).pack(side="right", anchor="ne", padx=(12, 0))

        titles = tk.Frame(head, bg=BG)
        titles.pack(side="left", fill="x", expand=True)
        tk.Label(titles, text="GraphVis passive audit", bg=BG, fg="#e8f1ff",
                 font=("Segoe UI", 14, "bold")).pack(anchor="w")
        self.status = tk.Label(titles, text="starting…", bg=BG, fg="#8fb6dd",
                               font=("Segoe UI", 9), justify="left", anchor="w")
        self.status.pack(anchor="w", fill="x")

        self.text = scrolledtext.ScrolledText(
            root, bg=PANEL, fg="#cfe3f7", insertbackground="#cfe3f7",
            font=("Consolas", 10), relief="flat", wrap="word")
        self.text.pack(fill="both", expand=True, padx=14, pady=(8, 6))
        self.text.tag_config("new", foreground="#ffd479")
        self.text.tag_config("high", foreground="#ff9d8a")
        self.text.tag_config("quiet", foreground="#6f90b0")
        self.make_readonly_but_copyable()

        foot = tk.Frame(root, bg=BG)
        foot.pack(fill="x", padx=14, pady=(0, 12))
        tk.Label(foot, text="Report: build-reports\\AUDIT.md   ·   "
                            "for agents: build-reports\\AUDIT.json",
                 bg=BG, fg="#6f90b0", font=("Segoe UI", 9)).pack(side="left")
        # THE BUTTON IS A NUDGE, NOT THE MECHANISM.
        #
        # Passes happen on their own - within seconds of any source file
        # changing, and every fifteen minutes regardless. This is for when you
        # want one right now without waiting, which is mostly "did it notice
        # what I just did".
        tk.Button(foot, text="Run a pass now", command=self.run_now,
                  bg="#1b3a5c", fg="#e8f1ff", relief="flat",
                  activebackground="#27527f", activeforeground="#ffffff",
                  padx=12, pady=4).pack(side="right")
        tk.Button(foot, text="Copy all", command=self.copy_all,
                  bg="#132b45", fg="#cfe3f7", relief="flat",
                  activebackground="#27527f", activeforeground="#ffffff",
                  padx=12, pady=4).pack(side="right", padx=(0, 8))

        # ASKS BEFORE IT CLOSES. This window exists to be left alone for hours.
        root.protocol("WM_DELETE_WINDOW", self.confirm_close)

        self.say("GraphVis passive audit — running automatically.", "quiet")
        self.say("A pass whenever a source file changes, and every 15 minutes "
                 "regardless.", "quiet")
        for n in self.notes:
            self.say(f"({n})", "quiet")
        self.say("", "quiet")

        self.worker = threading.Thread(target=self.loop, daemon=True)
        self.worker.start()
        self.root.after(200, self.drain)

    # ------------------------------------------------------------ selection
    def make_readonly_but_copyable(self) -> None:
        """Read-only, and easier to get text out of.

        Selecting with the mouse and Ctrl+C already worked on the old
        `state="disabled"` widget - I assumed otherwise when writing this and
        was wrong, so the note is corrected rather than left to mislead
        somebody later.

        What this adds is the rest of it: a right-click menu, Select all, a
        Copy all button, and Ctrl+A. Findings are file paths and line numbers,
        which are the things most worth pasting somewhere else, and hunting
        for the start of a line with the mouse is a poor way to get at them.

        The widget stays NORMAL rather than disabled and every key that would
        change the text is swallowed instead, which keeps it read-only while
        leaving the selection machinery completely untouched.
        """
        allowed = {
            "Left", "Right", "Up", "Down", "Home", "End", "Prior", "Next",
            "Shift_L", "Shift_R", "Control_L", "Control_R",
        }

        def keep_out(event):
            if event.state & 0x4:            # Control held: copy, select all
                if event.keysym.lower() in ("c", "a", "insert"):
                    return None
                return "break"
            if event.keysym in allowed:
                return None
            return "break"

        self.text.bind("<Key>", keep_out)
        self.text.bind("<<Paste>>", lambda e: "break")
        self.text.bind("<<Cut>>", lambda e: "break")
        self.text.bind("<Control-a>", self.select_all)
        self.text.bind("<Control-A>", self.select_all)

        menu = tk.Menu(self.text, tearoff=0)
        menu.add_command(label="Copy", command=self.copy_selection)
        menu.add_command(label="Copy everything", command=self.copy_all)
        menu.add_separator()
        menu.add_command(label="Select all", command=self.select_all)
        self._menu = menu

        def popup(event):
            try:
                menu.tk_popup(event.x_root, event.y_root)
            finally:
                menu.grab_release()

        self.text.bind("<Button-3>", popup)

    def select_all(self, _event=None):
        self.text.tag_add("sel", "1.0", "end-1c")
        self.text.focus_set()
        return "break"

    def copy_selection(self, _event=None):
        try:
            text = self.text.get("sel.first", "sel.last")
        except tk.TclError:
            return "break"                   # nothing selected
        self.to_clipboard(text)
        return "break"

    def copy_all(self, _event=None):
        self.to_clipboard(self.text.get("1.0", "end-1c"))
        return "break"

    def to_clipboard(self, text: str) -> None:
        if not text:
            return
        self.root.clipboard_clear()
        self.root.clipboard_append(text)
        # Windows empties the clipboard when the owning process exits unless
        # it has been flushed to the system.
        self.root.update_idletasks()

    # ----------------------------------------------------------------- icon
    def load_badge(self):
        """The 48px mark for the header.

        Loaded at its exact display size rather than scaled: Tk's PhotoImage
        can only subsample by whole integers, and a 256px image reduced that
        way looks chewed. tools/make_audit_icon.py emits the size this wants.
        """
        for candidate in (BADGE, ICON_PNG):
            if not candidate.exists():
                continue
            try:
                img = tk.PhotoImage(file=str(candidate))
                if candidate is ICON_PNG:
                    factor = max(1, img.width() // 48)
                    img = img.subsample(factor, factor)
                return img
            except Exception:
                continue
        return None

    def apply_icon(self, root: tk.Tk) -> str:
        """Set the window and taskbar icon, and say what happened.

        Both routes are tried and the failure is REPORTED rather than
        swallowed - the icon has been silently wrong three times, and each
        time a caught-and-ignored TclError is what hid the reason.
        """
        if not ICON.exists():
            return f"no icon at {ICON}"
        first = None
        try:
            root.iconbitmap(default=str(ICON))
        except Exception as exc:
            first = exc
        try:
            # iconphoto takes a PhotoImage, which reads PNG rather than ICO.
            # Set alongside iconbitmap, not instead of it: they feed different
            # parts of Windows, and between them the window, the Alt-Tab card
            # and the taskbar all end up with the mark.
            if ICON_PNG.exists():
                self._icon_image = tk.PhotoImage(file=str(ICON_PNG))
                root.iconphoto(True, self._icon_image)
                return ""
        except Exception as exc:
            return f"icon not set ({first}; then {exc})" if first else \
                   f"icon not set ({exc})"
        return f"icon not set ({first})" if first else ""

    # ---------------------------------------------------------------- output
    def say(self, line: str, tag: str = "") -> None:
        self.lines.put((line, tag))

    def drain(self) -> None:
        wrote = False
        while True:
            try:
                line, tag = self.lines.get_nowait()
            except queue.Empty:
                break
            self.text.insert("end", line + "\n", tag or ())
            wrote = True
        if wrote:
            self.text.see("end")
        self.root.after(200, self.drain)

    # ------------------------------------------------------------- the audit
    def run_now(self) -> None:
        self.stop.set()                  # nudge the sleeper; the loop re-arms

    def loop(self) -> None:
        try:
            audit = load_audit()
        except Exception as exc:
            self.say(f"the audit could not be loaded: {exc}", "high")
            return
        # Repair the shortcut from here too, so that opening this window once
        # is enough to fix a shortcut still pointing at the console version.
        # Wrapped: a taskbar shortcut is decoration and the audit is not.
        try:
            audit.make_shortcut()
        except Exception as exc:
            self.say(f"(shortcut not repaired: {exc})", "quiet")
        last_stamp = None
        ran_at = 0.0
        first = True
        tool_stamp = audit_stamp()
        while True:
            # Has the AUDIT changed under us? Reload before using it, so an
            # edited check takes effect on the next pass rather than the next
            # time somebody restarts this window.
            now_tools = audit_stamp()
            if now_tools != tool_stamp:
                tool_stamp = now_tools
                try:
                    audit = load_audit()
                    self.say("  the audit's own code changed — reloaded", "quiet")
                    first = True          # re-run the self-test on new checks
                except Exception as exc:
                    self.say(f"  the audit failed to reload: "
                             f"{type(exc).__name__}: {exc}", "high")
                    self.stop.wait(5.0)
                    continue
            now_stamp = audit.source_stamp()
            changed = last_stamp is not None and now_stamp != last_stamp
            due = (time.time() - ran_at) >= 900
            if last_stamp is None or changed or due or self.stop.is_set():
                reason = ("change" if changed else
                          "asked" if self.stop.is_set() else
                          "first pass" if last_stamp is None else "periodic")
                self.stop.clear()
                if changed:
                    time.sleep(1.5)      # a save often touches several files
                    now_stamp = audit.source_stamp()
                self.one_pass(audit, reason, selftest=first)
                first = False
                ran_at = time.time()
                last_stamp = now_stamp
                if self.once:
                    return
            else:
                self.stop.wait(3.0)

    def one_pass(self, audit, reason: str, selftest: bool = False) -> None:
        started = time.time()
        # Asked BEFORE the pass, because the pass writes it.
        first_ever = not audit.BASELINE.exists()
        try:
            # quiet=True because there is no stdout under pythonw, and
            # do_selftest only on the first pass: it is the same answer every
            # time unless a check has been edited, and it is not free.
            findings, fresh = audit.run_once(reason=reason,
                                             do_selftest=selftest,
                                             quiet=True)
        except Exception as exc:         # a broken check must not kill the window
            self.say(f"  audit failed: {type(exc).__name__}: {exc}", "high")
            return
        self.passes += 1
        clock = time.strftime("%H:%M:%S")
        took = time.time() - started
        high = [f for f in findings if f.severity == "high"]
        self.say(f"[{clock}] {len(fresh):>3} new · {len(findings):>3} standing · "
                 f"{len(high):>2} high · {took:.1f}s  ({reason})",
                 "" if fresh else "quiet")
        for f in sorted(fresh, key=lambda x: x.rank)[:10]:
            tag = "high" if f.severity == "high" else "new"
            self.say(f"          {f.where}", tag)
            self.say(f"            {f.what[:120]}", tag)
        if len(fresh) > 10:
            self.say(f"          …and {len(fresh) - 10} more in the report", "new")
        if first_ever:
            self.say(f"          first pass — all {len(findings)} recorded as "
                     "the baseline; from here only changes are listed", "quiet")
        elif not fresh:
            self.say("          nothing new", "quiet")
        self.status.config(
            text=f"Running automatically · pass {self.passes} at {clock} · "
                 f"{len(findings)} standing, {len(high)} high · "
                 f"next on any file change, or within 15 min")

    # -------------------------------------------------------------- closing
    def confirm_close(self) -> None:
        if messagebox.askokcancel(
                "Stop the passive audit?",
                "This window is the passive audit. Closing it stops the audit "
                "and nothing else.\n\nStop it?"):
            self.root.destroy()


def main() -> int:
    notes = []
    note = claim_taskbar_identity()       # BEFORE any window exists
    if note:
        notes.append(note)
    root = tk.Tk()
    AuditWindow(root, once="--once" in sys.argv, notes=notes)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
