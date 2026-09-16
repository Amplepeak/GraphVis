#!/usr/bin/env python3
"""A standing, parsing audit: what is wrong, what is dead, what is suspicious.

What changed, and why it matters
--------------------------------
This used to be a few thousand lines of regular expressions. Regular
expressions can see text; they cannot see structure, and nearly every bug this
project has actually shipped was structural:

  * a guard placed below the work it existed to prevent - identical text to a
    correct guard, only the position differs;
  * `property bool x: obj && obj.y` - fine for a `var`, broken for a `bool`,
    and only the DECLARATION says which;
  * a rule correct for one class of engine applied to all 434;
  * two independent answers to one question, drifting apart in silence.

So this now parses. There is a C++ lexer and parser (`tools/audit/cpplex.py`,
`cppparse.py`) that finds every function, block, statement and scope; a
resolver (`cppflow.py`) that knows which declaration a name refers to and
whether each use reads or writes it; a QML object-tree parser (`qmlparse.py`);
and Python's own `ast`. None of it needs a compiler, a build, or anything
installed - it runs on a Windows box during a build with nothing but Python.

Three rules govern what is allowed in here, all learned by getting them wrong
first:

  1. FLAG A PROBLEM, NEVER A PATTERN. A check that reports a shape of code
     rather than a defect gets disabled within a week. Several checks have
     been deleted for exactly this, and the counts they produced are recorded
     where they stood, so the next person does not rewrite them.
  2. EVERY CHECK STATES ITS METHOD. A finding is a suspicion with its
     reasoning attached, not a verdict. The method and the known false
     positives travel with the finding into AUDIT.json.
  3. EVERY CHECK IS TESTED BOTH WAYS. A fixture it must catch and a fixture
     it must leave alone - `--selftest`. Without the second half, a check that
     flags everything looks perfect.

It is STATIC and READ-ONLY: it compiles nothing, renders nothing, writes
nothing outside build-reports\\, and needs no build, so it can run while a
build runs. What it cannot do is press a button - see the end of the report.

    python tools/passive_audit.py              # write build-reports/AUDIT.md
    python tools/passive_audit.py --print      # ...and print it
    python tools/passive_audit.py --selftest   # prove the checks work
    python tools/passive_audit.py --accept     # note everything as known
    python tools/passive_audit.py --watch      # run on every change
    python tools/passive_audit.py --explain guard_after_use
"""
from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from audit import checks_cpp, checks_project, checks_py, checks_qml   # noqa: E402
from audit import proc                                                # noqa: E402
from audit import report as reporter                                  # noqa: E402
from audit import selftest as st                                      # noqa: E402
from audit.model import REGISTRY                                      # noqa: E402
from audit.project import Project                                     # noqa: E402

REPORTS = ROOT / "build-reports"
AUDIT_MD = REPORTS / "AUDIT.md"
AUDIT_JSON = REPORTS / "AUDIT.json"
BASELINE = REPORTS / "audit-baseline.json"

CHECK_MODULES = (checks_cpp, checks_qml, checks_py, checks_project)


def all_checks() -> list:
    out = []
    for mod in CHECK_MODULES:
        out.extend(mod.ALL)
    return out


def gather(project: Project) -> list:
    findings: list = []
    for fn in all_checks():
        try:
            fn(project, findings)
        except Exception as exc:        # one broken check is not a dead pass
            from audit.model import Finding
            findings.append(Finding(
                "audit_internal_error", "tools/audit", 1, fn.__name__,
                f"the check `{fn.__name__}` raised {type(exc).__name__}: {exc}",
                why="A check that raises reports nothing, so its whole subject "
                    "goes unexamined until this is fixed - silently, which is "
                    "why it is reported as a finding rather than printed.",
                severity="high", confidence="certain", category="build"))
    return findings


def run_once(reason: str = "", do_selftest: bool = True,
             quiet: bool = False, remember: bool = True) -> tuple[list, list]:
    """One pass. Reports what is NEW, and then remembers.

    `remember` is why this no longer says "147 new" on every single pass. The
    baseline used to be written only by --accept, so nothing was ever known
    and every pass reported every finding as new - which is the exact failure
    this audit exists to avoid, a report that becomes wallpaper. A pass now
    records what it saw, so the next one compares against it. That is what
    "new since the last pass" was always supposed to mean.

    The FIRST pass, with no baseline at all, reports nothing as new rather
    than announcing all 147 at once. There is no "last pass" to be new since.
    """
    t0 = time.time()
    project = Project(ROOT).load()
    findings = gather(project)

    first_ever = not BASELINE.exists()
    baseline = reporter.load_baseline(BASELINE)
    fresh, _known = reporter.split_new(findings, baseline)
    if first_ever:
        fresh = []

    result = st.run(verbose=False) if do_selftest else None
    repo = reporter.repo_info(ROOT)
    elapsed = time.time() - t0

    reporter.write_markdown(AUDIT_MD, findings, fresh, project, elapsed,
                            result, reason, repo=repo, first_ever=first_ever)
    reporter.write_json(AUDIT_JSON, findings, fresh, project, elapsed, result,
                        repo=repo)
    if remember:
        reporter.write_baseline(BASELINE, findings)

    if not quiet:
        stamp = time.strftime("%H:%M:%S")
        high = sum(1 for f in findings if f.severity == "high")
        bits = [f"{len(findings)} standing"]
        if fresh:
            bits.append(f"{len(fresh)} NEW")
        if high:
            bits.append(f"{high} high")
        head = f"[{stamp}] {', '.join(bits)}  ({elapsed:.1f}s"
        head += f", {reason})" if reason else ")"
        print(head)
        if first_ever:
            print(f"    first pass - all {len(findings)} recorded as the "
                  "baseline; from here only changes are listed")
        if result and result[2]:
            print(f"    check self-test FAILING: {result[2][0]}")
        for f in sorted(fresh, key=lambda x: x.rank)[:3]:
            print(f"    NEW  {f.where}")
            print(f"         {f.what}")
        if not fresh and reason and not first_ever:
            print("    nothing new")
    return findings, fresh


# ---------------------------------------------------------------- watching
WATCHED = ("native", "app", "services/python/graphvis_science", "config",
           "tools", "tests")
INTERESTING = {".cpp", ".h", ".qml", ".py", ".json", ".bat", ".ps1", ".txt"}
SKIP = {"target", "build", "__pycache__", ".git", "build-reports", "graph-check"}


def source_stamp() -> int:
    """One number that changes when any source file does.

    A sweep of the tree is cheap enough to ask every few seconds and still be
    idle, which is what makes watching better than a clock. A fixed interval
    is either too slow to be useful after a save or too frequent to be free.
    """
    total = 0
    for name in WATCHED:
        d = ROOT / name
        if not d.is_dir():
            continue
        for p in d.rglob("*"):
            if p.suffix not in INTERESTING:
                continue
            if any(part in SKIP for part in p.parts):
                continue
            try:
                s = p.stat()
                total += s.st_mtime_ns ^ s.st_size
            except OSError:
                # a file removed while the tree is being walked simply does not count
                pass
    return total


def make_shortcut() -> None:
    """Point the desktop shortcut at the WINDOW version of the audit.

    The shortcut used to target PASSIVE-AUDIT.bat - a console. On Windows 11
    consoles are hosted by Windows Terminal, which groups them under its own
    taskbar icon and ignores a shortcut's, so that shortcut could never show
    the audit's mark whatever was done to the .ico. Three rounds went into the
    icon before the target turned out to be the thing that was wrong.

    An existing shortcut is REPOINTED rather than left alone: the wrong target
    is exactly the state this needs to correct, and "leave it if it exists"
    would preserve the bug forever.
    """
    if os.name != "nt":
        return
    link = ROOT / "GraphVis Passive Audit.lnk"
    icon = ROOT / "assets" / "branding" / "graphvis-audit.ico"
    target = ROOT / "PASSIVE-AUDIT-WINDOW.bat"
    if not icon.exists() or not target.exists():
        return
    script = (
        "$w = New-Object -ComObject WScript.Shell; "
        f"$s = $w.CreateShortcut('{link}'); "
        f"if ($s.TargetPath -ne '{target}') {{ "
        f"$s.TargetPath = '{target}'; "
        f"$s.WorkingDirectory = '{ROOT}'; "
        f"$s.IconLocation = '{icon}'; "
        "$s.Description = 'GraphVis passive audit - leave this running'; "
        "$s.Save() }"
    )
    proc.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
              "-Command", script], timeout=30)


def watch(period: float, idle: float) -> int:
    """Audit on change, and at least every `idle` seconds regardless."""
    # Wrapped, because the shortcut is decoration and the audit is not: an
    # earlier version referenced a module it had not imported and took the
    # whole watcher down on startup. Nothing in here is allowed to be fatal.
    try:
        make_shortcut()
    except Exception as exc:
        print(f"(shortcut not made: {exc})")
    print(f"watching {', '.join(WATCHED)}")
    print(f"a pass when anything changes (checked every {period:g}s), "
          f"and at least every {idle / 60:g} min")
    print()
    last = None
    ran_at = 0.0
    try:
        while True:
            now = source_stamp()
            changed = last is not None and now != last
            due = (time.time() - ran_at) >= idle
            if last is None or changed or due:
                if changed:
                    time.sleep(1.5)     # a save often touches several files
                    now = source_stamp()
                run_once(reason="change" if changed else
                                ("first pass" if last is None else "periodic"),
                         do_selftest=last is None)
                ran_at = time.time()
                last = now
            else:
                time.sleep(period)
    except KeyboardInterrupt:
        print("\nstopped.")
    return 0


def explain(check_id: str) -> int:
    info = REGISTRY.get(check_id)
    if info is None:
        print(f"no such check: {check_id}\n\nchecks:")
        for cid, i in sorted(REGISTRY.items()):
            print(f"  {cid:38s} {i.title}")
        return 1
    print(f"{info.id}\n{'=' * len(info.id)}\n")
    print(f"{info.title}\n")
    print(f"WHY IT MATTERS\n  {info.rationale}\n")
    print(f"METHOD\n  {info.method}\n")
    if info.false_positives:
        print(f"KNOWN FALSE POSITIVES\n  {info.false_positives}\n")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="GraphVis passive audit",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--print", dest="show", action="store_true",
                    help="print the report as well as writing it")
    ap.add_argument("--accept", action="store_true",
                    help="record every current finding as known (every pass "
                         "now does this anyway; this only silences the "
                         "summary line)")
    ap.add_argument("--watch", action="store_true",
                    help="keep running, a pass on every change")
    ap.add_argument("--period", type=float, default=5.0,
                    help="seconds between change checks while watching")
    ap.add_argument("--idle", type=float, default=900.0,
                    help="seconds before a pass runs anyway")
    ap.add_argument("--selftest", action="store_true",
                    help="run each check against a case it must catch and a "
                         "case it must not, then stop")
    ap.add_argument("--explain", metavar="CHECK",
                    help="print one check's reasoning and method")
    ap.add_argument("--list", action="store_true", help="list every check")
    args = ap.parse_args()

    if args.explain:
        return explain(args.explain)
    if args.list:
        for cid, i in sorted(REGISTRY.items()):
            print(f"{cid:38s} {i.title}")
        print(f"\n{len(REGISTRY)} checks")
        return 0
    if args.selftest:
        _p, _t, failures = st.run(verbose=True)
        return 1 if failures else 0
    if args.watch:
        return watch(args.period, args.idle)

    findings, _fresh = run_once(do_selftest=True)
    if args.accept:
        reporter.write_baseline(BASELINE, findings)
        print(f"noted {len(findings)} finding(s) as known")
    if args.show:
        print()
        print(AUDIT_MD.read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
