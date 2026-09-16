# Build reports

One page per build, written by `tools/build_report.py`.

- `REPORT.md` — the latest: verdict, failures verbatim, what changed since the run before.
- `history.json` — the last 12 runs, so a trend can be seen.

Kept here rather than in `graph-check/`, which `CHECK-GRAPHS.bat` deletes at the
start of every run — a history kept there would be one run long, every time.
