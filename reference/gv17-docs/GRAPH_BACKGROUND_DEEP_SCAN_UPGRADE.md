# GraphVis 2026.08.29.8 — Graph Background, Deep Scan & Responsiveness Upgrade

## Graph workspace appearance

`View -> Graph background` now controls the **interactive** graph workstation independently of publication export:

- **Light** (new default): soft near-white workstation background with white plot axes.
- **White**: conventional fully white graph background.
- **Dark**: dark figure/axes background.
- **Theme**: follow the current GraphVis UI theme.
- **Custom colour**: user-selected base colour.
- **Brightness**: 0–100 lightness control for the selected graph-background mode.
- **Blend with UI theme** plus a user-set blend percentage.

GraphVis automatically adapts default tick, spine, grid, legend, title, axis-label and colourbar colours for contrast. Explicit user-selected styling colours are preserved. Export remains publication-controlled (white by default, or transparent when requested) instead of silently inheriting the workstation colour.

## Graph Library previews

Two preview options are available in the same View submenu:

- **Compact graph-library previews** uses tightly cropped release-time preview images so large white margins do not dominate the sidebar.
- **Merge preview background with UI** uses release-time RGBA preview assets with the near-white outer background converted to transparency.

Graph categories are now populated lazily: startup builds the category headings only, and graph rows/icons are instantiated when a category is expanded or an entry is selected through search.

## Resize and rendering responsiveness

- Qt/Matplotlib canvases now fill exposed resize regions with the selected graph background instead of the platform white default.
- Intermediate splitter resize events resize the Qt widget immediately but defer expensive Matplotlib figure geometry/redraw work until the drag settles.
- The in-memory render-cache key path was corrected so completed preview renders are stored under the same interactive-DPI key used for lookup.
- Existing fast-preview data/grid limits and single-worker Matplotlib safety remain in place.

## Smart Map Suite / deep scan

The scan budget accepts **Seconds, Minutes, or Hours**, up to 72 hours. Longer budgets progressively lift column, row and pairwise-analysis limits rather than stopping at the old short ceiling.

Deep scan work now has a resumable checkpoint under the active project's scan context. Column profiles and pairwise metrics are saved periodically, on budget/cancellation boundaries, and at successful completion. A later scan with the same dataset + literature fingerprint can reuse completed work. Short/medium scans now sample raw rows before expensive numeric conversion and limit variable profiling to the budgeted breadth; explicit 6+ hour scans can still inspect every row/column. Tools -> Clear Smart Suite saved scans removes these project-local caches when desired.

## Literature extraction

Literature extraction has its own Seconds/Minutes/Hours budget (up to 72 hours). PDF page reading and OCR obey the deadline/cancellation signal. OCR is no longer constrained by the old fixed 40-page cap when the user gives it a sufficiently large time budget.

Extracted literature text is stored in a compressed project cache. Normal startup/context loading reads only the 50k excerpt; features that need full literature text request and decompress it explicitly. This avoids repeatedly rereading or decompressing large documents during ordinary project startup.

## Arbitrary export DPI

Single export, batch export, default export settings, and Publication Ready profiles now accept a typed **positive integer DPI without a 3000-DPI ceiling**. Very high raster resolutions are not blocked, but GraphVis reports estimated peak RAM and raw RGBA image size before compression and warns that final file size/render time can increase sharply.

## Review notes / retained design choices

- Matplotlib figure building/export stays serialized on one worker because concurrent Matplotlib rendering is not reliably thread-safe. Increasing thread count here would trade small speed gains for crash/corruption risk.
- Cross-session pickled Matplotlib figure caching stays opt-in because write/read I/O can cost more than rerendering for small plots. The corrected in-memory cache remains the normal fast path.
- Full-quality export is intentionally separated from interactive fast preview so UI responsiveness does not reduce publication output quality.
