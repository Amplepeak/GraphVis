#!/usr/bin/env python3
"""One small file that says what this build did, what is wrong, and what changed.

Why this exists
---------------
Everything needed to answer "is this build healthy" is already written down —
graph-check\\selftest-output.txt, summary.txt, startup.log, the gallery, the
staged binaries — and it is spread over five places and about 80,000 characters,
most of which is 439 lines saying "rendering <engine>". Reading it costs more
attention than it is worth, so it does not get read.

This reduces it to one page: the verdict, the failures verbatim, and — the part
that is actually worth having — WHAT CHANGED SINCE LAST TIME. A failure that has
been there for a week is different from one that appeared in this build, and
only a history can tell them apart.

Standard library only, so it runs wherever python does. It reads; it never
builds, renders or deletes anything.

    python tools/build_report.py              # write build-reports/REPORT.md
    python tools/build_report.py --print      # and print it

The history lives in build-reports/, not graph-check/, because CHECK-GRAPHS.bat
deletes graph-check at the start of every run — a history kept there would be
one run long, every time.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECK = ROOT / "graph-check"

# STARTUP MESSAGES CAUSED BY THE CHECK RATHER THAN BY THE PROGRAM.
#
# THE PROGRAM SAYS WHICH THEY ARE. THIS FILE DOES NOT DECIDE.
#
# This was a table of message substrings kept here, with a justification beside
# each. It worked, and it was the second answer to a question the program was
# already answering for itself: --selftest-ui counted the font-directory warning
# and exited 3 while this report, matching the same message against the table
# below, printed "problems: none". Every run disagreed with itself, and which
# verdict a reader believed depended on which one they read.
#
# main.cpp now marks such a line as it writes it - `[HARNESS: <reason>]`, on the
# same line as the message, with the reason it was written for. It marks a line
# only when it KNOWS: the font message is excused when, and only when, the
# binary was launched with the offscreen platform this check uses. So the two
# sides cannot drift, because there is only one side.
#
# Kept as a pattern rather than a fixed string so a log written by a binary from
# before this marker existed still reports as it did - it simply finds nothing.
#
# MATCHED ANYWHERE ON THE LINE, not anchored to its end. Anchored, this missed
# the first real marked message: Qt's font warning carries a note after a
# newline, so the marker landed at the end of the SECOND physical line while the
# text that identifies the fault sat on the first. The report saw an unmarked
# warning and counted it - the marker was written, correct, and in a place this
# pattern could not see. main.cpp now writes the marker straight after the level
# and flattens the message onto one line; this stays unanchored so the position
# of the marker is not a second thing to keep in step.
HARNESS_MARK = re.compile(r"\s*\[HARNESS:\s*(?P<why>[^\]]*)\]\s*")

REPORTS = ROOT / "build-reports"
HISTORY = REPORTS / "history.json"
REPORT = REPORTS / "REPORT.md"
KEEP_RUNS = 12

# Lines that are noise by the thousand. Everything else in the selftest output
# is either a verdict or a number.
NOISE = re.compile(r"^(selftest: rendering |  order: |  edge: |  constant: |"
                   r"selftest: engines supported by)")


def read(path: Path, limit: int = 4_000_000) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")[:limit]
    except OSError:
        return ""


def git(*args: str) -> str:
    try:
        out = subprocess.run(("git",) + args, cwd=ROOT, capture_output=True,
                             text=True, timeout=20)
        return out.stdout.strip() if out.returncode == 0 else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def read_facts_from_log(log: str) -> dict:
    """What startup.log says went wrong, deduplicated and sorted by whose fault.

    A FUNCTION, not a passage inside collect(), so it can be handed a log and
    asked what it makes of it. It used to be reachable only by putting a file on
    disk in the right place, which meant the rule it implements - which lines
    count against the build - was pinned down by nothing.

    DEDUPLICATED, because one fault repeats. The first real run of this reported
    "24 warning/error line(s)" and all 24 were one QML binding, logged twice per
    occurrence over two sessions. A count of lines measures how often a delegate
    was rebuilt; a count of distinct messages measures how many things are
    wrong, which is the question. The timestamp and the duplicated [WARNING]
    prefix are stripped so the same fault collapses to one entry with a tally.

    MESSAGES THE HARNESS CAUSES, NOT THE PROGRAM - separated out, never hidden.
    A problem count that is permanently non-zero is a problem count nobody
    reads, and this project has already paid for that once: sixteen
    guaranteed-false qmllint warnings were what hid the seventeenth, which was
    real. So a line the program marked as an artefact of HOW it was launched is
    kept out of the count - and still printed under its own heading, with the
    reason the program gave, so it cannot quietly become permanent cover for
    something else. The judgement is the program's; see HARNESS_MARK.
    """
    facts: dict = {}
    # ONE MESSAGE, ONE LINE - reassembled here, whatever wrote the log.
    #
    # Qt writes multi-line messages, and this reader works a line at a time, so a
    # wrapped message arrived as two unrelated things: the half naming the fault,
    # and a half nothing recognised. That is how the font warning's [HARNESS]
    # marker - written, correct, and sitting on the continuation line - was not
    # seen, and the message it excuses was counted against the build.
    #
    # main.cpp now flattens as it writes, so new logs need none of this. It is
    # still done here, because the old logs on disk are read by the same code and
    # because the next multi-line message Qt invents should not need a second
    # fix. Every entry begins with an ISO timestamp; a line that does not is a
    # continuation of the one above it.
    stamped = re.compile(r"^\d{4}-\d{2}-\d{2}T")
    joined: list[str] = []
    for raw in log.splitlines():
        if joined and not stamped.match(raw):
            joined[-1] = joined[-1] + "  |  " + raw.strip()
        else:
            joined.append(raw.strip())
    bad = [l for l in joined
           if re.search(r"\b(WARNING|ERROR|Cannot|cannot|undefined|is not a|"
                        r"TypeError|ReferenceError)\b", l)]
    seen: dict[str, int] = {}
    benign: dict[str, int] = {}
    harness_reasons: dict[str, str] = {}
    for line in bad:
        key = re.sub(r"^\S+\s+", "", line)                 # timestamp
        # Any level, not the three that happened to be listed: a [CRITICAL] kept
        # its prefix and so counted as a different fault from the same message
        # logged as a [WARNING].
        key = re.sub(r"^(QML:|\[(?:DEBUG|INFO|WARNING|CRITICAL|FATAL)\])\s*", "", key)
        mark = HARNESS_MARK.search(key)
        if mark:
            key = HARNESS_MARK.sub(" ", key).strip()
        key = re.sub(r"\s+\(qrc:.*\)$", "", key)           # trailing source ref
        if mark:
            benign[key] = benign.get(key, 0) + 1
            harness_reasons[key] = mark.group("why").strip()
        else:
            seen[key] = seen.get(key, 0) + 1
    facts["startup_harness_reasons"] = harness_reasons
    facts["startup_problems"] = [f"{k}   [x{n}]" if n > 1 else k
                                 for k, n in list(seen.items())[:25]]
    facts["startup_problem_count"] = len(seen)
    facts["startup_benign"] = [f"{k}   [x{n}]" if n > 1 else k
                               for k, n in list(benign.items())[:25]]
    facts["startup_line_count"] = len(bad)
    return facts


def collect() -> dict:
    """Everything worth knowing, as plain data, so it can be diffed."""
    selftest = read(CHECK / "selftest-output.txt")
    summary = read(CHECK / "summary.txt")
    facts: dict = {
        "when": datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds"),
        "commit": git("rev-parse", "--short", "HEAD"),
        "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
        "dirty": len([l for l in git("status", "--porcelain").splitlines() if l.strip()]),
        "version": read(ROOT / "VERSION").strip(),
    }

    # ---- the checks -------------------------------------------------------
    def first(pattern: str, text: str = selftest, group: int = 1):
        m = re.search(pattern, text)
        return m.group(group) if m else None

    facts["engines_swept"] = int(first(r"swept (\d+) engines") or 0)
    facts["same_picture_groups"] = int(first(r"(\d+) group\(s\) of engines render the same") or 0)
    facts["undecided_clusters"] = int(first(r"(\d+) still to decide") or 0)
    facts["like_line_chart"] = int(first(r"(\d+) engine\(s\) render exactly as a plain Line Chart") or 0)
    facts["gallery_figures"] = int(first(r"gallery written to .*?\((\d+) figures\)") or 0)
    facts["every_engine_drew"] = "every engine drew data" in selftest
    facts["seed_stable"] = "survives a change of QHash seed" in selftest
    facts["vector_pdf_ok"] = "VECTOR PDF OK" in selftest
    facts["regression_passed"] = "regression checks passed" in selftest
    facts["selftest_exit"] = first(r"selftest exit code (-?\d+)", summary)

    # Failures, verbatim. These are what a person actually needs to see.
    facts["regressions"] = [l.split("REGRESSION: ", 1)[1].strip()
                            for l in selftest.splitlines() if "REGRESSION: " in l]
    facts["property_failures"] = [l.strip() for l in selftest.splitlines()
                                  if re.match(r"^property: .*(FAIL|failed)", l)]
    facts["failed_lines"] = [l.strip() for l in selftest.splitlines()
                             if re.search(r"\bFAILED\b", l)][:20]

    # Property-check headline numbers, which are measurements rather than
    # verdicts and are worth watching for drift.
    facts["order_changed"] = int(first(r"order-invariance checked on \d+ engines, (\d+) changed") or 0)
    facts["order_checked"] = int(first(r"order-invariance checked on (\d+) engines") or 0)

    # ---- performance ------------------------------------------------------
    facts["kde_cold_ms"] = float(first(r"KDE over 4000 points: cold ([\d.]+) ms") or 0)
    facts["kde_warm_ms"] = float(first(r"KDE over 4000 points: cold [\d.]+ ms, warm ([\d.]+) ms") or 0)
    # How long the checking phase took. Stamped by --mark-start at the top of
    # the run rather than computed in batch, where subtracting two %TIME%
    # values across midnight is its own small tragedy.
    started = read(REPORTS / "started.txt").strip()
    facts["check_seconds"] = 0.0
    if re.fullmatch(r"[\d.]+", started or ""):
        facts["check_seconds"] = round(time.time() - float(started), 1)
        try:
            (REPORTS / "started.txt").unlink()
        except OSError:
            # the marker was already gone, which is the state we wanted
            pass

    # ---- the interface ----------------------------------------------------
    log_path = CHECK / "startup.log"
    if not log_path.exists():
        log_path = ROOT / "graph-check" / "startup.log"
    log = read(log_path)

    # IS THIS LOG FROM THIS RUN? Ask before drawing any conclusion from it.
    #
    # CHECK-GRAPHS.bat copies the logs out of %LOCALAPPDATA% during its own
    # step [2/3], which happens BEFORE BUILD-AND-CHECK runs --selftest-ui. So
    # this file was, for a long time, always the previous session's log - and
    # every verdict below was about a run that had already finished before the
    # interface check started. BUILD-AND-CHECK now copies it again afterwards;
    # this is the guard for the case where it did not, or where the report is
    # run on its own.
    #
    # summary.txt is written at the end of the sweep, so a startup.log older
    # than it cannot contain anything this pass caused. Told apart from "the
    # interface did not start" deliberately: one is a verdict and the other is
    # the absence of one, and reporting a stale log as a crash is how a check
    # earns the reputation that gets it switched off.
    # summary_path, not `summary`: that name already holds this file's TEXT,
    # read at the top of this function. Two meanings for one name in one scope
    # is the shape the C++ side has a check for - an edit in the wrong place
    # compiles - and it costs nothing to not do it here.
    summary_path = CHECK / "summary.txt"
    facts["ui_log_stale"] = bool(
        log.strip() and summary_path.exists() and log_path.exists()
        and log_path.stat().st_mtime < summary_path.stat().st_mtime - 1)
    facts.update(read_facts_from_log(log))

    # DID THE INTERFACE ACTUALLY START? Counting complaints cannot answer that.
    #
    # This reported "Problems: none" and, in the trend, "better  startup.log
    # problems 2 -> 0 (down)" for a build whose every launch died with an
    # access violation inside the QML load. The reasoning was sound and the
    # question was wrong: a crash writes NO warning lines, because the process
    # is gone before it can write any - so zero complaints scored better than
    # the two harmless ones the previous build had. The check counted the
    # symptoms of a running program and read their absence as health.
    #
    # That is this project's own recorded rule, in the one place it hurts most:
    # a check that cannot run is not a check that passed, and silence is not a
    # pass. So the question is now a STATEMENT the program makes about itself.
    # main.cpp writes this line immediately after loadFromModule returns a root
    # object, and nothing else writes it - so its absence means the interface
    # did not finish starting, whatever else the log does or does not say.
    facts["ui_log_present"] = bool(log.strip())
    facts["ui_started"] = "Main QML window created successfully" in log

    # ---- what is deployed -------------------------------------------------
    sizes = {}
    stage = ROOT / "build" / "stage"
    if stage.is_dir():
        for f in stage.iterdir():
            try:
                if f.is_file() and f.stat().st_size > 8 * 1024 * 1024:
                    sizes[f.name] = round(f.stat().st_size / 1_048_576, 1)
            except OSError:
                # a file that vanished mid-scan is not a file we need to size
                pass
    facts["big_files_mb"] = dict(sorted(sizes.items(), key=lambda kv: -kv[1])[:10])
    facts["stage_total_mb"] = round(
        sum(f.stat().st_size for f in stage.rglob("*") if f.is_file()) / 1_048_576, 1
    ) if stage.is_dir() else 0

    # ---- the catalogue ----------------------------------------------------
    try:
        cat = json.loads(read(ROOT / "config" / "graph_catalogue.json"))
        entries = [e for c in cat["categories"] for e in c["entries"]]
        facts["catalogue_entries"] = len(entries)
        facts["catalogue_engines"] = len({e["engine"] for e in entries})
        facts["unverified_engines"] = sorted({e["engine"] for e in entries
                                              if e.get("verified") is False})
    except Exception:
        facts["catalogue_entries"] = facts["catalogue_engines"] = 0
        facts["unverified_engines"] = []
    return facts


def problems(f: dict) -> list[str]:
    """The things a person should act on, worst first."""
    out = []
    if f.get("selftest_exit") not in (None, "0"):
        out.append(f"selftest exited {f['selftest_exit']}")
    out += [f"REGRESSION: {r}" for r in f["regressions"]]
    out += f["property_failures"]
    if f["engines_swept"] and not f["every_engine_drew"]:
        out.append("an engine drew nothing")
    if f["same_picture_groups"]:
        out.append(f"{f['same_picture_groups']} group(s) of engines draw the same picture")
    if f["like_line_chart"]:
        out.append(f"{f['like_line_chart']} engine(s) draw exactly as a plain Line Chart")
    if not f["seed_stable"] and f["engines_swept"]:
        out.append("a figure changed with the hash seed")
    if not f["vector_pdf_ok"]:
        out.append("the vector PDF self-test did not report OK")
    # FIRST among the interface findings, because it subsumes the rest: if the
    # window was never created, the absence of warnings below is the silence of
    # a dead process rather than the quiet of a clean one.
    #
    # Defaulting to True keeps older runs in history.json - written before this
    # was recorded - from being reported as failures they were never judged on.
    if f.get("ui_log_stale"):
        out.append("the interface log in graph-check/ is older than the sweep, "
                   "so it is from an earlier session and this pass cannot say "
                   "whether the interface started - re-run BUILD-AND-CHECK, "
                   "which now refreshes it after the interface check")
    elif not f.get("ui_started", True):
        out.append("THE INTERFACE DID NOT FINISH STARTING - startup.log never "
                   "reaches \"Main QML window created successfully\", so the "
                   "window was not created. A crash or an exit during the QML "
                   "load logs nothing, which is why the warning count below is "
                   "zero.")
    elif not f.get("ui_log_present", True):
        out.append("startup.log is empty, so the interface self-test left no "
                   "evidence that it ran at all")
    if f["startup_problem_count"]:
        out.append(f"{f['startup_problem_count']} distinct warning/error(s) in startup.log"
                   + (f" ({f.get('startup_line_count', 0)} lines)"
                      if f.get("startup_line_count", 0) > f["startup_problem_count"] else ""))
    if f["unverified_engines"]:
        out.append(f"{len(f['unverified_engines'])} unverified engine(s): "
                   + ", ".join(f["unverified_engines"][:6]))
    if f["gallery_figures"] and f["engines_swept"] and f["gallery_figures"] != f["engines_swept"]:
        out.append(f"gallery has {f['gallery_figures']} figures for "
                   f"{f['engines_swept']} engines")
    return out


def compare(now: dict, before: dict | None) -> list[str]:
    """What is different from the previous run. The point of the whole file."""
    if not before:
        return ["no previous run recorded — this is the baseline"]
    lines = []
    was, is_ = set(problems(before)), set(problems(now))
    for p in sorted(is_ - was):
        lines.append(f"NEW    {p}")
    for p in sorted(was - is_):
        lines.append(f"GONE   {p}")

    def moved(key: str, label: str, unit: str = "", tol: float = 0.0, worse_is_up=True):
        a, b = before.get(key), now.get(key)
        if a is None or b is None or a == b:
            return
        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
            if abs(b - a) <= tol:
                return
            arrow = "up" if b > a else "down"
            bad = (b > a) if worse_is_up else (b < a)
            lines.append(f"{'WORSE ' if bad else 'better'} {label} {a}{unit} -> {b}{unit} ({arrow})")
        else:
            lines.append(f"       {label} {a} -> {b}")

    moved("engines_swept", "engines swept", worse_is_up=False)
    moved("catalogue_engines", "catalogue engines", worse_is_up=False)
    moved("order_changed", "order-invariance: engines that changed")
    moved("startup_problem_count", "startup.log problems")
    # Timings bounce around; only report a real move.
    moved("kde_cold_ms", "KDE cold", " ms", tol=max(8.0, before.get("kde_cold_ms", 0) * 0.25))
    moved("check_seconds", "check phase", " s", tol=max(10.0, before.get("check_seconds", 0) * 0.25))
    moved("stage_total_mb", "staged size", " MB", tol=2.0)
    return lines or ["nothing measurable changed"]


def render(now: dict, history: list[dict]) -> str:
    before = history[-1] if history else None
    issues = problems(now)
    verdict = "OK" if not issues else f"{len(issues)} PROBLEM(S)"
    L = [f"# GraphVis build report — {verdict}", ""]
    L.append(f"- when: {now['when']}")
    L.append(f"- version {now['version']}  ·  {now['branch']}@{now['commit']}"
             f"  ·  {now['dirty']} uncommitted file(s)")
    L.append(f"- {now['engines_swept']} engines swept, {now['gallery_figures']} figures written"
             f", {now['catalogue_entries']} catalogue entries")
    L.append("")

    L.append("## Problems")
    L += [f"- {p}" for p in issues] if issues else ["- none"]
    L.append("")

    L.append("## Changed since the last run")
    L += [f"- {c}" for c in compare(now, before)]
    L.append("")

    L.append("## Numbers")
    L.append(f"- check phase: {now['check_seconds'] or '?'} s")
    L.append(f"- prepared-spec cache, KDE 4000 pts: cold {now['kde_cold_ms']} ms, "
             f"warm {now['kde_warm_ms']} ms")
    L.append(f"- order-invariance: {now['order_changed']} of {now['order_checked']} "
             f"engines change under a row shuffle")
    L.append(f"- staged build: {now['stage_total_mb']} MB")
    for name, mb in now["big_files_mb"].items():
        L.append(f"    - {name}  {mb} MB")
    L.append("")

    # THE AUDIT'S HEADLINE, so one file says how the build AND the source look.
    #
    # The passive audit writes its own page; repeating its top line here means a
    # build report is never read without also seeing whether the standing audit
    # found anything, which is the whole point of having it running.
    audit = REPORTS / "AUDIT.md"
    if audit.exists():
        head = audit.read_text(encoding="utf-8", errors="replace").splitlines()
        title = head[0].lstrip("# ").strip() if head else ""
        L.append("## Passive audit")
        L.append(f"- {title}  (build-reports/AUDIT.md)")
        # WHEN IT WAS RUN, because this page does not run it.
        #
        # The audit writes AUDIT.md on its own schedule and this report quotes
        # the top of it. A build report once carried "17 standing · nothing new"
        # from a pass two hours and several pushes old, stated in the present
        # tense beside numbers that were minutes old - which is the stale
        # startup.log again, one file along. The date line comes across too, and
        # an audit older than the check phase says so in as many words.
        stamp = next((l for l in head[1:6] if l.strip().startswith("- 20")), "")
        if stamp:
            L.append(f"  {stamp.strip().lstrip('- ')}")
        try:
            behind = time.time() - audit.stat().st_mtime
        except OSError:
            behind = 0.0
        if behind > max(2 * 3600.0, float(now.get("check_seconds") or 0) * 2):
            L.append(f"  NOT RE-RUN FOR THIS BUILD - AUDIT.md is "
                     f"{behind / 3600.0:.1f} h old, so the line above describes "
                     f"the source as it was then. Run PASSIVE-AUDIT.bat.")
        grab = False
        for line in head:
            if line.startswith("## New since the last pass"):
                grab = True
                continue
            if grab:
                if line.startswith("## "):
                    break
                if line.strip():
                    L.append("  " + line.strip())
        L.append("")

    if now["startup_problems"]:
        L.append("## startup.log — distinct messages")
        L += [f"    {l}" for l in now["startup_problems"]]
        L.append("")

    # PRINTED, NOT COUNTED. Keeping these out of the verdict is what stops a
    # permanently non-zero count from being ignored; printing them with the
    # reason is what stops the list from becoming a place things go to be
    # forgotten. If one of these ever stops being an artefact, it is still on
    # the page for somebody to notice.
    if now.get("startup_benign"):
        L.append("## startup.log — the program says the check caused these")
        for entry in now["startup_benign"]:
            L.append(f"    {entry}")
            key = re.sub(r"\s+\[x\d+\]$", "", entry)
            why = (now.get("startup_harness_reasons") or {}).get(key)
            if why:
                L.append(f"      why: {why}")
        L.append("")

    if len(history) >= 2:
        L.append("## Trend (most recent last)")
        L.append("| when | engines | problems | check s | KDE cold ms | staged MB |")
        L.append("|---|---|---|---|---|---|")
        for h in history[-6:] + [now]:
            L.append(f"| {h['when'][:16]} | {h.get('engines_swept','?')} "
                     f"| {len(problems(h))} | {h.get('check_seconds','?')} "
                     f"| {h.get('kde_cold_ms','?')} | {h.get('stage_total_mb','?')} |")
        L.append("")

    L.append("<!-- Written by tools/build_report.py. Sources: graph-check/, "
             "build/stage/, config/graph_catalogue.json. -->")
    return "\n".join(L) + "\n"


def main() -> int:
    REPORTS.mkdir(exist_ok=True)
    if "--mark-start" in sys.argv:
        (REPORTS / "started.txt").write_text(str(time.time()), encoding="utf-8")
        return 0
    history = []
    if HISTORY.exists():
        try:
            history = json.loads(HISTORY.read_text(encoding="utf-8"))
        except Exception:
            history = []
    now = collect()
    text = render(now, history)
    REPORT.write_text(text, encoding="utf-8")
    HISTORY.write_text(json.dumps((history + [now])[-KEEP_RUNS:], indent=1),
                       encoding="utf-8")
    if "--print" in sys.argv:
        sys.stdout.write(text)
    else:
        print(f"wrote {REPORT.relative_to(ROOT)} "
              f"({len(problems(now))} problem(s), {len(text)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
