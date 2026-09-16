"""Writing the audit out, twice: once for a person, once for a program.

AUDIT.md is for reading over coffee. It is ranked, grouped, capped, and says
what changed since the last pass, because an unranked list of two hundred
things is wallpaper.

AUDIT.json is for an agent - or the next version of this audit, or a CI job.
It carries every finding with a stable id, a file and line anchor, the
reasoning, the evidence, and the method the check used. That last part is the
one that matters: an agent given a verdict has to trust it, and an agent given
a method can check it. It also carries what the audit did NOT look at, so a
reader can tell the difference between "clean" and "not examined".
"""
from __future__ import annotations

import json
import platform
import sys
from datetime import datetime, timezone
from pathlib import Path

from . import proc
from .model import REGISTRY, SEVERITY, Finding

SCHEMA_VERSION = 2

CATEGORY_ORDER = ["correctness", "build", "verification", "concurrency",
                  "performance", "design", "dead-code", "maintainability",
                  "housekeeping"]

CAP_PER_CHECK = 12


def _now() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def repo_info(root: Path) -> dict:
    """Branch, commit and dirty count, in three subprocess calls - once.

    This used to be five calls per pass, because write_markdown and write_json
    each asked independently. On Windows each one flashed a console window
    over whatever the person was doing, so asking twice for the same answer
    was not merely wasteful.
    """
    status = proc.out(["git", "status", "--porcelain"], cwd=root, timeout=20)
    return {
        "branch": proc.out(["git", "rev-parse", "--abbrev-ref", "HEAD"],
                           cwd=root, timeout=20),
        "commit": proc.out(["git", "rev-parse", "--short", "HEAD"],
                           cwd=root, timeout=20),
        "dirty_files": len([l for l in status.splitlines() if l.strip()]),
    }


def load_baseline(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def split_new(findings: list[Finding], baseline: dict) -> tuple[list, list]:
    known = set(baseline.get("keys", []))
    fresh = [f for f in findings if f.key not in known]
    old = [f for f in findings if f.key in known]
    return fresh, old


def write_json(path: Path, findings: list[Finding], fresh: list[Finding],
               project, elapsed: float, selftest: tuple | None = None,
               repo: dict | None = None) -> None:
    root = project.root
    repo = repo if repo is not None else repo_info(root)
    doc = {
        "schema": SCHEMA_VERSION,
        "generated": _now(),
        "tool": "tools/passive_audit.py",
        "how_to_read": (
            "Each finding is a SUSPICION with its method attached, not a "
            "verdict. `severity` is how much it would matter if real; "
            "`confidence` is how likely it is to be real - they are different "
            "questions. Look up `check` in `checks` for the reasoning the "
            "check used and the false positives it is known to produce, and "
            "judge the finding against that rather than taking it on trust. "
            "`id` is stable across edits that only move the code, so it can "
            "be used to track or suppress a finding."),
        "repo": dict(repo, root=str(root)),
        "run": {
            "seconds": round(elapsed, 2),
            "python": sys.version.split()[0],
            "platform": platform.system(),
            "timings": {k: round(v, 3) for k, v in project.timings.items()},
        },
        "coverage": project.stats(),
        "parse_errors": [{"file": f, "error": e} for f, e in project.parse_errors],
        "not_examined": [
            "Anything that requires running the program: rendering, timing, "
            "memory, and whether a button does what it says.",
            "C++ template instantiation, overload resolution and macro "
            "expansion - names introduced by a macro body are not resolved.",
            "Conditional compilation: both arms of an #ifdef are parsed, and "
            "checks that depend on control flow are suppressed across them.",
            "Rust sources under native/ and the FFI boundary.",
            "Generated build output, vendored headers and native/target/.",
        ],
        "checks": {
            cid: {
                "title": info.title,
                "why_it_matters": info.rationale,
                "method": info.method,
                "known_false_positives": info.false_positives,
            }
            for cid, info in sorted(REGISTRY.items())
        },
        "summary": {
            "total": len(findings),
            "new_since_last_run": len(fresh),
            "by_severity": {s: sum(1 for f in findings if f.severity == s)
                            for s in ("high", "medium", "low", "info")},
            "by_category": {c: sum(1 for f in findings if f.category == c)
                            for c in CATEGORY_ORDER
                            if any(f.category == c for f in findings)},
        },
        "findings": [f.to_json() for f in sorted(findings, key=lambda x: x.rank)],
        "new_finding_ids": [f.key for f in fresh],
    }
    if selftest is not None:
        passed, total, failures = selftest
        doc["self_test"] = {
            "passed": passed, "total": total, "failures": failures,
            "what_it_proves": (
                "Every check is run against a fixture it must flag and a "
                "fixture it must leave alone. The second half is the point: a "
                "check that flags everything passes the first half perfectly."),
        }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=2), encoding="utf-8")


def write_markdown(path: Path, findings: list[Finding], fresh: list[Finding],
                   project, elapsed: float, selftest: tuple | None = None,
                   reason: str = "", repo: dict | None = None,
                   first_ever: bool = False) -> str:
    root = project.root
    repo = repo if repo is not None else repo_info(root)
    st = project.stats()
    high = [f for f in findings if f.severity == "high"]
    lines: list[str] = []
    head = f"# GraphVis passive audit — {len(findings)} standing"
    if fresh:
        head += f", {len(fresh)} NEW"
    if high:
        head += f", {len(high)} high"
    lines.append(head)
    lines.append("")
    lines.append(f"- {_now()}{('  ·  ' + reason) if reason else ''}")
    if repo.get("commit"):
        lines.append(f"- {repo.get('branch', '?')}@{repo['commit']}")
    lines.append(f"- read {st['cpp_files']} C++ files ({st['cpp_lines']:,} lines, "
                 f"{st['tokens']:,} tokens), {st['qml_files']} QML "
                 f"({st['qml_lines']:,} lines), {st['python_files']} Python "
                 f"({st['python_lines']:,} lines)")
    lines.append(f"- parsed {st['functions']} functions, {st['types']} types; "
                 f"pass took {elapsed:.1f}s")
    if selftest is not None:
        passed, total, failures = selftest
        mark = "pass" if not failures else "FAILING"
        lines.append(f"- check self-test: {passed}/{total} {mark}")
        for f in failures:
            lines.append(f"    - {f}")
        # HOW MANY CASES RAN IS NOT HOW MANY CHECKS EXIST.
        #
        # "33/33 pass" reads as full coverage and is not: it counts fixtures,
        # and a check added without one is not counted, not named, and not
        # distinguishable from a covered one. That is the same "a check that
        # reports nothing is indistinguishable from a check that is broken"
        # this self-test exists for, one level up - applied to the self-test.
        try:
            from . import selftest as _st
            missing = _st.uncovered_checks()
        except Exception:                                 # pragma: no cover
            missing = []
        if missing:
            lines.append(f"- {len(missing)} check(s) have no self-test case, so "
                         "nothing proves they still fire: "
                         + ", ".join(f"`{m}`" for m in missing))
    if project.parse_errors:
        lines.append(f"- {len(project.parse_errors)} file(s) could not be parsed "
                     "(listed at the end)")
    lines.append("")

    if first_ever:
        lines.append("## New since the last pass")
        lines.append("")
        lines.append(f"This is the first pass, so there is no previous one to "
                     f"be new since. All {len(findings)} findings below have "
                     f"been recorded as the baseline, and the next pass will "
                     f"list only what changes.")
        lines.append("")
    elif fresh:
        lines.append("## New since the last pass")
        lines.append("")
        for f in sorted(fresh, key=lambda x: x.rank)[:25]:
            lines.append(f"- **{f.what}**")
            lines.append(f"  `{f.where}`  ·  {f.severity}/{f.confidence}  ·  {f.check}")
        if len(fresh) > 25:
            lines.append(f"- ...and {len(fresh) - 25} more")
        lines.append("")
    elif findings:
        lines.append("## New since the last pass")
        lines.append("")
        lines.append("Nothing new. Everything below was already known.")
        lines.append("")

    by_cat: dict[str, list] = {}
    for f in findings:
        by_cat.setdefault(f.category, []).append(f)

    for cat in CATEGORY_ORDER + sorted(set(by_cat) - set(CATEGORY_ORDER)):
        items = by_cat.get(cat)
        if not items:
            continue
        lines.append(f"## {cat} ({len(items)})")
        lines.append("")
        by_check: dict[str, list] = {}
        for f in items:
            by_check.setdefault(f.check, []).append(f)
        for check, group in sorted(by_check.items(),
                                   key=lambda kv: -max(SEVERITY.get(x.severity, 1)
                                                       for x in kv[1])):
            info = REGISTRY.get(check)
            title = info.title if info else check
            lines.append(f"### {title}  ({len(group)})")
            lines.append("")
            if info:
                lines.append(f"*{info.rationale}*")
                lines.append("")
            for f in sorted(group, key=lambda x: x.rank)[:CAP_PER_CHECK]:
                new = " **NEW**" if f in fresh else ""
                lines.append(f"- `{f.where}`{new}")
                lines.append(f"  {f.what}")
                if f.evidence:
                    for ev in f.evidence.split("\n")[:3]:
                        lines.append(f"      {ev}")
                if f.suggestion:
                    lines.append(f"  → {f.suggestion}")
            if len(group) > CAP_PER_CHECK:
                lines.append(f"- ...and {len(group) - CAP_PER_CHECK} more "
                             f"(all of them in AUDIT.json)")
            lines.append("")

    if not findings:
        lines.append("## Nothing to report")
        lines.append("")
        lines.append("Every check ran and found nothing. The self-test line "
                     "above is what tells you that means the checks work, "
                     "rather than that they are broken.")
        lines.append("")

    lines.append("## What this pass did not look at")
    lines.append("")
    lines.append("A clean report is not the same as a working program. This is "
                 "static: it reads the source and runs nothing.")
    lines.append("")
    for item in ("Anything that needs the program running: rendering, timing, "
                 "memory, and whether a button does what it says.",
                 "Template instantiation, overload resolution and macro bodies.",
                 "Both arms of an #ifdef are parsed; flow checks stay silent "
                 "across them.",
                 "Rust sources and the FFI boundary.",
                 "Generated output, vendored headers and native/target/."):
        lines.append(f"- {item}")
    lines.append("")

    # A CHECK THAT DID NOT RUN, said out loud.
    #
    # Beside the parse errors and for the same reason: the reader is being told
    # what this pass does not cover, and "one of the checks could not run here"
    # belongs in that list rather than nowhere. Silence about it makes a check
    # that has not run for months read exactly like a check that passes.
    #
    # Deliberately NOT a finding. A finding is a claim about the code, and the
    # last time a check that could not run made one, it told a reader that a
    # header full of measured palettes was wrong because numpy was missing.
    if getattr(project, "checks_not_run", None):
        lines.append("## Checks that could not run here")
        lines.append("")
        lines.append("These say nothing about the code. They are checks whose "
                     "tool did not run on this machine, listed so that their "
                     "silence is not mistaken for a pass.")
        lines.append("")
        for name, why in project.checks_not_run[:20]:
            lines.append(f"- `{name}`: {why}")
        lines.append("")

    if project.parse_errors:
        lines.append("## Files that could not be parsed")
        lines.append("")
        for f, e in project.parse_errors[:20]:
            lines.append(f"- `{f}`: {e}")
        lines.append("")

    lines.append("## For another agent")
    lines.append("")
    lines.append("`build-reports/AUDIT.json` carries every finding with a "
                 "stable id, a file and line anchor, the reasoning, the "
                 "evidence, and the method each check used — including the "
                 "false positives it is known to produce. Read the method "
                 "before acting on the finding.")
    lines.append("")
    lines.append("<!-- Written by tools/passive_audit.py. Static and read-only: "
                 "it compiles nothing and renders nothing. -->")

    text = "\n".join(lines)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return text


def write_baseline(path: Path, findings: list[Finding]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({
        "written": _now(),
        "keys": sorted({f.key for f in findings}),
    }, indent=2), encoding="utf-8")
