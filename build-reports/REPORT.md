# GraphVis build report — OK

- when: 2026-09-16T18:42:27+01:00
- version 18.4.0  ·  master@e35102a  ·  142 uncommitted file(s)
- 440 engines swept, 440 figures written, 2122 catalogue entries

## Problems
- none

## Changed since the last run
- GONE   2 distinct warning/error(s) in startup.log
- better startup.log problems 2 -> 0 (down)
- better KDE cold 72.3 ms -> 11.7 ms (down)
- WORSE  check phase 147.4 s -> 321.9 s (up)

## Numbers
- check phase: 321.9 s
- prepared-spec cache, KDE 4000 pts: cold 11.7 ms, warm 2.3 ms
- order-invariance: 169 of 389 engines change under a row shuffle
- staged build: 391.9 MB
    - graphvis_ffi.dll  127.4 MB
    - icudt78.dll  31.6 MB
    - vtkCommonCore-9.3.dll  11.7 MB
    - arrow.dll  10.6 MB

## Passive audit
- GraphVis passive audit — 17 standing  (build-reports/AUDIT.md)
  Nothing new. Everything below was already known.

## startup.log — known to be the check, not the program
    QFontDatabase: Cannot find font directory C:/GraphVis/build/stage/lib/fonts.
      why: emitted only under `-platform offscreen`, which is how --selftest-ui runs: Qt's offscreen plugin uses the generic font database and looks for a deployed lib/fonts, where the Windows plugin uses the system fonts and says nothing. Measured 2026-09-16: the offscreen check run at 17:45:57 logged it and a normal launch of the same binary at 18:08:58 did not.

## Trend (most recent last)
| when | engines | problems | check s | KDE cold ms | staged MB |
|---|---|---|---|---|---|
| 2026-09-16T13:25 | 440 | 1 | 151.0 | 14.8 | 391.9 |
| 2026-09-16T13:52 | 440 | 1 | 111.9 | 55.3 | 391.9 |
| 2026-09-16T16:09 | 440 | 0 | 351.1 | 12.9 | 391.9 |
| 2026-09-16T16:39 | 440 | 0 | 191.3 | 73.2 | 391.9 |
| 2026-09-16T17:10 | 440 | 1 | 235.4 | 11.6 | 391.9 |
| 2026-09-16T17:46 | 440 | 1 | 147.4 | 72.3 | 391.9 |
| 2026-09-16T18:42 | 440 | 0 | 321.9 | 11.7 | 391.9 |

<!-- Written by tools/build_report.py. Sources: graph-check/, build/stage/, config/graph_catalogue.json. -->
