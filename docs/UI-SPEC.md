# GraphVis 18 — UI critique and redesign brief

Source: user review of GraphVis 18 against GraphVis 17, 6 Sep 2026, with
screenshots of both. This is the actionable form of that feedback.

## Product vision (the thing every decision is judged against)

GraphVis is a **scientific graph editor**. Creating graphs and visualising data
is the primary workflow. **Reading literature is the differentiator** — it is
what GraphVis does that other plotting tools do not, and it earns priority in
the layout, but it is not the product on its own.

## Keep and extend

Two things in v18 are better than v17 and must not be regressed:

1. **Read Literature.** The strongest part of the new product and its USP.
2. **The visualiser selector panel.** Faster and more responsive than the
   legacy graph library — *provided* docking and tool-switching reach parity
   with v17 (see D2).

---

## D1 — Cluttered and broken landing page

**Observed.** The landing page led with a marketing strapline ("Scientific
intelligence, not just plotting") and closed with six chips naming internal
technologies — Rust native core, WGPU direct GPU, Apache Arrow, DataFusion SQL,
VTK C++ / PBR, Arrow Flight. None were interactive. They competed for attention
with the only two actions on the page.

**Required.**
- Remove non-functioning placeholder text. Implementation detail is not a
  landing-page feature.
- One hierarchy: what this is → the two entry points → current state.
- Replace decoration with live state: datasets loaded, graphs available,
  paper currently open.

**Status: done.** `app/qml/workspaces/HomeWorkspace.qml` rewritten.

---

## D2 — Panels clipped, and the splitter would not travel

**Observed.** Panel text was cut off on the right (Variable Mapping, Data
workspace). The sidebar could not be dragged fully right; content was clipped
rather than the panel growing.

**Root cause.** Every panel was `ScrollView { ColumnLayout { width: parent.width } }`.
Inside a `ScrollView`, `parent` is the *content item*, not the viewport, so that
binding is self-referential: the column never learns the viewport width, content
overflows sideways, and a horizontal scrollbar appears instead of the text
wrapping. Separately the splitter had a minimum on the sidebar but no maximum,
and no minimum on the canvas, leaving the handle's travel undefined.

**Required.**
- Bind panel content to `ScrollView.availableWidth`, never `parent.width`.
- Horizontal scrollbar off; text wraps.
- Explicit `minimumWidth`/`maximumWidth` on both splitter panes.

**Status: done.** All five panels fixed; splitter travel bounded.

---

## D3 — Unreadable text

**Observed.** Group box titles struck through their own frame line
("Intelligent Auto-Optimization", "Native SQL / DataFusion", "5th axis / point
cloud · AUTO"). Disabled labels ("Point size", "Opacity %", "Density mode")
were too dim to read.

**Root cause.** The Basic style draws a `GroupBox` title directly on the frame
with no background — fine on light, struck-through on dark. Colour was
hard-coded as hex literals in every file, so contrast was never checked.

**Required.**
- A `GroupBox` whose title clears its frame.
- A single source of colour and typography.
- Disabled text must stay legible, not merely dimmed.

**Status: done.** Three separate causes, all fixed:

1. `GvGroupBox.qml` — title clears the frame.
2. `Theme.qml` singleton — one palette, no scattered hex literals.
3. The real cause of the invisible labels: Switch/CheckBox text, ComboBox and
   SpinBox contents are painted from the **palette**, not from any colour set
   per item. `Main.qml` now binds the whole window palette to `Theme`,
   including `palette.disabled.*` so disabled controls stay legible.
4. The Basic style hardcodes control backgrounds and ignores the palette, which
   is why combo boxes stayed white on a dark theme. `main.cpp` now selects
   **Fusion**, which is fully palette-driven.

Verified on screen: every previously invisible label ("Smart Render", "Point
size", "Opacity %", "Invert Opacity", "Density mode") is readable, and no
control is stuck in light mode.

---

## D4 — No tear-out docks

**Observed.** v17 could pop any dock or toolbar into a floating, resizable
window. v18 had fixed panels only.

**Required.** A reusable panel wrapper with compact chrome (~26 px, matching
v17's 22–24 px) and a pop-out control that reparents content into a real
window, preserving state, and docks it back.

**Status: component built** (`DockPanel.qml`), **not yet applied** to the
sidebar and canvas. That wiring is the next step.

---

## D5 — One theme instead of thirty

**Observed.** v17 shipped 30 themes (10 core + 20 accent variants). v18 had one
hard-coded dark palette.

**Required.** Same structure: base palettes × accents, selectable at runtime.

**Status: done.** `Theme.qml` provides 10 bases × 3 accents = 30, with a
selector in the toolbar. Two bases are light. **Not yet persisted** between
sessions.

---

## Remaining

| # | Item | State |
|---|---|---|
| D4 | Apply `DockPanel` to sidebar, canvas and graph library | to do |
| D5 | Persist the chosen theme via `QSettings` | to do |
| — | Migrate remaining hex literals in ControlSidebar, StatusStrip, GraphLibrary, workspaces to `Theme` | partial |
| — | Tool-switching parity: measure v18 tab switching against v17 dock swapping | not measured |

## Bugs found and fixed while doing this work

- `GvGroupBox` derived from itself. A find-and-replace that swapped `GroupBox`
  for `GvGroupBox` also rewrote the root element of `GvGroupBox.qml`, so the
  whole UI failed with "GvGroupBox is instantiated recursively".
- Fusion parses `&` in a control's text as a keyboard mnemonic, so
  "Analyze & Apply" rendered as "Analyze _Apply". Escaped to `&&` everywhere.
- Mapping rows had no column widths, so the label and combo overlapped
  ("Densit Raw points"). Labels now have a fixed width; fields fill the rest.
- `tools/Full-Diagnose.ps1` reported success from a *previous* run's log,
  because the wait loop only looked for `DONE` and never checked the log was
  new. It now stamps each run, and also kills a stale `graphvis.exe` that would
  otherwise hold a lock on the exe and fail the next link.
