# GraphVis — build and startup fixes (6 Sep 2026)

Working state reached: `build\stage-fast\graphvis.exe` starts and opens its
main window. `run.bat` (-> `tools\Dev-Run.ps1` -> `tools\Quick-Build.ps1`) is
the correct entry point and now works from a plain double-click.

Five separate defects were stacked on top of each other. Each one masked the
next, which is why it looked like a single unfixable "won't compile" problem.

## 1. Launch failed with a side-by-side configuration error

`sxstrace` said:

```
ManifestPath = C:\GraphVis\build\windows-fast\graphvis.exe
ERROR: Line 6: The required attribute level is missing from element requestedExecutionLevel.
```

The manifest embedded in the exe read:

```xml
<ms_asmv2:requestedExecutionLevel ms_asmv2:level="asInvoker" ms_asmv2:uiAccess="false"/>
```

The Windows loader only recognises an unprefixed `level` attribute. The
prefixes came from `mt.exe` re-serialising `graphvis.exe.manifest`, which mixed
namespaces - `<trustInfo xmlns="...asm.v2">` wrapping
`<requestedPrivileges xmlns="...asm.v3">`. That mixed form makes `mt` hoist
`asm.v2` into an `ms_asmv2:` prefix and stamp it onto the attributes.

**Fix.** New `app/graphvis.manifest` with a single-namespace `trustInfo` in
`asm.v3`. It is embedded verbatim as `RT_MANIFEST` resource 1 through a
generated `.rc`, with `/MANIFEST:NO /MANIFESTUAC:NO`, so neither `lld-link` nor
`mt.exe` can rewrite the XML. `enable_language(RC)` added to the top-level
`CMakeLists.txt`.

## 2. Module "GraphVis" contains no type named "Main"

The generated qmldir said `prefer :/GraphVis/`, so the module lived at
`qrc:/GraphVis/`. The engine only searches `qrc:/qt/qml`, so it was never
found. Qt policy QTP0001 was not set, and the default `RESOURCE_PREFIX` fell
back to `/`. `main.cpp` meanwhile refers to
`qrc:/qt/qml/GraphVis/assets/graphvis_icon.png`, so `/qt/qml` was the intent.

**Fix.** `RESOURCE_PREFIX "/qt/qml"` pinned explicitly on
`qt_add_qml_module`, so it cannot drift with policy again. Also removed a
subdirectory `cmake_minimum_required(VERSION 3.24)` and an `install(DIRECTORY)`
rule that copied the app's own QML module to disk, where it shadowed the copy
compiled into the exe.

## 3. The actual compile failure

```
qglobal.h(14): fatal error C1083: Cannot open include file: 'type_traits'
```

`cl.exe` finds the C++ standard library and Windows SDK through the
`INCLUDE`/`LIB`/`PATH` variables that `vcvars64.bat` sets - they are not passed
on the command line. `tools/Build-Environment.ps1` never entered the Visual
Studio environment, so the build only worked when launched from a Developer
Command Prompt. Double-clicking `build.bat` or `run.bat` could never work,
which is what "it keeps failing to compile" actually was.

**Fix.** `Initialize-GraphVisMsvcEnvironment` in `tools/Build-Environment.ps1`
locates Visual Studio via `vswhere` (with a directory-probe fallback) and
imports `vcvars64.bat` into the session. It no-ops if the environment is
already set. `Quick-Build.ps1` and `Build-Release.ps1` both route through it.

## 4. module "QtQuick.Pdf" is not installed

`LiteratureWorkspace.qml` imported `QtQuick.Pdf` for `PdfDocument` and
`PdfMultiPageView`. Qt PDF is not in this vcpkg set - in this baseline it ships
only inside the enormous `qtwebengine` port. A failed import fails the whole
type, so ClassicShell and then Main failed with it.

**Fix.** The Literature tab reads papers rather than displaying them -
extraction runs natively through `app.analyzeLiterature()`. The import,
`PdfDocument` and `PdfMultiPageView` were removed. The centre pane now shows
the open paper and its extraction results. The sidebar's title/author/page-count
labels came from `PdfDocument` and are now the filename and analysis status.
`Bookmark page` is gone, since there are no page numbers without a viewer.
`app/qml/lazy/PdfReader.qml.unused` holds the viewer split out as a
lazily-loaded file, unreferenced, if Qt PDF is ever added.

## 5. Required property app was not initialized

Every component declares `required property var app`, lowercase. But 23 call
sites passed `root.App` with a capital A, and `main.cpp` injected the
controller under `"App"`. QML yields `undefined` for an unknown property rather
than erroring, so the whole UI tree received `undefined` and every
`app.something` binding failed.

**Fix.** `root.App` -> `root.app` in `Main.qml` (4), `ControlSidebar.qml` (5),
`ExperimentalShell.qml` (6), `ClassicShell.qml` (6), `VisualizeWorkspace.qml`
(2), and `setInitialProperties` in `main.cpp` to `"app"`. `AppController` in
`VtkViewport.qml` is a different property and was left alone.

## Notes

- `build\windows-fast\graphvis.exe` still does not run, by design. That tree is
  not a deployed layout - no Qt QML modules and no `graphvis_ffi.dll` beside
  it. Run the staged build.
- C: was at 98% (11 GB free of 475 GB) during this work. Steps stalled for
  minutes at a time. `.tooling/vcpkg/buildtrees` is the usual thing to clear.
- Scratch files left in the repo root from earlier manifest debugging can be
  deleted: `manifest3.xml`, `manifest4.xml`, `manifest-test.*`, `embedded.*`,
  `manual-embedded.xml`, `validated.manifest`, `graphvis-manifest.xml`,
  `plugin-manifest.xml`, `extracted.manifest`, `graphvis-test.exe`,
  `sxstrace.*`, `dir`, `type`, `findstr`, `nuldir`, `crash_log.txt`.
- Diagnostic helpers added: `BUILD-AND-DIAGNOSE.bat` +
  `tools/Full-Diagnose.ps1` (build, run both layouts, dump the startup log),
  `verify-build.bat` + `tools/Verify-Manifest.ps1` (manifest check),
  `diagnose.bat` + `tools/Diagnose-Run.ps1`. Delete them when they stop
  earning their place.

## The Rust core was silently missing from the staged application

**Symptom.** Every build printed, buried among the install output:

```
CMake Error at build/windows-fast/cmake_install.cmake:57 (file):
  Syntax error in cmake code at .../cmake_install.cmake:57
  when parsing string
    C:\GraphVis\.cache\cargo-target/release/graphvis_ffi.dll
  Invalid character escape '\G'.
```

The build itself reported success and the application started, so this was
easy to read past. It is not cosmetic: the install script fails to parse at
that line, so `graphvis_ffi.dll` - the Rust/DataFusion core - is never copied
into the staged application.

**Cause.** `tools/Build-Environment.ps1` exports `CARGO_TARGET_DIR` as a native
Windows path with backslashes. `CMakeLists.txt` took it verbatim:

```cmake
set(GRAPHVIS_RUST_TARGET_DIR "$ENV{CARGO_TARGET_DIR}")
```

CMake stores strings unescaped, and `install(FILES ...)` writes the path
straight into the generated `cmake_install.cmake`, where `\G` is not a valid
escape sequence.

**Fix.** Convert it once, where it enters the build:

```cmake
file(TO_CMAKE_PATH "$ENV{CARGO_TARGET_DIR}" GRAPHVIS_RUST_TARGET_DIR)
```

**Lesson.** An environment variable holding a path is native-form by
definition. Any path from outside the build has to go through
`file(TO_CMAKE_PATH ...)` before it reaches a generated script.
