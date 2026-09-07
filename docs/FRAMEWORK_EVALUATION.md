# Framework evaluation — GraphVis 18

A second ecosystem sweep was performed before the 18 rewrite.

## Decision

Keep **Qt Quick/QML + Rust/WGPU + Arrow/DataFusion + C++ VTK** as the production architecture.
No alternative provided a net gain large enough to justify losing Qt PDF, mature desktop
accessibility/windowing, QML tooling and direct VTK integration.

## Ideas adopted from alternatives

- **Slint / Iced / GPUI / Floem / Xilem:** fine-grained state-driven panels, command-centric UI,
  lightweight composition and avoiding monolithic controller-owned widgets.
- **Rerun:** workspace/blueprint thinking: the same scientific state can be shown through
  different view shells without duplicating datasets.
- **Tauri/Dioxus:** clear separation between application shell and optional service processes.
- **Qt 6.11 direction:** literature-first QML shell, command palette, scalable declarative
  components; the production baseline remains Qt 6.8+ so the release does not depend on
  beta/newer-only APIs.

## Experimental UI

The toolbar exposes a persistent **Experimental UI** toggle. Both shells share the same Rust
AppState, Arrow store and native renderers; changing shells does not reload data. The
experimental shell provides a navigation rail, command palette (`Ctrl+K`), `Ctrl+L` literature
shortcut, a larger literature-first home screen and the three-pane PDF/intelligence reader.
