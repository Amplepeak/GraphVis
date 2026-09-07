"""Optional GPU-accelerated GraphVis preview backends.

PyVista/PyVistaQt is preferred for glossy 3-D PBR rendering; VisPy is used for
million-point 2-D/3-D scatter previews. The Matplotlib render core remains the
reproducible publication/export backend.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import numpy as np


@dataclass(slots=True)
class GPUCapabilities:
    vispy: bool
    pyvista: bool
    pyvistaqt: bool


def capabilities() -> GPUCapabilities:
    try:
        import vispy  # noqa: F401
        vis = True
    except Exception:
        vis = False
    try:
        import pyvista  # noqa: F401
        pv = True
    except Exception:
        pv = False
    try:
        import pyvistaqt  # noqa: F401
        pvqt = True
    except Exception:
        pvqt = False
    return GPUCapabilities(vis, pv, pvqt)


def open_gpu_preview(spec, parent=None) -> Any:
    """Open an accelerated preview for the active PlotSpec.

    Returns the created dialog/window object. Raises RuntimeError when the
    optional GPU stack is unavailable or the active mapping is unsupported.
    """
    caps = capabilities()
    chart = str(spec.chart_type)
    if caps.pyvista and caps.pyvistaqt and any(k in chart.lower() for k in ("3d", "surface", "mesh", "volume", "iso", "slice", "quiver", "cone", "patch")):
        return _open_pyvista(spec, parent)
    if caps.vispy:
        return _open_vispy(spec, parent)
    raise RuntimeError("GPU preview requires VisPy or PyVista + PyVistaQt. Run update.bat to install optional acceleration packages.")


def _first_dataset(spec):
    return next(iter(spec.datasets.values()), None)


def _open_pyvista(spec, parent=None):
    import os
    os.environ.setdefault("QT_API", "pyside6")
    import pyvista as pv
    from pyvistaqt import QtInteractor
    from PySide6.QtWidgets import QDialog, QVBoxLayout

    dlg = QDialog(parent); dlg.setWindowTitle(f"GraphVis GPU / glossy preview — {spec.chart_type}"); dlg.resize(1000, 760)
    layout = QVBoxLayout(dlg); plotter = QtInteractor(dlg); layout.addWidget(plotter.interactor)
    ds = _first_dataset(spec)
    if ds is None:
        raise RuntimeError("No dataset is active.")
    chart = str(spec.chart_type)
    matrix_token = spec.mappings.get("matrix")
    arr = None
    if matrix_token:
        token = str(matrix_token); name = token.split(":", 1)[-1]
        for store in (getattr(ds, "volumes", {}), getattr(ds, "matrices", {}), getattr(ds, "tensors", {})):
            if name in store:
                arr = np.asarray(store[name], dtype=float); break
    if arr is not None and arr.ndim == 3:
        grid = pv.ImageData(dimensions=np.array(arr.shape) + 1)
        grid.cell_data["values"] = np.ascontiguousarray(arr).ravel(order="F")
        if "volume" in chart.lower() or "slice" in chart.lower():
            plotter.add_volume(grid.cell_data_to_point_data(), scalars="values", opacity="sigmoid", cmap=str(spec.colourmap).lower())
        else:
            level = float(np.nanmedian(arr)) if spec.volume_level is None else float(spec.volume_level)
            surf = grid.cell_data_to_point_data().contour([level], scalars="values")
            plotter.add_mesh(surf, cmap=str(spec.colourmap).lower(), smooth_shading=True, pbr=True, metallic=0.22,
                             roughness=0.28, ambient=0.22, diffuse=0.78, specular=0.65, specular_power=30,
                             opacity=float(spec.surface_alpha))
    else:
        cx, cy, cz = (spec.mappings.get(k) for k in ("x", "y", "z"))
        if cx in ds.df.columns and cy in ds.df.columns and cz in ds.df.columns:
            sub = ds.df[[cx, cy, cz]].replace([np.inf, -np.inf], np.nan).dropna()
            if len(sub) > 2_000_000:
                idx = np.linspace(0, len(sub) - 1, 2_000_000).astype(int); sub = sub.iloc[idx]
            pts = sub[[cx, cy, cz]].to_numpy(float)
            cloud = pv.PolyData(pts)
            if any(k in chart.lower() for k in ("surface", "mesh", "topography", "contour")) and len(pts) >= 6:
                try:
                    surf = cloud.delaunay_2d()
                    plotter.add_mesh(surf, color=spec.series_color, smooth_shading=True, pbr=True, metallic=0.18,
                                     roughness=0.24, ambient=0.20, diffuse=0.82, specular=0.72, specular_power=34,
                                     opacity=float(spec.surface_alpha))
                except Exception:
                    plotter.add_mesh(cloud, render_points_as_spheres=True, point_size=5.0, opacity=float(spec.surface_alpha),
                                     color=spec.series_color)
            else:
                plotter.add_mesh(cloud, render_points_as_spheres=True, point_size=5.0, opacity=float(spec.surface_alpha),
                                 color=spec.series_color)
        else:
            raise RuntimeError("GPU 3-D preview needs X/Y/Z columns or a selected 3-D volume.")
    plotter.add_axes(); plotter.show_grid(); plotter.enable_anti_aliasing("ssaa")
    try:
        light = pv.Light(position=(1.6, 1.2, 2.2), focal_point=(0, 0, 0), color="white", intensity=0.85)
        plotter.add_light(light)
        fill = pv.Light(position=(-1.2, -0.8, 0.6), focal_point=(0, 0, 0), color="white", intensity=0.28)
        plotter.add_light(fill)
    except Exception:
        pass
    try:
        plotter.enable_ssao(radius=0.4, bias=0.02, kernel_size=128)
    except Exception:
        pass
    plotter.camera_position = "iso"
    dlg._graphvis_plotter = plotter  # keep alive
    dlg.show(); return dlg


def _open_vispy(spec, parent=None):
    from PySide6.QtWidgets import QDialog, QVBoxLayout
    from vispy import scene
    from vispy.scene import visuals

    dlg = QDialog(parent); dlg.setWindowTitle(f"GraphVis VisPy preview — {spec.chart_type}"); dlg.resize(1000, 760)
    canvas = scene.SceneCanvas(keys="interactive", show=False, bgcolor="white")
    view = canvas.central_widget.add_view(); view.camera = "turntable" if "3d" in str(spec.chart_type).lower() else "panzoom"
    ds = _first_dataset(spec)
    if ds is None:
        raise RuntimeError("No dataset is active.")
    cols = [spec.mappings.get(k) for k in ("x", "y", "z")]
    valid = [c for c in cols if c in ds.df.columns]
    if len(valid) < 2:
        raise RuntimeError("VisPy preview needs at least X and Y numeric mappings.")
    use = valid[:3]; sub = ds.df[use].replace([np.inf, -np.inf], np.nan).dropna()
    if len(sub) > 3_000_000:
        sub = sub.iloc[np.linspace(0, len(sub) - 1, 3_000_000).astype(int)]
    pos = sub.to_numpy(np.float32)
    if pos.shape[1] == 2:
        pos = np.column_stack([pos, np.zeros(len(pos), dtype=np.float32)])
    markers = visuals.Markers(); markers.set_data(pos, face_color=spec.series_color, size=4, edge_width=0)
    view.add(markers); view.camera.set_range()
    layout = QVBoxLayout(dlg); layout.addWidget(canvas.native)
    dlg._graphvis_canvas = canvas
    dlg.show(); return dlg
