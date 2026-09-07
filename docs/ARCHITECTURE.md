# GraphVis 18 architecture

## Runtime ownership

```text
Qt Quick / QML
   │
   ├── Stable scientific shell
   └── Experimental literature-first shell
             │
       AppController (thin Qt/native bridge)
             │
        Rust GraphVis core
        ├── AppState / Commands / PlotSpec / history
        ├── Arrow dataset store
        ├── DataFusion query engine
        ├── WGPU renderer + native geometry
        └── Arrow Flight worker
             │
       C++ VTK specialist renderer
```

Python, Julia and R are optional Arrow-speaking services. They do not own GraphVis state,
windows, datasets or rendering.

## Rendering lanes

**Native WGPU** is the interactive high-density point-cloud lane. WGPU owns a presentable
surface attached to the Qt child window. Camera uniforms, bind groups, depth buffers and
point buffers persist across frames. Point size, alpha and invert-opacity are uniforms;
dense clouds may be voxel-aggregated natively.

**VTK C++** is the specialist mesh/surface/PBR lane. Arrow IPC is read directly by Arrow C++.
Geometry changes rebuild the VTK pipeline; material changes update only `vtkProperty` and
never reread data or reset the camera.

## Data

Arrow `RecordBatch` is the canonical native representation. DataFusion provides SQL/query
execution. Query results are streamed directly to Arrow IPC rather than collected into an
intermediate result vector by the FFI path.

## State

Rust `AppState` is the authoritative project/history state. Dataset insertion and mapping
changes are commands and therefore participate in Undo/Redo and reproducible project-state
export.

## Literature

The native UI uses Qt PDF for reading, text search, selection/copy, navigation and zoom.
Optional literature extraction runs out-of-process through the Python science service and
returns small JSON control metadata plus saved numeric datasets that GraphVis can import
through its native data pipeline.
