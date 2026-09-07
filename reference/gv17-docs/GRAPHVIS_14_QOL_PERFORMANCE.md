# GraphVis 14 QoL, performance and MATLAB-style surface pass

Release: **2026.08.29.12**

## 1. Deliberate/tactical control application

Plot-setting dropdowns in the main GraphVis sidebar are staged. Selecting an item changes only the editor state; the graph continues to use the last committed value. A 25 px `▶` button beside the dropdown commits that one setting and schedules one render. **Apply all pending controls** commits all staged sidebar values in one render.

The same contract is used for the Interactive strip's Colourmap, Noise Method, Quiver/Streamplot and Marker dropdowns. Numeric sliders/spinboxes remain direct controls where immediate feedback is useful. Graph-library selection is also staged: search/tree selection highlights and centers the chosen entry without invoking Matplotlib; **▶ Apply selected visualisation** changes the rendering engine deliberately.

## 2. Dataset removal and recovery

`ProjectWorkspace.detach_dataset()` replaces destructive project removal as the normal path. The project manifest record is removed and the local project copy is moved from `datasets` to `detached_datasets`. This makes it invisible to recursive project auto-scan while preserving a simple restore path.

Permanent removal requires both:

1. `Permanently delete the project-local copied file(s) instead of moving them`
2. `I understand permanent deletion cannot be undone`

Even that mode does not remove the original external import source. Dataset membership is removed from Project Groups when the project copy is detached.

## 3. Project Groups

`ProjectGroupsDialog` is the dedicated grouping workspace. Each named group has independent checklists for Literature/Thesis, Datasets, and Scripts/Parameter Files plus notes. All/None helpers make large groups faster to configure. Membership is many-to-many: one file may be checked in any number of groups, and membership does not move, duplicate or delete files.

## 4. Surface failure masks and full-domain extrapolation

Dense kinetic/ODE sweeps often contain isolated NaN/Inf cells. Previous nearest-failure ownership could make one failure erase a large Voronoi-like region. The default failure footprint is now **Sample cell only**, derived from normalized source spacing and output pixel spacing. Stronger modes remain available:

- Sample cell only
- Conservative local region
- Voronoi failure region
- None (fit through failures)

Extrapolation is independently selectable:

- Mask outside convex hull
- Nearest fill outside hull
- IDW full-domain extension
- Linear + nearest full rectangle
- Edge-clamped full rectangle

All distance-based extension continues to operate in the same normalized linear/Log10 metric used by the estimator, so mixed Applied Voltage / Flow Rate domains do not become geometrically distorted.

## 5. MATLAB-style presentation

The MATLAB-style surface preset uses Turbo, continuous Gouraud/interpolated shading, automatic colorbar extension, compact failed-point handling and a complete rectangular edge fill. When Auto scientific surface presentation is enabled, 2-D fields use exact margins, outward ticks, thin spines, no generic dashed grid, and an automatic `Response: X vs Y` title unless the user supplies a custom title.

This is intentionally a presentation preset, not a data transformation. Convex-hull masking and other conservative policies remain one click away.

## 6. Loader and Windows branding

Startup progress now reports actual bootstrap milestones. The in-workspace LoadingOverlay advances an explicitly approximate `~NN%` when a third-party parser cannot report fine-grained progress; completion remains controlled only by the worker.

The startup and long-operation loaders use a procedural swimming otter with water and coral. Thirty variant seeds alter swim phase, paddle motion, spot pattern, bubbles/waves and coral placement. Immediate repeats are avoided.

On Windows, `SetCurrentProcessExplicitAppUserModelID("GraphVis.ScientificStudio")`, application/window icons, and a multi-resolution `assets/branding/graphvis.ico` give the taskbar and `.lnk` shortcuts GraphVis branding rather than the Python icon. Install/update runs `tools/create_desktop_shortcut.ps1`; `Create_Desktop_Shortcut.bat` can refresh it manually.

## 7. Performance changes

- Graph selection no longer queues a render on selection/search.
- Multiple staged dropdown edits can collapse into one render.
- Existing renderer cancellation/coalescing, estimator caching, smoothing-cache separation and progressive surface sliders remain active.
- Graph-tree categories remain lazy and result selection scrolls only the required category into view.
- Release packaging removes Python bytecode/cache directories.

## 8. Validation

Passed in the build environment:

- Python `compileall` across source/tools.
- All existing non-GUI smoke tests.
- New `graphvis14_qol_static_smoke.py`.
- New `surface_mask_extrapolation_smoke.py`.
- New `workspace_detach_restore_smoke.py`.
- Full Agg/headless graph sweep: **130 rendering engines, 0 renderer errors**.

The build container has no Windows PySide6 desktop session. Actual taskbar grouping, `.lnk` icon refresh, mouse/dock feel, live animation frame pacing, and end-user interaction remain target-Windows checks.
