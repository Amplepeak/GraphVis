# =========================================================================
# main_app.py — GraphVis scientific visualisation and analysis desktop app
# =========================================================================
from __future__ import annotations

from typing import TYPE_CHECKING

import copy
import difflib
import json
import os
from pathlib import Path
import sys
import shutil
import time

import numpy as np
import pandas as pd
from PySide6.QtCore import Qt, QTimer, QThreadPool, QSize, QFileSystemWatcher
from PySide6.QtGui import QAction, QActionGroup, QColor, QIcon, QKeySequence, QPalette, QPixmap, QShortcut
from PySide6.QtWidgets import (QAbstractItemView, QApplication, QCheckBox, QComboBox, QDialog, QDialogButtonBox,
                               QDoubleSpinBox, QFileDialog, QFormLayout, QGroupBox, QHBoxLayout, QLabel, QLineEdit,
                               QListWidget, QListWidgetItem, QMainWindow, QMessageBox, QPlainTextEdit, QPushButton,
                               QScrollArea, QSlider, QSpinBox, QSplitter, QSplashScreen, QTabWidget, QVBoxLayout, QWidget, QInputDialog, QMenu, QToolBar, QTabBar, QDockWidget, QSizePolicy, QStackedWidget, QFrame, QToolButton, QColorDialog, QCompleter, QWidgetAction, QAbstractSpinBox)

from graphvis.analysis.limits import DEFAULT_LIMIT_OPTIONS
from graphvis.rendering.colormaps import COLOURMAPS, populate_colormap_combo
from graphvis.core.commands import CommandHistory
from graphvis.data.loader import (CACHE_DIR, CHART_TYPES, PRIORITY_KEYS, Dataset, LiteratureBatchTask,
                         LiteratureTask, LoadTask, CancellableTask, chart_capabilities, get_pretty_label, load_csv_dataset, load_variable_aliases,
                         refresh_alias_cache, save_variable_aliases, set_context_aliases, suggest_axis_mapping, submit, load_excel_workbook)
from graphvis.core.diagnostics import (DebugDrawer, FreezeWatchdog, SettingsDialog, app_settings, install_exception_hooks, log_line,
                         safe_window_geometry)
from graphvis.core.session_diagnostics import record_activity
from graphvis.analysis.electro import intelligent_visualization_advisor
from graphvis.analysis.intelligent_scan import (scan_dataset as deep_scan_dataset, save_scan, load_cached_scan,
                                                 best_mapping_for_graph, dataset_fingerprint,
                                                 save_scan_checkpoint, load_scan_checkpoint)
from graphvis.literature.extractor import LiteratureExtraction, load_extraction_sidecars
from graphvis.rendering.plotting_engine import (ScientificPlotCanvas, clear_figure_cache, manual_apply_for,
                                                manual_apply_mode, HEAVY_POOL)
from graphvis.rendering.graph_library import GRAPH_LIBRARY
from graphvis.core.workspace import active_project, create_project, open_project
from graphvis.core.project_context import ProjectContextStore, ProjectGroup, analyse_script_text, request_ai_graph_advice
from graphvis.data.variable_context import parse_variable_context, infer_display, conversions_for, replace_unit, extract_unit
from graphvis.rendering.export_code import export_python, export_matlab
from graphvis.rendering.render_core import (AXIS_SCALE_OPTIONS, DEFAULT_LIMIT_STYLE, DEFAULT_STYLING, FIFTH_AXIS_MODES, STYLE_ELEMENTS, PlotSpec,
                         SURFACE_CACHE, PARETO_CACHE, SURFACE_FIELD_CHARTS, axis_roles, canonical_axis_scale, legacy_scale_to_axes)
from graphvis.rendering.surface_estimators import ESTIMATOR_CATEGORIES, canonical_estimator
from graphvis.views.widgets import (ColorButton, GraphLibraryDialog, GraphPicker, MutedSuffixDelegate, SidebarSplitter, StatusPill, install_typing_commit,
                     ControlSidebarDock, DetachablePanel, LoadingOverlay, install_dock_titlebar, stacked_rectangles_icon, compact_tool_icon, CompactSplitter, InstructionsDialog)
from graphvis.models.application_model import GraphVisApplicationModel
from graphvis.views.dialogs import ParameterNicknameDialog, PublicationProfileDialog, LayerManagerDialog
from graphvis.core.native_figure import save_native_figure, load_native_figure
from graphvis.core.publication import PublicationProfile, BUILTIN_PROFILES
from graphvis.rendering.export import BatchExportTask, EXPORT_FORMATS, DPI_TIERS, memory_verdict
from graphvis.views.themes import (THEMES, THEME_GROUPS, canonical_theme_name, colours_for,
                                   blend_colours, colour_with_lightness, colour_lightness,
                                   contrasting_graph_colours)
from graphvis.core.paths import ASSETS_DIR, INSTALL_ROOT
from graphvis.views.pro_suite_dialogs import (AnalysisHubDialog, BatchProcessingDialog, DataConnectorDialog,
                                     ObjectManagerDock, SpreadsheetDialog, WorkflowBuilderDialog)
from graphvis.views.project_context_dialogs import ProjectGroupsDialog, ScriptImportDialog, AIAdvisorSettingsDialog

APP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SESSION_FILE = os.path.join(CACHE_DIR, "session_state.json")
LOGO_FILE = str(ASSETS_DIR / "graphvis_logo.png")
ICON_FILE = str(ASSETS_DIR / "graphvis_icon.png")
APP_NAME = "GraphVis"
EXPRESSION_CHARTS = {
    "Function Plot", "Function 3D Parametric", "Implicit Function", "Implicit Surface",
    "Function Contour", "Function Surface", "Function Mesh",
}

CHART_DESCRIPTIONS = {
    "Line Chart": "Trajectory of Y against X. Map a 4th axis to colour the line by a third variable.",
    "4D / 5D Scatter": "X–Y scatter with a 4th variable on a colour gradient and a 5th on marker size, opacity or geometry.",
    "3D Scatter": "X–Y–Z point cloud, optionally coloured (4th) and sized (5th). Drag to rotate, pinch to zoom.",
    "Pareto Front": "Non-dominated trade-off frontier between two objectives with a bootstrap confidence band.",
    "2D Heatmap": "Smoothly interpolated colour field of Z over two swept parameters (gouraud shading + Gaussian filter).",
    "2D Contour": "Filled iso-lines of Z over two swept parameters.",
    "3D Topography / Surface": "3-D response surface of Z; a 4th axis colours the surface by another variable.",
    "Hexbin Density": "Hexagonally binned sample density (or mean of Z per cell) for very large sample sets.",
    "Global Sensitivity": "Standardised regression coefficients and Spearman ρ of every parameter on the response.",
    "1D Marginal Responses": "Mean ± σ of the response against each sampled parameter, one panel per parameter.",
    "sCOD Degradation Profile": "Median sCOD trajectory with a 10–90 % band across Monte-Carlo samples (profile matrix).",
    "VFA Concentration Profile": "Median VFA trajectory with a 10–90 % band across Monte-Carlo samples (profile matrix).",
    "Polarisation & Power Curve": "X = current density, Y = cell voltage; power density on a twin axis with the peak marked.",
    "EIS: Nyquist": "X = Z′, Y = Z″ with a semicircle fit reporting Rs and Rct.",
    "EIS: Bode": "X = frequency, Y = Z′, Z = Z″ → |Z| and phase against log frequency.",
    "Gompertz H₂ Kinetics": "X = time, Y = cumulative H₂; modified Gompertz fit reporting P, Rm and λ.",
}
for _entry in GRAPH_LIBRARY:
    if _entry.get("engine"):
        CHART_DESCRIPTIONS.setdefault(_entry["engine"], _entry["description"])



class AIProjectAdvisorTask(CancellableTask):
    def __init__(self, endpoint: str, model: str, api_key: str, project_summary: str, dataset_schema: dict):
        super().__init__("ai-project-advisor")
        self.endpoint, self.model, self.api_key = endpoint, model, api_key
        self.project_summary, self.dataset_schema = project_summary, dataset_schema

    def execute(self):
        self.stage("Sending compact project context to AI advisor…", 15)
        result = request_ai_graph_advice(self.endpoint, self.model, self.api_key, self.project_summary, self.dataset_schema)
        self.stage("AI advisor response received", 100)
        return result


class DatasetScanTask(CancellableTask):
    """Background deep scan; scientific work stays off the Qt GUI thread."""
    def __init__(self, dataset: Dataset, literature: list[LiteratureExtraction], cache_dir: str, budget_seconds: float = 30.0):
        super().__init__(f"scan-dataset:{dataset.name}")
        self.dataset = dataset
        self.literature = list(literature or [])
        self.cache_dir = cache_dir
        self.budget_seconds = max(3.0, float(budget_seconds))

    def execute(self):
        resume = load_scan_checkpoint(self.dataset, self.cache_dir, self.literature)
        result = deep_scan_dataset(
            self.dataset, self.literature,
            progress=lambda msg, pct: self.stage(msg, pct),
            cancelled=lambda: self._cancelled,
            time_budget_seconds=self.budget_seconds,
            resume_state=resume,
            checkpoint=lambda state: save_scan_checkpoint(state, self.cache_dir, self.dataset, self.literature),
        )
        if self._cancelled:
            raise RuntimeError("Cancelled")
        save_scan(result, self.cache_dir, self.dataset)
        return result


class SmartSuiteTask(CancellableTask):
    """Deep project-group analysis used by Smart Suite.

    Each dataset gets a share of the user-selected thinking budget. Cached
    scans are reused when they are at least as deep as the requested budget.
    The task returns recommendations only; Qt tabs are created on the GUI
    thread after analysis finishes.
    """
    def __init__(self, datasets: list[Dataset], literature: list[LiteratureExtraction], cache_dir: str,
                 total_budget_seconds: float = 30.0, ai_advice: dict | None = None):
        super().__init__("smart-suite")
        self.datasets = list(datasets)
        self.literature = list(literature or [])
        self.cache_dir = cache_dir
        self.total_budget_seconds = max(5.0, min(float(total_budget_seconds or 30.0), 259200.0))
        self.ai_advice = dict(ai_advice or {})

    def execute(self):
        if not self.datasets:
            return {"recommendations": [], "scans": {}}
        per_dataset = max(3.0, self.total_budget_seconds / max(len(self.datasets), 1))
        scans: dict[str, dict] = {}
        merged: list[dict] = []
        for i, ds in enumerate(self.datasets):
            if self._cancelled:
                raise RuntimeError("Cancelled")
            base = int(5 + 80 * i / max(len(self.datasets), 1))
            self.stage(f"Smart Suite: analysing {ds.name}…", base)
            cached = load_cached_scan(ds, self.cache_dir, self.literature)
            if cached and float(cached.get("time_budget_seconds", 0.0)) >= per_dataset * 0.85:
                result = cached
            else:
                resume = load_scan_checkpoint(ds, self.cache_dir, self.literature)
                result = deep_scan_dataset(
                    ds, self.literature,
                    progress=lambda msg, pct, b=base: self.stage(msg, min(90, b + int(pct / max(len(self.datasets), 1)))),
                    cancelled=lambda: self._cancelled,
                    time_budget_seconds=per_dataset,
                    resume_state=resume,
                    checkpoint=lambda state, current=ds: save_scan_checkpoint(state, self.cache_dir, current, self.literature),
                )
                save_scan(result, self.cache_dir, ds)
            scans[ds.name] = result
            for rec in result.get("recommendations", []):
                row = dict(rec); row["dataset_name"] = ds.name
                merged.append(row)
        # If the user previously ran the optional AI Project Advisor, fold its
        # cached project interpretation into the deterministic scan.  No network
        # call occurs here: Smart Suite remains offline/reproducible unless the
        # user explicitly runs the API advisor first.
        ai_result = self.ai_advice.get("result", self.ai_advice) if isinstance(self.ai_advice, dict) else {}
        if isinstance(ai_result, dict) and ai_result.get("recommendations"):
            engine_names = list(CHART_TYPES)
            global_axis = {"x": ai_result.get("x_variable"), "y": ai_result.get("y_variable"), "z": ai_result.get("z_variable")}
            for ai_rec in ai_result.get("recommendations", [])[:8]:
                if not isinstance(ai_rec, dict):
                    continue
                requested = str(ai_rec.get("graph", "")).strip()
                if not requested:
                    continue
                exact = next((g for g in engine_names if g.lower() == requested.lower()), None)
                graph = exact or next(iter(difflib.get_close_matches(requested, engine_names, n=1, cutoff=0.58)), None)
                if not graph:
                    continue
                # Attach the AI recommendation to the first dataset whose
                # columns can support its named axes; never invent variables.
                for ds in self.datasets:
                    cols = [str(c) for c in ds.df.columns]
                    mappings = {}
                    for role, wanted in global_axis.items():
                        if not wanted:
                            continue
                        direct = next((c for c in cols if c.lower() == str(wanted).lower()), None)
                        match = direct or next(iter(difflib.get_close_matches(str(wanted), cols, n=1, cutoff=0.55)), None)
                        if match:
                            mappings[role] = match
                    merged.append({
                        "graph": graph, "score": 0.955, "mappings": mappings,
                        "reason": f"AI project-context advisor: {ai_rec.get('reason','context-aware recommendation')}",
                        "source": "dataset+literature+ai", "dataset_name": ds.name,
                        "diagnostics": {"recommended_colourmap": "viridis" if any(k in graph.lower() for k in ("heat", "surface", "contour", "density")) else "parula",
                                        "recommended_series_color": "#2E86C1", "recommended_scale": "Linear Scale"},
                    })
                    break

        merged.sort(key=lambda r: float(r.get("score", 0.0)), reverse=True)
        # Prefer both graph AND mapping diversity. The previous advisor could
        # select several unrelated graph names with identical axes, which made
        # the output feel arbitrary rather than analytical.
        # allowed only after several distinct views have already been selected.
        chosen: list[dict] = []
        counts: dict[str, int] = {}
        mapping_counts: dict[tuple, int] = {}
        for rec in merged:
            graph = str(rec.get("graph", ""))
            if not graph or counts.get(graph, 0) >= 1:
                continue
            mp = rec.get("mappings") or {}
            sig = tuple(mp.get(k) for k in ("x", "y", "z") if mp.get(k))
            if sig and mapping_counts.get(sig, 0) >= 2:
                continue
            chosen.append(rec); counts[graph] = 1
            if sig: mapping_counts[sig] = mapping_counts.get(sig, 0) + 1
            if len(chosen) >= 7:
                break
        self.stage("Smart Suite: finalising graph plan…", 96)
        return {"recommendations": chosen, "scans": scans, "budget_seconds": self.total_budget_seconds}


# ====================================================================
# dialogs
# ====================================================================
class AliasEditorDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Variable Alias Manager")
        self.resize(560, 420)
        lay = QVBoxLayout(self)
        lay.addWidget(QLabel("<b>Descriptive labels for simulation keys:</b>"))
        self.aliases = load_variable_aliases()
        form = QFormLayout()
        self.inputs = {}
        for k, v in self.aliases.items():
            ed = QLineEdit(v)
            form.addRow(k, ed)
            self.inputs[k] = ed
        scroll = QScrollArea()
        w = QWidget(); w.setLayout(form)
        scroll.setWidget(w); scroll.setWidgetResizable(True)
        lay.addWidget(scroll)
        buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)
        lay.addWidget(buttons)

    def _save(self):
        save_variable_aliases({k: ed.text().strip() for k, ed in self.inputs.items()})
        refresh_alias_cache()
        self.accept()


class IntelligentAdvisorDialog(QDialog):
    def __init__(self, advice_list, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Intelligent Visualisation Advisor")
        self.resize(760, 520)
        self.selected_chart = None
        lay = QVBoxLayout(self)
        lay.addWidget(QLabel("<b>Recommended visualisations & scientific justification</b> "
                             "(scanned: active dataset + ingested literature)"))
        self.list = QListWidget()
        self.list.setWordWrap(True)
        for chart, why in advice_list:
            self.list.addItem(f"【 {chart} 】\n{why}\n")
        if self.list.count():
            self.list.setCurrentRow(0)
        lay.addWidget(self.list)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.button(QDialogButtonBox.Ok).setText("Select recommended visualisation")
        buttons.button(QDialogButtonBox.Ok).setToolTip("Stage this recommendation in Graph Library. Press the nearby ▶ Apply button when you deliberately want to render it.")
        buttons.accepted.connect(self._accept)
        buttons.rejected.connect(self.reject)
        lay.addWidget(buttons)

    def _accept(self):
        item = self.list.currentItem()
        if item:
            self.selected_chart = item.text().split("\n")[0].replace("【", "").replace("】", "").strip()
        self.accept()


class LiteratureResultDialog(QDialog):
    """Shows what was extracted and lets the user overlay / recreate graphs."""

    def __init__(self, ext: LiteratureExtraction, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Literature extraction result")
        self.resize(720, 520)
        self.ext = ext
        self.choice = None
        lay = QVBoxLayout(self)
        txt = QPlainTextEdit(ext.summary())
        txt.setReadOnly(True)
        lay.addWidget(txt)
        self.list = QListWidget()
        for i, d in enumerate(ext.datasets):
            cols = ", ".join(map(str, d.df.columns[:6])) + (" …" if len(d.df.columns) > 6 else "")
            item = QListWidgetItem(f"{d.name}  [{d.kind_hint}]  {len(d.df)} rows — {cols}")
            item.setData(Qt.UserRole, i)
            self.list.addItem(item)
        if self.list.count():
            self.list.setCurrentRow(0)
        lay.addWidget(QLabel("Extracted datasets (select one):"))
        lay.addWidget(self.list)
        row = QHBoxLayout()
        b_overlay = QPushButton("Overlay on live preview")
        b_overlay.clicked.connect(lambda: self._done("overlay"))
        b_recreate = QPushButton("Recreate graph in new tab")
        b_recreate.clicked.connect(lambda: self._done("recreate"))
        b_predict = QPushButton("Recreate + predictive band")
        b_predict.setToolTip("Use the semantically inferred X/Y mapping and fit a conservative extrapolation band beyond the extracted data.")
        b_predict.clicked.connect(lambda: self._done("recreate_predictive"))
        b_all = QPushButton("Recreate all in tabs")
        b_all.clicked.connect(lambda: self._done("recreate_all"))
        b_close = QPushButton("Close")
        b_close.clicked.connect(self.reject)
        for b in (b_overlay, b_recreate, b_predict, b_all, b_close):
            row.addWidget(b)
        lay.addLayout(row)

    def selected_index(self):
        item = self.list.currentItem()
        return item.data(Qt.UserRole) if item else None

    def _done(self, choice):
        self.choice = choice
        self.accept()


# ====================================================================
# main window
# ====================================================================
class GraphVisWindow(QMainWindow):
    """Qt View layer. Domain state lives in GraphVisApplicationModel; user intents are delegated to the injected Controller."""
    def __init__(self, model: GraphVisApplicationModel | None = None):
        super().__init__()
        self.setWindowTitle("GraphVis — Scientific Visualisation & Analysis")
        if os.path.exists(ICON_FILE):
            self.setWindowIcon(QIcon(ICON_FILE))
        elif os.path.exists(LOGO_FILE):
            self.setWindowIcon(QIcon(LOGO_FILE))
        safe_window_geometry(self, 1680, 1000)
        install_exception_hooks()
        record_activity("Constructing main window")
        self.settings = app_settings()
        saved_geometry = self.settings.value("ui/mainwindow_geometry")
        if saved_geometry:
            try:
                self.restoreGeometry(saved_geometry)
            except Exception as exc:
                log_line(f"Window geometry restore skipped: {exc}", "UI")

        self.app_model = model or GraphVisApplicationModel(active_project())
        self.datasets: dict[str, Dataset] = self.app_model.datasets
        self.literature: list[LiteratureExtraction] = []
        self._active_background_task = None
        self._suppress_auto_render = True
        self._auto_scan_in_progress = False
        self.current_experimental_overlay = None
        self.literature_overlays: list[dict] = []
        self.styling = copy.deepcopy(DEFAULT_STYLING)
        self.limit_style = dict(DEFAULT_LIMIT_STYLE)
        self._pending_loads = 0
        self._session_restored = False
        self._applying_state = False
        # Dropdown changes are tactical/staged: choosing an item does not mutate
        # the rendered graph until its play button (or Apply all) is pressed.
        self._staged_combos: list[QComboBox] = []
        self._pending_graph_entry: dict | None = None
        context_source = self.settings.value("variable_context/source", "", str)
        self.variable_context: dict[str, dict] = parse_variable_context(context_source) if context_source else {}
        if self.variable_context:
            set_context_aliases({k: (f"{v.get('name', k)} [{v.get('unit')}]" if v.get('unit') else v.get('name', k))
                                 for k, v in self.variable_context.items()})
        self.project = self.app_model.project
        self.project_context = ProjectContextStore(self.project)
        self.scan_cache_dir = self.project.context_dir / "scans"
        self.scan_cache_dir.mkdir(parents=True, exist_ok=True)
        self._scan_results: dict[str, dict] = {}
        # Reload already-read literature context from the active project. The
        # source PDF/thesis does not need to be selected or parsed again.
        self.literature = self.project_context.load_literature()
        self.dataset_dir = str(self.project.datasets_dir)
        # One-time migration: copy legacy datasets into the active project so
        # startup auto-loading is project-local and reproducible.
        legacy_dir = os.path.join(str(INSTALL_ROOT), "datasets")
        if os.path.isdir(legacy_dir) and not list(self.project.datasets_dir.iterdir()):
            for fn in os.listdir(legacy_dir):
                src = os.path.join(legacy_dir, fn)
                if os.path.isfile(src):
                    try: self.project.ingest_file(src, "datasets")
                    except Exception: pass
        os.makedirs(self.dataset_dir, exist_ok=True)
        self.source_watcher = QFileSystemWatcher(self)
        self.source_watcher.fileChanged.connect(self._on_source_file_changed)
        self._live_reload_pending: set[str] = set()
        self._watch_source_map: dict[str, str] = {}  # original external source -> project-local dataset
        self.live_telemetry_enabled = self.settings.value("telemetry/enabled", False, bool)
        QThreadPool.globalInstance().setMaxThreadCount(max(4, QThreadPool.globalInstance().maxThreadCount()))

        # silent watchdog — logs only, never opens UI
        self.watchdog = FreezeWatchdog(threshold_sec=self.settings.value("watchdog/threshold", 3.0, float), parent=self)
        self.watchdog.set_enabled(self.settings.value("watchdog/enabled", True, bool))
        self.watchdog.frozen_detected.connect(self._on_freeze_logged)
        self.watchdog.recovered.connect(self._on_freeze_recovered)
        self.watchdog.start()

        self.history = CommandHistory(self)
        self._debounce = QTimer(self)
        self._debounce.setSingleShot(True)
        self._debounce.setInterval(140)
        self._debounce.timeout.connect(self._on_debounce)
        # Surface sliders can request an intentionally coarse worker render
        # while the handle is moving, then a full-fidelity render on release.
        # The separate timer prevents high-frequency slider events from flooding
        # the render pool.
        self._surface_drag_active = False
        self._surface_drag_debounce = QTimer(self)
        self._surface_drag_debounce.setSingleShot(True)
        self._surface_drag_debounce.setInterval(110)
        self._surface_drag_debounce.timeout.connect(self._render_progressive_surface_preview)
        # GraphVis 16 instant auto-apply: dropdown changes commit automatically
        # after a short debounce (no per-combo ▶ press needed). The last
        # successfully rendered UI snapshot is retained so a cancelled or
        # failed render can roll every control back to its confirmed state.
        self._staged_commit_timer = QTimer(self)
        self._staged_commit_timer.setSingleShot(True)
        self._staged_commit_timer.setInterval(420)
        self._staged_commit_timer.timeout.connect(self._commit_pending_staged)
        self._last_good_ui_state: dict | None = None

        # Graph-library thumbnail preferences are consumed while the sidebar is
        # constructed. Seed the application properties first so the picker does
        # not need an immediate second rebuild during initial theme setup.
        app = QApplication.instance()
        if app is not None:
            app.setProperty("graphvis_compact_previews", self.settings.value("graph_library/compact_previews", True, bool))
            app.setProperty("graphvis_merge_previews", self.settings.value("graph_library/merge_previews", True, bool))

        self._build_ui()
        self._build_quick_toolbar()
        # Numeric-input reliability: typed values commit on Enter/focus-out
        # (no per-keystroke intermediate values corrupting min/max ranges),
        # and every numeric box keeps its embedded step arrows.
        for _spin in self.findChildren(QAbstractSpinBox):
            _spin.setKeyboardTracking(False)
            _spin.setButtonSymbols(QAbstractSpinBox.UpDownArrows)
            install_typing_commit(_spin)
        # Pristine baseline for the "Reset all controls" action.
        self._default_ui_state = None
        try:
            self._default_ui_state = self.snapshot_ui_state()
        except Exception:
            pass
        self.object_manager = ObjectManagerDock(self)
        self.object_manager.setObjectName("GraphVisObjectManager")
        install_dock_titlebar(self.object_manager, "Object Manager")
        self.addDockWidget(Qt.RightDockWidgetArea, self.object_manager)
        self._build_window_toolbar()
        self._build_menus()
        if self.settings.value("ui/show_diagnostics", False, bool):
            self.set_diagnostics_visible(True)
        self.object_manager.setVisible(self.settings.value("ui/object_manager", True, bool))
        # Recreate any sidebar sections that were left popped out before Qt
        # restores the main-window state.  QMainWindow.saveState can only place
        # docks that already exist when restoreState is called.
        self._restore_detachable_panel_shells()
        saved_dock_state = self.settings.value("ui/mainwindow_state")
        if saved_dock_state:
            try:
                self.restoreState(saved_dock_state)
            except Exception as exc:
                log_line(f"Dock layout restore skipped: {exc}", "UI")
        self._floating_windows: list[QMainWindow] = []
        self._connector_last_refresh: dict[str, float] = {}
        self._connector_refresh_timer = QTimer(self)
        self._connector_refresh_timer.setInterval(1000)
        self._connector_refresh_timer.timeout.connect(self._refresh_due_connectors)
        self._connector_refresh_timer.start()
        self._build_shortcuts()
        self.controller = None
        theme_name = canonical_theme_name(self.settings.value("ui/theme", "Light", str))
        self.apply_theme(theme_name)
        self._refresh_display_aliases()
        self.history.baseline(self.snapshot_ui_state())
        QTimer.singleShot(150, self.auto_load_datasets_from_folder)
        QTimer.singleShot(250, self.refresh_object_manager)
        log_line("Application started.", "INFO")

    def attach_controller(self, controller) -> None:
        """Dependency-injection boundary used by the bootstrap module."""
        self.controller = controller

    def _refresh_active_group_label(self) -> None:
        if not hasattr(self, "project_context"):
            return
        group = self.project_context.get_group()
        text = f"Active group: {group.name}" if group else "Active group: Project Overview (not yet configured)"
        if hasattr(self, "lbl_active_group"):
            self.lbl_active_group.setText(text)
            self.lbl_active_group.setToolTip("Project Groups controls which thesis/papers, datasets and scripts Smart Map Suite considers together.")

    def _set_active_background_task(self, task, label: str = "Working…") -> None:
        self._active_background_task = task
        if hasattr(self, "preview_canvas"):
            overlay = self._operation_overlay()
            # Global >5 s feedback rule: any registered background operation
            # automatically arms the otter indicator if its caller did not.
            if not overlay.is_armed():
                overlay.start(label, cancellable=True)
            else:
                overlay.set_cancellable(True)
        self.statusBar().showMessage(label, 0)

    def _clear_active_background_task(self) -> None:
        self._active_background_task = None
        if hasattr(self, "preview_canvas"):
            self._operation_overlay().set_cancellable(False)

    def _cancel_active_background_task(self) -> None:
        task = self._active_background_task
        if task is None:
            return
        try:
            task.cancel()
        except Exception:
            return
        if hasattr(self, "preview_canvas"):
            self._operation_overlay().mark_cancelling()
        self.pill_render.set_state("cancelling", "#E67E22")

    def open_project_groups(self) -> None:
        known = list(self.datasets)
        try:
            known.extend(os.path.basename(p) for p in self.project.dataset_files())
        except Exception:
            pass
        dlg = ProjectGroupsDialog(self.project_context, known, self)
        dlg.exec()
        self._refresh_active_group_label()
        self._refresh_recommendations()

    def link_selected_datasets_to_active_group(self) -> None:
        names = [item.text() for item in self.list_datasets.selectedItems()] if hasattr(self, "list_datasets") else []
        if not names:
            return QMessageBox.information(self, "Project group", "Select one or more project datasets first.")
        group = self.project_context.add_to_group(datasets=names)
        self._refresh_active_group_label(); self._refresh_recommendations()
        self.statusBar().showMessage(f"Linked {len(names)} dataset(s) to '{group.name}'.", 4000)

    def import_analysis_script(self) -> None:
        dlg = ScriptImportDialog(self)
        if not dlg.exec():
            return
        name = os.path.basename(dlg.name.text().strip())
        text = dlg.text.toPlainText()
        try:
            if dlg.source_path and os.path.exists(dlg.source_path):
                project_path = self.project.ingest_file(dlg.source_path, "scripts")
            else:
                self.project.scripts_dir.mkdir(parents=True, exist_ok=True)
                project_path = str(self.project.scripts_dir / name)
                with open(project_path, "w", encoding="utf-8") as fh:
                    fh.write(text)
            rec = self.project_context.cache_script(project_path, text)
            self.project_context.add_to_group(scripts=[project_path])
            self._refresh_active_group_label()
            calls = ", ".join(c.get("call", "") for c in rec.get("plot_calls", [])[:8]) or "no explicit plot calls"
            QMessageBox.information(self, "Script context saved",
                                    f"Saved to project scripts:\n{project_path}\n\nDetected {len(rec.get('assignments',{}))} parameter assignment(s); {calls}.")
        except Exception as exc:
            log_line(f"Script context import failed: {exc}", "CONTEXT")
            QMessageBox.critical(self, "Script context", str(exc))

    def open_ai_project_advisor(self) -> None:
        ds = self.primary_dataset()
        if ds is None:
            return QMessageBox.information(self, "AI Project Advisor", "Select a project dataset first. Use Project Groups to link it with the thesis/literature.")
        endpoint = self.settings.value("ai/endpoint", "http://localhost:1234/v1/chat/completions", str)
        model = self.settings.value("ai/model", "", str)
        dlg = AIAdvisorSettingsDialog(endpoint, model, self)
        if not dlg.exec():
            return
        endpoint, model, key = dlg.endpoint.text().strip(), dlg.model.text().strip(), dlg.key.text()
        if not endpoint or not model:
            return QMessageBox.warning(self, "AI Project Advisor", "Endpoint and model are required.")
        self.settings.setValue("ai/endpoint", endpoint); self.settings.setValue("ai/model", model)
        group = self.project_context.get_group() or self.project_context.ensure_default_group()
        summary = self.project_context.project_summary(group.name)
        schema = {"dataset": ds.name, "rows": len(ds.df), "columns": [str(c) for c in ds.df.columns],
                  "numeric": [str(c) for c in ds.numeric_columns], "parameters": [str(c) for c in ds.parameter_columns],
                  "units": getattr(ds, "units", {})}
        if QMessageBox.question(self, "Send project context to API?",
                "GraphVis will send a compact literature/script summary and dataset COLUMN NAMES to the configured API. Raw dataset rows are not sent. Continue?",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            return
        self._operation_overlay().start("AI advisor is reading project context…", 5, cancellable=True)
        self.pill_render.set_state("AI advisor", "#8E44AD")
        task = AIProjectAdvisorTask(endpoint, model, key, summary, schema)
        self._set_active_background_task(task, "AI Project Advisor running…")
        task.signals.progress.connect(lambda msg, pct: self._operation_overlay().set_message(msg, pct))
        task.signals.finished.connect(self._on_ai_project_advice)
        task.signals.failed.connect(self._on_literature_failed)
        task.signals.cancelled.connect(self._on_background_cancelled)
        submit(task)

    def _on_ai_project_advice(self, result: dict) -> None:
        self._operation_overlay().stop(); self._clear_active_background_task(); self.pill_render.set_state("idle", "#7F8C8D")
        recs = result.get("recommendations", []) if isinstance(result, dict) else []
        try:
            self.project_context.save_ai_advice(result)
        except Exception as exc:
            log_line(f"AI advice cache skipped: {exc}", "CONTEXT")
        lines = []
        interpretation = result.get("project_interpretation", "") if isinstance(result, dict) else ""
        if interpretation:
            lines.append(str(interpretation))
        for rec in recs[:8]:
            if isinstance(rec, dict):
                lines.append(f"• {rec.get('graph','Graph')}: {rec.get('reason','')}")
        QMessageBox.information(self, "AI Project Advisor", "\n\n".join(lines) or "The API returned no graph recommendations.")

    def _on_background_cancelled(self) -> None:
        if hasattr(self, "preview_canvas") and not self._auto_scan_in_progress:
            self._operation_overlay().stop()
        if not self._auto_scan_in_progress:
            self._clear_active_background_task()
        self.pill_render.set_state("cancelled", "#7F8C8D")
        self.statusBar().showMessage("Operation cancelled.", 4000)

    # ------------------------------------------------------------ MVC view protocol
    def show_status(self, message: str, timeout_ms: int = 5000) -> None:
        self.statusBar().showMessage(message, timeout_ms)

    def show_error(self, title: str, message: str) -> None:
        QMessageBox.critical(self, title, message)

    def dataset_registered_from_controller(self, dataset: Dataset) -> None:
        self._register_dataset(dataset)
        self.populate_axes(self.primary_dataset())
        self.queue_render()
        self.refresh_object_manager()

    def dataset_removed_from_controller(self, name: str) -> None:
        # The model already removed the registry entry; clean only View state.
        for i in range(self.list_datasets.count() - 1, -1, -1):
            if self.list_datasets.item(i).text() == name:
                self.list_datasets.takeItem(i)
        self.populate_axes(self.primary_dataset())
        self.queue_render()
        self.refresh_object_manager()

    def _refresh_display_aliases(self) -> None:
        aliases = {}
        for raw, rec in self.variable_context.items():
            name = rec.get("name", raw)
            unit = rec.get("unit")
            aliases[raw] = f"{name} [{unit}]" if unit else str(name)
        for raw, nickname in self.app_model.nicknames.items():
            aliases[raw] = f"{nickname} ({raw})"
        set_context_aliases(aliases)
        refresh_alias_cache()

    def open_parameter_nicknames(self) -> None:
        variables = []
        for ds in self.datasets.values():
            variables.extend(map(str, ds.df.columns))
        dlg = ParameterNicknameDialog(variables, self.app_model.nicknames, self)
        if dlg.exec():
            if self.controller is not None:
                self.controller.set_nicknames(dlg.mapping())
            else:
                self.app_model.set_nicknames(dlg.mapping())
            self._refresh_display_aliases()
            self.populate_axes(self.primary_dataset())
            self.queue_render()
            self.statusBar().showMessage("Parameter nicknames saved to the active project.", 5000)

    # ------------------------------------------------------------ workspace layout
    def focus_graph_search(self) -> None:
        """Reveal the control dock and put keyboard focus in fuzzy graph search."""
        self.splitter.setVisible(True)
        self.splitter.raise_()
        self.picker.search.setFocus(Qt.ShortcutFocusReason)
        self.picker.search.selectAll()

    def analysis_workspace_layout(self) -> None:
        # Return temporary pop-out panels to their inline homes first. Closing
        # a GraphVisPanel_* QDockWidget triggers DetachablePanel._redock().
        for dock in self.findChildren(QDockWidget):
            if dock.objectName().startswith("GraphVisPanel_"):
                dock.close()
        self.splitter.setVisible(True)
        self.quick_toolbar.setVisible(True)
        if hasattr(self, "window_toolbar"):
            self.window_toolbar.setVisible(True)
            if self.toolBarArea(self.window_toolbar) == Qt.NoToolBarArea:
                self.window_toolbar.hide(); self.window_toolbar.setWindowFlags(Qt.Widget)
            self.addToolBar(Qt.TopToolBarArea, self.window_toolbar); self.window_toolbar.show()
        if self.toolBarArea(self.quick_toolbar) == Qt.NoToolBarArea:
            self.quick_toolbar.hide(); self.quick_toolbar.setWindowFlags(Qt.Widget)
        if hasattr(self, "object_manager"):
            self.object_manager.setVisible(True)
            if self.object_manager.isFloating():
                self.object_manager.setFloating(False)
            self.addDockWidget(Qt.RightDockWidgetArea, self.object_manager)
        if self.splitter.isFloating():
            self.splitter.setFloating(False)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.splitter)
        self.addToolBar(Qt.LeftToolBarArea, self.quick_toolbar)
        self.splitter.resetSidebarWidth()
        canvas = self.current_canvas()
        canvas.top_controls.setExpanded(True)
        canvas.drawer.setExpanded(False)
        canvas.graph_splitter.setSizes([900, 24])
        if hasattr(self, "act_window_graph_controls"):
            self.act_window_graph_controls.setChecked(True)
            self.act_window_interactive.setChecked(False)
        self.statusBar().showMessage("Analysis workspace restored.", 3000)

    def focus_graph_layout(self) -> None:
        self.splitter.setVisible(False)
        self.quick_toolbar.setVisible(False)
        if hasattr(self, "object_manager"):
            self.object_manager.setVisible(False)
        canvas = self.current_canvas()
        canvas.top_controls.setExpanded(True)
        canvas.drawer.setExpanded(False)
        self.statusBar().showMessage("Focus Graph layout — tab bar and graph controls retained.", 3000)

    def presentation_workspace_layout(self) -> None:
        self.splitter.setVisible(False)
        self.quick_toolbar.setVisible(False)
        if hasattr(self, "object_manager"):
            self.object_manager.setVisible(False)
        canvas = self.current_canvas()
        canvas.top_controls.setExpanded(False)
        canvas.drawer.setExpanded(False)
        self.statusBar().showMessage("Presentation layout — graph canvas maximised; tab bar retained.", 3000)

    def reset_workspace_layout(self) -> None:
        self.analysis_workspace_layout()
        self.debug_container.setVisible(False)
        if hasattr(self, "act_diag"):
            self.act_diag.setChecked(False)
        self.settings.remove("ui/mainwindow_state")
        self.statusBar().showMessage("Dock and panel layout reset.", 3000)

    # ------------------------------------------------------------ watchdog
    def _on_freeze_logged(self, msg):
        self.pill_watchdog.set_state("freeze logged", "#E67E22")
        self.statusBar().showMessage(msg, 8000)
        if self.debug_container.isVisible():
            self.debug_drawer.reload()

    def _on_freeze_recovered(self, msg):
        self.pill_watchdog.set_state("watchdog ok", "#27AE60")
        self.statusBar().showMessage(msg, 4000)

    # ------------------------------------------------------------ UI build
    def _restore_detachable_panel_shells(self) -> None:
        try:
            saved = json.loads(self.settings.value("ui/detached_panels", "[]", str) or "[]")
        except Exception:
            saved = []
        wanted = {str(x) for x in saved}
        for panel in getattr(self, "_detachable_sections", []):
            if getattr(panel, "_title", "") in wanted and getattr(panel, "_float_dock", None) is None:
                try:
                    panel.detach()
                except Exception as exc:
                    log_line(f"Detached panel restore skipped ({getattr(panel, '_title', '?')}): {exc}", "UI")

    def _sidebar_section(self, widget: QWidget, *, expanded: bool = True) -> DetachablePanel:
        """Wrap a sidebar group in a collapsible, individually undockable panel."""
        title = widget.title() if isinstance(widget, QGroupBox) else (widget.objectName() or "Panel")
        if isinstance(widget, QGroupBox):
            widget.setTitle("")
            widget.setStyleSheet("QGroupBox{margin-top:0px;}")
        panel = DetachablePanel(title, expanded=expanded)
        panel.addWidget(widget)
        self._detachable_sections.append(panel)
        return panel

    def _install_sidebar_help_tooltips(self) -> None:
        """Give every sidebar control at least a concise hover explanation.

        Purpose-written tooltips are kept verbatim.  This fallback only fills
        controls that would otherwise be silent, so unfamiliar controls still
        tell the user whether they are a staged choice or an immediate numeric
        adjustment without turning the interface into permanent help text.
        """
        for combo in self.panel.findChildren(QComboBox):
            if not combo.toolTip().strip():
                combo.setToolTip(
                    "Choose an option for this control. Plot-setting dropdowns are staged: "
                    "press the small ▶ beside the dropdown, or Apply all pending controls, to commit it."
                )
        for spin in [*self.panel.findChildren(QSpinBox), *self.panel.findChildren(QDoubleSpinBox)]:
            if not spin.toolTip().strip():
                spin.setToolTip("Adjust this numeric setting. Values are kept with the current GraphVis workspace/project state.")
        for slider in self.panel.findChildren(QSlider):
            if not slider.toolTip().strip():
                slider.setToolTip("Drag to adjust this value. Expensive surface controls use a coarse preview while dragging and restore full fidelity on release.")
        for check in self.panel.findChildren(QCheckBox):
            if not check.toolTip().strip():
                label = check.text().strip() or "this option"
                check.setToolTip(f"Toggle {label}. Checkboxes take effect deliberately and are stored with the workspace where applicable.")
        for button in self.panel.findChildren(QPushButton):
            if not button.toolTip().strip() and button.text().strip():
                button.setToolTip(f"Run: {button.text().replace('&', '').strip()}.")

    def _build_ui(self):
        # Central area is reserved for graph tabs.  Control panels are native
        # Qt docks, so their separators drag normally and every panel can move
        # to another edge/monitor without constraining the graph canvas.
        self.setDockNestingEnabled(True)
        self.setDockOptions(QMainWindow.DockOption.AllowNestedDocks | QMainWindow.DockOption.AllowTabbedDocks)
        # Dock animation makes live splitter movement feel sticky because Qt
        # repeatedly interpolates geometry while Matplotlib is also receiving
        # resize events.  Native non-animated docking is substantially cheaper
        # and still gives immediate visual feedback from the separator.
        self.setAnimated(False)

        central = QWidget()
        central.setObjectName("centralWidget")
        central.setAttribute(Qt.WA_StaticContents, True)
        self.setCentralWidget(central)
        outer = QVBoxLayout(central)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self._detachable_sections: list[DetachablePanel] = []
        self.scroll = QScrollArea()
        self.scroll.setWidgetResizable(True)
        self.scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.scroll.viewport().setAttribute(Qt.WA_StaticContents, True)
        self.panel = QWidget()
        self.panel.setAttribute(Qt.WA_StaticContents, True)
        self.panel.setMinimumWidth(240)
        left = QVBoxLayout(self.panel)
        left.setContentsMargins(4, 4, 4, 4)
        left.setSpacing(4)

        brand = QHBoxLayout()
        brand.setSpacing(5)
        logo = QLabel()
        logo.setObjectName("GraphVisBrandLogo")
        logo.setAttribute(Qt.WA_TranslucentBackground, True)
        logo.setStyleSheet("background: transparent; border: none;")
        # Prefer the full GraphVis branding logo; fall back to the app icon.
        # Accept either the shipped PNG or a user-supplied "logo graphvis.jpg"
        # dropped into assets/branding.
        jpg_logo = str(Path(LOGO_FILE).parent / "logo graphvis.jpg")
        brand_asset = next((p for p in (jpg_logo, LOGO_FILE, ICON_FILE) if os.path.exists(p)), "")
        if brand_asset:
            pix = QPixmap(brand_asset)
            logo.setPixmap(pix.scaled(52, 52, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        logo.setFixedSize(56, 56)
        logo.setToolTip("GraphVis — DF + MEC scientific visualisation studio")
        brand.addWidget(logo, 0, Qt.AlignVCenter)
        title = QLabel("<span style='font-size:17px;font-weight:750'>GraphVis</span><br>"
                       "<span style='font-size:8.5pt'>Scientific studio</span>")
        title.setTextFormat(Qt.RichText)
        title.setWordWrap(True)
        brand.addWidget(title, 1, Qt.AlignVCenter)
        left.addLayout(brand)

        mode_bar = QHBoxLayout()
        self.btn_mode_simple = QPushButton("Simple View")
        self.btn_mode_simple.setCheckable(True)
        self.btn_mode_simple.clicked.connect(lambda: self.set_ui_mode(advanced=False))
        self.btn_mode_advanced = QPushButton("Advanced View")
        self.btn_mode_advanced.setCheckable(True)
        self.btn_mode_advanced.clicked.connect(lambda: self.set_ui_mode(advanced=True))
        for b in (self.btn_mode_simple, self.btn_mode_advanced):
            b.setMinimumHeight(28); b.setMaximumHeight(28)
            b.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
            mode_bar.addWidget(b, 1)
        left.addLayout(mode_bar)

        left.addWidget(self._sidebar_section(self._group_ingestion(), expanded=True))
        left.addWidget(self._sidebar_section(self._group_chart(), expanded=True))
        left.addWidget(self._sidebar_section(self._group_mapping(), expanded=False))

        self.advanced_container = QWidget()
        adv = QVBoxLayout(self.advanced_container)
        adv.setContentsMargins(0, 0, 0, 0)
        adv.setSpacing(4)
        adv.addWidget(self._sidebar_section(self._group_clipping(), expanded=False))
        adv.addWidget(self._sidebar_section(self._group_surface_estimation(), expanded=False))
        adv.addWidget(self._sidebar_section(self._group_limits(), expanded=False))
        adv.addWidget(self._sidebar_section(self._group_styling(), expanded=False))
        adv.addWidget(self._sidebar_section(self._group_alias_manager(), expanded=False))
        left.addWidget(self.advanced_container)
        left.addWidget(self._sidebar_section(self._group_actions(), expanded=False))
        self.btn_apply_all_controls = QPushButton("▶  Apply all pending controls")
        self.btn_apply_all_controls.setObjectName("GraphVisApplyAllButton")
        self.btn_apply_all_controls.setMinimumHeight(32)
        self.btn_apply_all_controls.setToolTip("Commit every pending dropdown selection and render exactly once.")
        self.btn_apply_all_controls.clicked.connect(self._apply_all_staged)
        self.btn_apply_all_controls.setVisible(manual_apply_mode())
        left.addWidget(self.btn_apply_all_controls)
        self._install_sidebar_help_tooltips()
        left.addStretch(1)

        self.scroll.setWidget(self.panel)
        self.splitter = ControlSidebarDock("GraphVis Controls", self)
        self.splitter.setWidget(self.scroll)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.splitter)
        sidebar_width = self.settings.value("ui/sidebar_width", 360, int)
        sidebar_collapsed = self.settings.value("ui/sidebar_collapsed", False, bool)
        if not self.settings.value("ui/compact_studio_width_migrated", False, bool):
            if int(sidebar_width or 360) > 440:
                sidebar_width = 360
            self.settings.setValue("ui/compact_studio_width_migrated", True)
        self.splitter.restoreSidebar(sidebar_width, sidebar_collapsed)

        self.tabs = QTabWidget()
        self.tabs.setTabsClosable(True)
        self.tabs.setMovable(True)
        self.tabs.setDocumentMode(True)
        self.tabs.tabBar().setUsesScrollButtons(True)
        self.tabs.tabBar().setElideMode(Qt.ElideRight)
        self.tabs.tabCloseRequested.connect(self.close_tab)
        self.tabs.currentChanged.connect(self._on_tab_changed)
        QTimer.singleShot(0, self._style_tab_navigation_buttons)

        self.right_split = CompactSplitter(Qt.Vertical)
        self.right_split.setChildrenCollapsible(True)
        self.right_split.addWidget(self.tabs)

        self.debug_container = DetachablePanel("Diagnostics", expanded=True)
        self.debug_drawer = DebugDrawer()
        self.debug_container.addWidget(self.debug_drawer)
        self.debug_container.setVisible(False)
        self.right_split.addWidget(self.debug_container)
        self.right_split.setStretchFactor(0, 5)
        self.right_split.setStretchFactor(1, 1)
        self.right_split.setSizes([800, 200])

        # True empty-canvas startup.  The graph/editor stack is not shown until
        # the user explicitly generates/restores a graph.  This removes a large
        # amount of visual chrome at startup and avoids any accidental render.
        self.stage_stack = QStackedWidget()
        self.blank_stage = QFrame()
        self.blank_stage.setObjectName("GraphVisBlankStage")
        self.stage_stack.addWidget(self.blank_stage)
        self.stage_stack.addWidget(self.right_split)
        self.stage_stack.setCurrentWidget(self.blank_stage)
        outer.addWidget(self.stage_stack, 1)

        # Long-running non-graph operations still need progress/cancel feedback
        # while the canvas is deliberately absent.
        self.global_overlay = LoadingOverlay(central)
        self.global_overlay.cancel_requested.connect(self._cancel_active_background_task)

        self.pill_watchdog = StatusPill("watchdog ok", "#27AE60")
        self.pill_render = StatusPill("idle", "#7F8C8D")
        self.statusBar().addPermanentWidget(self.pill_render)
        self.statusBar().addPermanentWidget(self.pill_watchdog)

        self._init_preview_tab()
        self._refresh_recommendations()
        self.set_ui_mode(advanced=self.settings.value("ui/advanced", False, bool))

    def _style_tab_navigation_buttons(self) -> None:
        """Give Qt's hidden tab scrollers explicit, high-contrast arrows."""
        try:
            bar = self.tabs.tabBar()
            for button in bar.findChildren(QToolButton):
                arrow = button.arrowType()
                if arrow == Qt.LeftArrow:
                    button.setIcon(compact_tool_icon("left", 18))
                    button.setToolTip("Previous graph tab")
                elif arrow == Qt.RightArrow:
                    button.setIcon(compact_tool_icon("right", 18))
                    button.setToolTip("Next graph tab")
                else:
                    continue
                button.setArrowType(Qt.NoArrow)
                button.setFixedSize(24, 24)
                button.setObjectName("GraphVisTabScrollButton")
        except Exception:
            pass

    def _build_menus(self):
        mb = self.menuBar()
        m_file = mb.addMenu("&File")
        project_menu = m_file.addMenu("Project workspace")
        project_menu.addAction("New project…", self.new_project)
        project_menu.addAction("Open project folder…", self.open_project_folder)
        project_menu.addAction("Save comparison snapshot…", self.save_project_snapshot)
        project_menu.addAction("Restore snapshot…", self.restore_project_snapshot)
        m_file.addSeparator()
        m_file.addAction("Open editable GraphVis figure…", self.open_native_figure)
        m_file.addAction("Save active figure as .gvfig…", self.save_current_native_figure)
        m_file.addSeparator()
        m_file.addAction("Add dataset to project…", self.load_dataset_file)
        m_file.addAction("Refresh project datasets	F5", self.refresh_dataset_folder)
        m_file.addAction("Add literature / thesis…", self.read_literature_only)
        m_file.addAction("Add literature bundle (advanced)…", self.upload_literature_file)
        m_file.addSeparator()
        m_file.addAction("Save workspace snapshot (JSON)…", self.save_workspace_snapshot_file)
        m_file.addAction("Restore workspace snapshot…", self.restore_workspace_snapshot_file)
        m_file.addAction("Restore previous graph tabs…", self.restore_previous_graph_tabs)
        m_file.addAction("Optical figure digitizer…", self.open_digitizer)
        m_file.addSeparator()
        m_file.addAction("Export current tab…\tCtrl+E", self.export_current)
        m_file.addAction("Export all active graph tabs…", self.batch_export_active_tabs)
        m_file.addAction("Export dataset CSV…", lambda: self.current_canvas().export_csv())
        code_menu = m_file.addMenu("Export reproducible script")
        code_menu.addAction("Python / Matplotlib…", lambda: self.export_plot_code("python"))
        code_menu.addAction("MATLAB…", lambda: self.export_plot_code("matlab"))
        m_file.addSeparator()
        m_file.addAction("Exit", self.close)

        m_data = mb.addMenu("&Data")
        m_data.addAction("Database / Web / Cloud Connector…", self.open_data_connector)
        m_data.addAction("Load Excel workbook (all sheets)…", self.load_excel_workbook_all_sheets)
        m_data.addAction("Workbook / Spreadsheet Operations…", self.open_spreadsheet_tools)
        m_data.addAction("Refresh active connectors", self.refresh_live_connectors)
        m_data.addSeparator()
        m_data.addAction("Optical Figure Digitizer…", self.open_digitizer)
        m_data.addAction("Remove selected dataset from auto-scan…", self.remove_selected_dataset)
        m_data.addAction("Restore removed dataset…", self.restore_detached_dataset)

        m_analysis = mb.addMenu("&Analysis")
        m_analysis.addAction("Statistics / ML / Signal / Fitting Hub…", self.open_analysis_hub)
        m_analysis.addAction("Peak analysis / nonlinear fitting…", self.open_peak_analysis)
        m_analysis.addAction("Predictive Modeling Advisor…", self.open_predictive_advisor)
        m_analysis.addSeparator()
        m_analysis.addAction("Batch Processing / Reports…", self.open_batch_processor)

        m_automation = mb.addMenu("A&utomation")
        m_automation.addAction("Embedded Python Console…", self.open_python_console)
        m_automation.addAction("Optional R Console…", self.open_r_console)
        m_automation.addAction("Analysis Workflow Builder…", self.open_workflow_builder)
        m_automation.addAction("Clone active project…", self.clone_active_project)
        m_automation.addAction("Open workflow folder", self.open_workflow_folder)

        m_edit = mb.addMenu("&Edit")
        undo, redo = self.history.create_actions(self)
        undo.setShortcuts(QKeySequence.StandardKey.Undo)
        redo.setShortcuts([QKeySequence.StandardKey.Redo, QKeySequence("Ctrl+Y")])
        m_edit.addAction(undo); m_edit.addAction(redo)
        self.addAction(undo); self.addAction(redo)

        m_view = mb.addMenu("&View")
        m_view.addAction("Simple view", lambda: self.set_ui_mode(False))
        m_view.addAction("Advanced view", lambda: self.set_ui_mode(True))
        layout_menu = m_view.addMenu("Workspace layout")
        layout_menu.addAction("Analysis workspace", self.analysis_workspace_layout)
        layout_menu.addAction("Focus graph", self.focus_graph_layout)
        layout_menu.addAction("Presentation / clean canvas", self.presentation_workspace_layout)
        layout_menu.addAction("Reset docks & panels", self.reset_workspace_layout)
        layout_menu.addSeparator()
        layout_menu.addAction("Clear graph workspace", self.clear_workspace_view)
        m_view.addAction("Focus graph search	Ctrl+K", self.focus_graph_search)
        m_view.addSeparator()
        panels_menu = m_view.addMenu("Panels")
        act_controls = self.splitter.toggleViewAction(); act_controls.setText("Control sidebar   [Ctrl+B]")
        panels_menu.addAction(act_controls)
        act_tools = self.quick_toolbar.toggleViewAction(); act_tools.setText("Scientific tools   [Ctrl+Shift+B]")
        panels_menu.addAction(act_tools)
        act_windowbar = self.window_toolbar.toggleViewAction(); act_windowbar.setText("Window & Layout toolbar")
        panels_menu.addAction(act_windowbar)
        self.act_object_manager = self.object_manager.toggleViewAction(); self.act_object_manager.setText("Object Manager")
        panels_menu.addAction(self.act_object_manager)
        panels_menu.addSeparator()
        panels_menu.addAction("Graph top controls", lambda: self.current_canvas().top_controls.setExpanded(not self.current_canvas().top_controls.isExpanded()))
        panels_menu.addAction("Graph interactive controls", lambda: self.current_canvas().set_drawer_visible(not self.current_canvas().drawer.isExpanded()))
        panels_menu.addAction("Diagnostics drawer", lambda: self.set_diagnostics_visible(not self.debug_container.isVisible()))
        panels_menu.addAction("Reset control sidebar width", self.splitter.resetSidebarWidth)
        m_view.addAction("Float active graph window…", self.pop_out_current_graph)
        self.act_link_views = QAction("Link zoom/pan across graph tabs", self, checkable=True)
        self.act_link_views.setChecked(False)
        m_view.addAction(self.act_link_views)
        graph_bg = m_view.addMenu("Graph background")
        self._graph_background_actions = {}
        self._graph_background_group = QActionGroup(self)
        self._graph_background_group.setExclusive(True)
        current_bg_mode = str(self.settings.value("graph/background_mode", "Light", str) or "Light")
        for mode in ("Light", "White", "Dark", "Theme"):
            act = QAction(mode, self, checkable=True)
            act.setChecked(current_bg_mode == mode)
            act.triggered.connect(lambda _=False, m=mode: self._set_graph_background_mode(m))
            self._graph_background_group.addAction(act); graph_bg.addAction(act)
            self._graph_background_actions[mode] = act
        custom = QAction("Custom colour…", self, checkable=True)
        custom.setChecked(current_bg_mode == "Custom")
        custom.triggered.connect(self._choose_graph_background_custom)
        self._graph_background_group.addAction(custom); graph_bg.addAction(custom)
        self._graph_background_actions["Custom"] = custom
        graph_bg.addSeparator()
        graph_bg.addAction("Brightness…", self._set_graph_background_brightness)
        self.act_graph_bg_merge = QAction("Blend with UI theme", self, checkable=True)
        self.act_graph_bg_merge.setChecked(self.settings.value("graph/background_merge_theme", False, bool))
        self.act_graph_bg_merge.toggled.connect(self._toggle_graph_background_merge)
        graph_bg.addAction(self.act_graph_bg_merge)
        graph_bg.addAction("Theme blend amount…", self._set_graph_background_merge_amount)
        graph_bg.addSeparator()
        self.act_compact_graph_previews = QAction("Compact graph-library previews", self, checkable=True)
        self.act_compact_graph_previews.setChecked(self.settings.value("graph_library/compact_previews", True, bool))
        self.act_compact_graph_previews.toggled.connect(self._set_graph_preview_compact)
        graph_bg.addAction(self.act_compact_graph_previews)
        self.act_merge_graph_previews = QAction("Merge preview background with UI", self, checkable=True)
        self.act_merge_graph_previews.setChecked(self.settings.value("graph_library/merge_previews", True, bool))
        self.act_merge_graph_previews.toggled.connect(self._set_graph_preview_merge)
        graph_bg.addAction(self.act_merge_graph_previews)
        theme_menu = m_view.addMenu("UI theme")
        for group_name, theme_names in THEME_GROUPS.items():
            group_menu = theme_menu.addMenu(group_name)
            for theme_name in theme_names:
                group_menu.addAction(theme_name, lambda _=False, n=theme_name: self.apply_theme(n))
        m_view.addSeparator()
        self.act_diag = QAction("Diagnostics drawer", self, checkable=True)
        self.act_diag.setShortcut("Ctrl+D")
        self.act_diag.toggled.connect(self.set_diagnostics_visible)
        m_view.addAction(self.act_diag)

        m_tools = mb.addMenu("&Tools")
        m_tools.addAction("Intelligent visualisation advisor…", self.run_intelligent_advisor)
        m_tools.addAction("Smart Map Suite from active project group", self.auto_generate_smart_suite)
        m_tools.addAction("Project groups…", self.open_project_groups)
        m_tools.addAction("Import / paste MATLAB or analysis script…", self.import_analysis_script)
        m_tools.addAction("External simulation bridge (Python / ANSYS / COMSOL / AQUASIM)…", self.open_simulation_bridge)
        m_tools.addAction("AI Project Advisor…", self.open_ai_project_advisor)
        m_tools.addAction("Variable alias manager…", self.open_alias_editor)
        m_tools.addAction("Parameter nicknames…", self.open_parameter_nicknames)
        m_tools.addAction("Paste code / variable context…", self.open_code_context)
        m_tools.addAction("Peak analysis / nonlinear fitting…", self.open_peak_analysis)
        m_tools.addAction("GPU accelerated preview…", self.open_gpu_preview_for_current)
        m_tools.addSeparator()
        m_tools.addAction("Publication Ready", self.apply_publication_ready)
        m_tools.addAction("Manage publication profiles…", self.manage_publication_profiles)
        m_tools.addSeparator()
        template_menu = m_tools.addMenu("Graph templates")
        template_menu.addAction("Save current styling as template…", self.save_graph_template)
        template_menu.addAction("Apply project template…", self.apply_graph_template)
        m_tools.addSeparator()
        m_tools.addAction("Clear render caches", self.clear_caches)
        m_tools.addAction("Clear Smart Suite saved scans…", self.clear_smart_scan_cache)

        m_settings = mb.addMenu("&Settings")
        m_settings.addAction("Settings & diagnostics…", self.open_settings)
        m_settings.addAction("Open graphvis.log", self.debug_drawer.open_log_file)
        m_settings.addAction("Open Debug logs folder", self.debug_drawer.open_debug_folder)
        m_settings.addAction("Open Errors / crash reports folder", self.debug_drawer.open_errors_folder)

        m_help = mb.addMenu("&Help")
        m_help.addAction("Instructions & graph guide…", self.open_instructions)
        m_help.addAction("Keyboard & touch shortcuts", self.show_help)

        # A persistent searchable help field sits immediately to the right of
        # Help. It searches both feature instructions and every graph catalogue
        # entry, so complex plots do not require guessing their mapping rules.
        self.help_search = QLineEdit(mb)
        self.help_search.setObjectName("GraphVisHelpSearch")
        self.help_search.setPlaceholderText("Search help / graphs…")
        self.help_search.setClearButtonEnabled(True)
        self.help_search.setMinimumWidth(175); self.help_search.setMaximumWidth(250)
        self.help_search.setToolTip("Search GraphVis instructions and all graph types; press Enter to open the guide")
        self._help_completer = QCompleter(InstructionsDialog.search_terms(), self)
        self._help_completer.setCaseSensitivity(Qt.CaseInsensitive)
        self._help_completer.setFilterMode(Qt.MatchContains)
        self.help_search.setCompleter(self._help_completer)
        self.help_search.returnPressed.connect(self._open_help_search)
        try:
            self._help_completer.activated[str].connect(lambda text: self.open_instructions(str(text)))
        except Exception:
            self._help_completer.activated.connect(lambda text: self.open_instructions(str(text)))
        self._help_search_action = QWidgetAction(mb)
        self._help_search_action.setDefaultWidget(self.help_search)
        mb.addAction(self._help_search_action)

        # Qt menu bars themselves remain standard, but every drop-down is
        # tear-off enabled so frequently used menus can be floated as small
        # independent windows when desired.
        for menu in mb.findChildren(QMenu):
            try:
                menu.setTearOffEnabled(True)
            except Exception:
                pass

    def _toggle_toolbar_popout(self, toolbar: QToolBar, default_area) -> None:
        """Programmatically float/re-dock a QToolBar from the pop-out icon."""
        if self.toolBarArea(toolbar) == Qt.NoToolBarArea:
            toolbar.hide(); toolbar.setWindowFlags(Qt.Widget)
            self.addToolBar(default_area, toolbar); toolbar.show()
        else:
            self.removeToolBar(toolbar)
            toolbar.setParent(self); toolbar.setWindowFlags(Qt.Tool)
            toolbar.show(); toolbar.raise_(); toolbar.activateWindow()

    def _build_window_toolbar(self) -> None:
        """Dockable top toolbar dedicated to window/panel management."""
        tb = QToolBar("Window & Layout", self)
        tb.setObjectName("GraphVisWindowLayoutToolbar")
        tb.setMovable(True); tb.setFloatable(True)
        tb.setAllowedAreas(Qt.TopToolBarArea | Qt.BottomToolBarArea | Qt.LeftToolBarArea | Qt.RightToolBarArea)
        tb.setToolButtonStyle(Qt.ToolButtonIconOnly)
        tb.setIconSize(QSize(18, 18))
        self.addToolBar(Qt.TopToolBarArea, tb)

        controls = self.splitter.toggleViewAction(); controls.setText("Controls"); controls.setIcon(compact_tool_icon("controls")); controls.setToolTip("Show/hide controls")
        tools = self.quick_toolbar.toggleViewAction(); tools.setText("Tools"); tools.setIcon(compact_tool_icon("style")); tools.setToolTip("Show/hide scientific tools")
        objects = self.object_manager.toggleViewAction(); objects.setText("Objects"); objects.setIcon(compact_tool_icon("objects")); objects.setToolTip("Show/hide Object Manager")
        tb.addAction(controls); tb.addAction(tools); tb.addAction(objects); tb.addSeparator()
        act_top = QAction(compact_tool_icon("sliders"), "Graph Controls", self, checkable=True); act_top.setChecked(True); act_top.setToolTip("Show/hide graph controls")
        act_top.toggled.connect(lambda on: self.current_canvas().top_controls.setExpanded(on))
        tb.addAction(act_top)
        act_bottom = QAction(compact_tool_icon("smooth"), "Interactive", self, checkable=True); act_bottom.setChecked(False); act_bottom.setToolTip("Show/hide interactive controls")
        act_bottom.toggled.connect(lambda on: self.current_canvas().drawer.setExpanded(on))
        tb.addAction(act_bottom)
        act_diag = QAction(compact_tool_icon("document"), "Diagnostics", self, checkable=True); act_diag.setChecked(False); act_diag.setToolTip("Show/hide diagnostics")
        act_diag.toggled.connect(self.set_diagnostics_visible); tb.addAction(act_diag)
        tb.addSeparator()
        reset = QAction(compact_tool_icon("reset"), "Reset Layout", self); reset.setToolTip("Reset dock layout"); reset.triggered.connect(self.reset_workspace_layout); tb.addAction(reset)
        float_tb = QAction(stacked_rectangles_icon(), "Pop out toolbar", self)
        float_tb.setToolTip("Float/dock the Window & Layout toolbar")
        float_tb.triggered.connect(lambda: self._toggle_toolbar_popout(tb, Qt.TopToolBarArea))
        tb.addAction(float_tb)
        self.window_toolbar = tb
        self.act_window_graph_controls = act_top
        self.act_window_interactive = act_bottom
        self.act_window_diagnostics = act_diag

    def _build_quick_toolbar(self) -> None:
        """Persistent left quick-access toolbar; actions delegate to controllers/tools."""
        tb = QToolBar("Scientific tools", self)
        tb.setObjectName("GraphVisScientificTools")
        tb.setMovable(True); tb.setFloatable(True)
        tb.setAllowedAreas(Qt.LeftToolBarArea | Qt.RightToolBarArea | Qt.TopToolBarArea | Qt.BottomToolBarArea)
        tb.setToolButtonStyle(Qt.ToolButtonIconOnly)
        tb.setIconSize(QSize(18, 18))
        self.addToolBar(Qt.LeftToolBarArea, tb)
        actions = [
            ("layers", "Layers", self.open_layer_manager, "Manage dataset/layer visibility"),
            ("roi", "ROI", lambda: self.current_canvas().toggle_roi_selector(), "Drag a region of interest"),
            ("fit", "Fit", self.open_peak_analysis, "Peak, curve and nonlinear fitting"),
            ("smooth", "Smooth", lambda: self.current_canvas().focus_smoothing_controls(), "Smoothing and filtering"),
            ("style", "Style", lambda: self.set_ui_mode(True), "Advanced styling controls"),
            ("annotate", "Annotate", lambda: self.current_canvas().open_annotation_manager(), "Editable annotations"),
            ("publish", "Publish", self.apply_publication_ready, "Publication-ready journal profile"),
            ("gpu", "GPU 3D", self.open_gpu_preview_for_current, "GPU accelerated 3D preview"),
            ("digitize", "Digitize", self.open_digitizer, "Extract data from figures"),
        ]
        for icon_name, text, slot, tip in actions:
            act = QAction(compact_tool_icon(icon_name), text, self); act.setToolTip(f"{text} — {tip}"); act.triggered.connect(slot); tb.addAction(act)
        # Global parameter-history controls beside the float/dock buttons.
        tb.addSeparator()
        # Shortcuts already live on the Edit-menu actions; these buttons share
        # the same QUndoStack so enabled state and history stay in sync.
        undo_act, redo_act = self.history.create_actions(self)
        undo_act.setIcon(compact_tool_icon("reset"))
        undo_act.setToolTip("Undo — revert the last parameter/state change (Ctrl+Z)")
        redo_act.setIcon(compact_tool_icon("play"))
        redo_act.setToolTip("Redo — re-apply the reverted change (Ctrl+Shift+Z)")
        tb.addAction(undo_act); tb.addAction(redo_act)
        self.act_history_undo, self.act_history_redo = undo_act, redo_act
        pop = QAction(stacked_rectangles_icon(), "Pop out", self)
        pop.setToolTip("Float/dock the Scientific tools toolbar")
        pop.triggered.connect(lambda: self._toggle_toolbar_popout(tb, Qt.LeftToolBarArea))
        tb.addAction(pop)
        self.quick_toolbar = tb

    def _smart_budget_seconds(self) -> float:
        if not hasattr(self, "spin_smart_budget"):
            return 30.0
        value = float(self.spin_smart_budget.value())
        unit = self.cb_smart_budget_unit.currentText() if hasattr(self, "cb_smart_budget_unit") else "Seconds"
        if unit == "Minutes":
            value *= 60.0
        elif unit == "Hours":
            value *= 3600.0
        return max(5.0, min(value, 259200.0))

    def _sync_smart_budget_editor(self, seconds: float) -> None:
        unit = self.cb_smart_budget_unit.currentText() if hasattr(self, "cb_smart_budget_unit") else "Seconds"
        self.spin_smart_budget.blockSignals(True)
        if unit == "Hours":
            self.spin_smart_budget.setRange(1, 72)
            self.spin_smart_budget.setSuffix(" h")
            self.spin_smart_budget.setValue(max(1, min(72, int(round(float(seconds) / 3600.0)))))
        elif unit == "Minutes":
            self.spin_smart_budget.setRange(1, 4320)
            self.spin_smart_budget.setSuffix(" min")
            self.spin_smart_budget.setValue(max(1, min(4320, int(round(float(seconds) / 60.0)))))
        else:
            self.spin_smart_budget.setRange(5, 259200)
            self.spin_smart_budget.setSuffix(" s")
            self.spin_smart_budget.setValue(max(5, min(259200, int(round(float(seconds))))))
        self.spin_smart_budget.blockSignals(False)

    def _store_smart_budget(self, *_args) -> None:
        self.settings.setValue("advisor/time_budget_seconds", int(round(self._smart_budget_seconds())))
        if hasattr(self, "cb_smart_budget_unit"):
            self.settings.setValue("advisor/time_budget_unit", self.cb_smart_budget_unit.currentText())

    def _smart_budget_unit_changed(self, unit: str) -> None:
        seconds = self.settings.value("advisor/time_budget_seconds", 30, int)
        self.settings.setValue("advisor/time_budget_unit", unit)
        self._sync_smart_budget_editor(seconds)
        self._store_smart_budget()

    def _literature_budget_seconds(self) -> float:
        if not hasattr(self, "spin_literature_budget"):
            return 1800.0
        value = float(self.spin_literature_budget.value())
        unit = self.cb_literature_budget_unit.currentText()
        if unit == "Minutes":
            value *= 60.0
        elif unit == "Hours":
            value *= 3600.0
        return max(10.0, min(value, 259200.0))

    def _sync_literature_budget_editor(self, seconds: float) -> None:
        unit = self.cb_literature_budget_unit.currentText() if hasattr(self, "cb_literature_budget_unit") else "Minutes"
        self.spin_literature_budget.blockSignals(True)
        if unit == "Hours":
            self.spin_literature_budget.setRange(1, 72); self.spin_literature_budget.setSuffix(" h")
            self.spin_literature_budget.setValue(max(1, min(72, int(round(float(seconds) / 3600.0)))))
        elif unit == "Minutes":
            self.spin_literature_budget.setRange(1, 4320); self.spin_literature_budget.setSuffix(" min")
            self.spin_literature_budget.setValue(max(1, min(4320, int(round(float(seconds) / 60.0)))))
        else:
            self.spin_literature_budget.setRange(10, 259200); self.spin_literature_budget.setSuffix(" s")
            self.spin_literature_budget.setValue(max(10, min(259200, int(round(float(seconds))))))
        self.spin_literature_budget.blockSignals(False)

    def _store_literature_budget(self, *_args) -> None:
        self.settings.setValue("literature/time_budget_seconds", int(round(self._literature_budget_seconds())))
        if hasattr(self, "cb_literature_budget_unit"):
            self.settings.setValue("literature/time_budget_unit", self.cb_literature_budget_unit.currentText())

    def _literature_budget_unit_changed(self, unit: str) -> None:
        seconds = self.settings.value("literature/time_budget_seconds", 1800, int)
        self.settings.setValue("literature/time_budget_unit", unit)
        self._sync_literature_budget_editor(seconds)
        self._store_literature_budget()

    def _build_shortcuts(self):
        for seq, slot in (("Ctrl+R", self.generate_preview), ("Ctrl+S", self.save_graph_to_tab),
                          ("Ctrl+B", self.splitter.toggleSidebar), ("Ctrl+Shift+B", lambda: self.quick_toolbar.setVisible(not self.quick_toolbar.isVisible())),
                          ("Ctrl+K", self.focus_graph_search), ("Ctrl+E", self.export_current), ("F5", self.refresh_dataset_folder)):
            QShortcut(QKeySequence(seq), self).activated.connect(slot)

    # ------------------------------------------------------------ tactical apply controls
    def _register_staged_combo(self, combo: QComboBox, help_text: str = "") -> QToolButton:
        """Track a combo's last committed value under the active apply mode.

        Adaptive (default): light charts commit reactively after a short
        debounce; heavy visualizations queue until ▶ Apply.  Manual queues
        everything; Instant commits everything.  The applied-value bookkeeping
        also lets a cancelled/failed render roll the combo back to its last
        confirmed state.
        """
        if combo not in self._staged_combos:
            self._staged_combos.append(combo)
        combo._graphvis_applied_text = combo.currentText()
        combo._graphvis_applied_data = combo.currentData()
        if help_text:
            combo.setToolTip(help_text)
        button = QToolButton()
        button.setText("▶")
        button.setObjectName("GraphVisApplyChoiceButton")
        button.setFixedSize(25, 25)
        button.setEnabled(False)
        button.setVisible(manual_apply_mode())
        button.setToolTip("Apply this dropdown's queued selection and update the graph."
                          if manual_apply_mode() else "Changes apply automatically. This button remains for compatibility.")
        combo._graphvis_apply_button = button
        combo.currentIndexChanged.connect(lambda _=None, c=combo: self._on_staged_combo_changed(c))
        button.clicked.connect(lambda _=False, c=combo: self._apply_staged_combo(c))
        return button

    def _on_staged_combo_changed(self, combo: QComboBox) -> None:
        if self._applying_state:
            return
        self._mark_staged_combo_pending(combo)
        if not manual_apply_for(self.cb_chart.currentText()):
            # Reactive path (standard tools / light charts): coalesce rapid
            # navigation into one commit + render.  Heavy visualizations and
            # full-manual mode queue until ▶ Apply.
            self._staged_commit_timer.start()

    def _commit_pending_staged(self) -> None:
        if self._applying_state:
            return
        pending = [c for c in self._staged_combos
                   if c.currentText() != getattr(c, "_graphvis_applied_text", c.currentText())
                   or c.currentData() != getattr(c, "_graphvis_applied_data", c.currentData())]
        if not pending and not self._pending_graph_entry:
            return
        self._apply_all_staged()

    def _revert_staged_combos(self) -> None:
        """Roll every staged dropdown back to its last committed value."""
        for combo in list(self._staged_combos):
            applied_text = getattr(combo, "_graphvis_applied_text", None)
            applied_data = getattr(combo, "_graphvis_applied_data", None)
            if applied_text is None:
                continue
            if combo.currentText() == applied_text and combo.currentData() == applied_data:
                continue
            combo.blockSignals(True)
            try:
                idx = combo.findData(applied_data) if applied_data is not None else -1
                if idx < 0:
                    idx = combo.findText(applied_text)
                if idx >= 0:
                    combo.setCurrentIndex(idx)
                else:
                    combo.setCurrentText(applied_text)
            finally:
                combo.blockSignals(False)
            self._mark_staged_combo_pending(combo)

    def _staged_combo_widget(self, combo: QComboBox, help_text: str = "") -> QWidget:
        row = QWidget(); lay = QHBoxLayout(row); lay.setContentsMargins(0, 0, 0, 0); lay.setSpacing(4)
        lay.addWidget(combo, 1); lay.addWidget(self._register_staged_combo(combo, help_text), 0)
        return row

    def _mark_staged_combo_pending(self, combo: QComboBox) -> None:
        if self._applying_state:
            return
        button = getattr(combo, "_graphvis_apply_button", None)
        if button is not None:
            changed = (combo.currentText() != getattr(combo, "_graphvis_applied_text", combo.currentText()) or
                       combo.currentData() != getattr(combo, "_graphvis_applied_data", combo.currentData()))
            button.setEnabled(bool(changed))
            button.setProperty("pending", bool(changed)); button.style().unpolish(button); button.style().polish(button)

    def _sync_staged_combo(self, combo: QComboBox) -> None:
        combo._graphvis_applied_text = combo.currentText()
        combo._graphvis_applied_data = combo.currentData()
        button = getattr(combo, "_graphvis_apply_button", None)
        if button is not None:
            button.setEnabled(False); button.setProperty("pending", False); button.style().unpolish(button); button.style().polish(button)

    def _sync_all_staged_combos(self) -> None:
        for combo in list(self._staged_combos):
            self._sync_staged_combo(combo)

    @staticmethod
    def _applied_combo_text(combo: QComboBox) -> str:
        return str(getattr(combo, "_graphvis_applied_text", combo.currentText()))

    @staticmethod
    def _applied_combo_data(combo: QComboBox):
        return getattr(combo, "_graphvis_applied_data", combo.currentData())

    def _apply_staged_combo(self, combo: QComboBox, *, render: bool = True) -> None:
        self._sync_staged_combo(combo)
        self._apply_axis_roles()
        if render:
            self.queue_render()

    def _apply_all_staged(self) -> None:
        # A pending graph can choose defaults/mappings; commit it first, then
        # commit every currently visible dropdown and issue one render only.
        if self._pending_graph_entry:
            self._apply_pending_graph_selection(render=False)
        self._sync_all_staged_combos()
        self._apply_axis_roles()
        self.queue_render()
        self.statusBar().showMessage("Applied all pending graph controls.", 3000)

    # ------------------------------------------------------------ groups
    def _group_ingestion(self):
        g = QGroupBox("Project data & context")
        v = QVBoxLayout(g)
        intro = QLabel("Read the thesis/papers that explain the project, add datasets once, then link them in a named Project Group. GraphVis rescans the project folders automatically on later starts.")
        intro.setWordWrap(True); intro.setObjectName("ProjectContextHelp")
        v.addWidget(intro)

        self.lbl_active_group = QLabel()
        self.lbl_active_group.setObjectName("ActiveProjectGroupLabel")
        self._refresh_active_group_label()
        v.addWidget(self.lbl_active_group)

        first = QHBoxLayout()
        read_lit = QPushButton("Add Literature…")
        read_lit.setMinimumHeight(34); read_lit.setToolTip("Add a paper or thesis to project context. GraphVis caches extracted text for Smart Suite; use the File menu's advanced bundle command only for a paper plus supplements.")
        read_lit.clicked.connect(self.read_literature_only)
        add_data = QPushButton("Add Dataset…")
        add_data.setMinimumHeight(34); add_data.setToolTip("Copy a dataset into the active project's datasets folder. It will be scanned automatically on future starts.")
        add_data.clicked.connect(lambda: self.load_dataset_file())
        first.addWidget(read_lit, 1); first.addWidget(add_data, 1); v.addLayout(first)

        group_row = QHBoxLayout()
        groups = QPushButton("Project Groups…")
        groups.setToolTip("Name groups and choose which already-read literature, datasets and scripts belong together.")
        groups.clicked.connect(self.open_project_groups)
        script = QPushButton("Add Analysis Script…")
        script.setToolTip("Optional context: add MATLAB/Python/R/Julia code or a parameter text file. GraphVis reads variable assignments and plotting hints; it never executes the script.")
        script.clicked.connect(self.import_analysis_script)
        group_row.addWidget(groups, 1); group_row.addWidget(script, 1); v.addLayout(group_row)

        self.list_datasets = QListWidget()
        self.list_datasets.setMaximumHeight(116)
        self.list_datasets.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.list_datasets.itemSelectionChanged.connect(self.on_selection_changed)
        v.addWidget(QLabel("Project datasets (auto-scanned):")); v.addWidget(self.list_datasets)
        link_group = QPushButton("Link Selected Dataset(s) to Active Group")
        link_group.setToolTip("Associate the selected project datasets with the thesis/literature already remembered in the active Project Group.")
        link_group.clicked.connect(self.link_selected_datasets_to_active_group)
        v.addWidget(link_group)
        smart_row = QHBoxLayout()
        smart = QPushButton("Smart Suite")
        smart.setObjectName("PrimaryProjectAction")
        smart.setMinimumHeight(32)
        smart.setToolTip("Deeply analyse the active group's data and remembered literature, then create a small set of graph-specific recommendations with independent axes, scales and colours.")
        smart.clicked.connect(self.auto_generate_smart_suite)
        smart_row.addWidget(smart, 1)
        self.spin_smart_budget = QSpinBox()
        self.spin_smart_budget.setMaximumWidth(92)
        self.spin_smart_budget.setToolTip("Maximum Smart Suite scanning/thinking budget. Larger values inspect more variables, pairs and literature context.")
        self.cb_smart_budget_unit = QComboBox()
        self.cb_smart_budget_unit.addItems(["Seconds", "Minutes", "Hours"])
        self.cb_smart_budget_unit.setMaximumWidth(86)
        saved_budget = max(5, self.settings.value("advisor/time_budget_seconds", 30, int))
        saved_unit = self.settings.value("advisor/time_budget_unit", "Seconds", str)
        self.cb_smart_budget_unit.setCurrentText(saved_unit if saved_unit in ("Seconds", "Minutes", "Hours") else "Seconds")
        self._sync_smart_budget_editor(saved_budget)
        self.spin_smart_budget.valueChanged.connect(self._store_smart_budget)
        self.cb_smart_budget_unit.currentTextChanged.connect(self._smart_budget_unit_changed)
        smart_row.addWidget(self.spin_smart_budget)
        smart_row.addWidget(self.cb_smart_budget_unit)
        v.addLayout(smart_row)

        file_row = QHBoxLayout()
        self.btn_refresh_datasets = QPushButton("↻ Rescan")
        self.btn_refresh_datasets.setToolTip("Rescan the active project's datasets folder immediately.")
        self.btn_refresh_datasets.clicked.connect(self.refresh_dataset_folder)
        self.btn_delete_dataset = QPushButton("Remove")
        self.btn_delete_dataset.setToolTip("Remove selected dataset(s) from this project's auto-scanned list. By default the project copy is moved to detached_datasets and can be recovered.")
        self.btn_delete_dataset.clicked.connect(self.remove_selected_dataset)
        restore = QPushButton("Restore…"); restore.setToolTip("Move a previously removed project copy from detached_datasets back into the auto-scanned datasets folder.")
        restore.clicked.connect(self.restore_detached_dataset)
        nick = QPushButton("Nicknames…"); nick.clicked.connect(self.open_parameter_nicknames)
        file_row.addWidget(self.btn_refresh_datasets); file_row.addWidget(restore); file_row.addWidget(nick); file_row.addWidget(self.btn_delete_dataset)
        v.addLayout(file_row)

        self.chk_live_telemetry = QCheckBox("Live Telemetry — monitor selected dataset files for changes")
        self.chk_live_telemetry.setChecked(bool(self.live_telemetry_enabled)); self.chk_live_telemetry.toggled.connect(self.set_live_telemetry)
        v.addWidget(self.chk_live_telemetry)

        # Literature processing options stay available but are deliberately
        # compact; the default workflow reads context without spawning plots.
        self.chk_extract_lit = QCheckBox("Extract numeric tables/series found in literature")
        self.chk_extract_lit.setChecked(True); v.addWidget(self.chk_extract_lit)
        self.chk_lit_overlay = QCheckBox("Use literature data as datasets & overlay recreated plots")
        self.chk_lit_overlay.setToolTip("After reading a paper, register every extracted table as a dataset (saved under "
                                        "literature_dataset/) and overlay the recreated experimental curves on the active simulation graph.")
        self.chk_lit_overlay.setChecked(False); v.addWidget(self.chk_lit_overlay)
        self.chk_ocr = QCheckBox("Use OCR only when the PDF has no usable text layer")
        self.chk_ocr.setChecked(True); v.addWidget(self.chk_ocr)
        literature_budget_row = QHBoxLayout()
        literature_budget_row.addWidget(QLabel("Literature scan limit"))
        self.spin_literature_budget = QSpinBox(); self.spin_literature_budget.setMaximumWidth(92)
        self.spin_literature_budget.setToolTip("Maximum extraction/OCR time per literature file. Long reviews save extracted text so later Smart Suite runs do not reread the source document.")
        self.cb_literature_budget_unit = QComboBox(); self.cb_literature_budget_unit.addItems(["Seconds", "Minutes", "Hours"])
        self.cb_literature_budget_unit.setMaximumWidth(86)
        saved_lit_budget = max(10, self.settings.value("literature/time_budget_seconds", 1800, int))
        saved_lit_unit = self.settings.value("literature/time_budget_unit", "Minutes", str)
        self.cb_literature_budget_unit.setCurrentText(saved_lit_unit if saved_lit_unit in ("Seconds", "Minutes", "Hours") else "Minutes")
        self._sync_literature_budget_editor(saved_lit_budget)
        self.spin_literature_budget.valueChanged.connect(self._store_literature_budget)
        self.cb_literature_budget_unit.currentTextChanged.connect(self._literature_budget_unit_changed)
        literature_budget_row.addWidget(self.spin_literature_budget); literature_budget_row.addWidget(self.cb_literature_budget_unit)
        literature_budget_row.addStretch(1); v.addLayout(literature_budget_row)
        self.chk_auto_graphs = QCheckBox("Open reconstructed literature graph tabs immediately")
        self.chk_auto_graphs.setChecked(False); v.addWidget(self.chk_auto_graphs)

        self.chk_assumed_dataset = QCheckBox("Selected dataset contains assumed / incomplete data")
        self.chk_assumed_dataset.toggled.connect(self._set_selected_assumed); v.addWidget(self.chk_assumed_dataset)
        self.txt_assumption_note = QLineEdit(); self.txt_assumption_note.setPlaceholderText("Optional assumption / missing-data note…")
        self.txt_assumption_note.editingFinished.connect(self._set_selected_assumption_note); v.addWidget(self.txt_assumption_note)
        return g

    def _group_chart(self):
        g = QGroupBox("Graph library")
        v = QVBoxLayout(g)
        # Keep a hidden engine combo as the canonical state value for sessions,
        # undo/redo and the renderer.  The user-facing control is the searchable
        # categorized GraphPicker below.
        self.cb_chart = QComboBox()
        self.cb_chart.addItems(CHART_TYPES)
        self.cb_chart.currentIndexChanged.connect(self.on_chart_changed)
        self.cb_chart.hide()

        self.picker = GraphPicker()
        self.picker.entrySelected.connect(self._on_picker_entry_selected)
        v.addWidget(self.picker, 1)
        graph_apply_row = QHBoxLayout()
        self.btn_apply_graph_choice = QPushButton("▶  Apply selected visualisation")
        self.btn_apply_graph_choice.setEnabled(False)
        self.btn_apply_graph_choice.setToolTip("Graph selections apply automatically. This button remains for compatibility.")
        self.btn_apply_graph_choice.clicked.connect(lambda: self._apply_pending_graph_selection(render=True))
        self.btn_apply_graph_choice.setVisible(manual_apply_mode())
        graph_apply_row.addWidget(self.btn_apply_graph_choice, 1)
        v.addLayout(graph_apply_row)

        scan_row = QHBoxLayout()
        self.btn_scan_dataset = QPushButton("Scan Dataset")
        self.btn_scan_dataset.setToolTip("Deep-profile the selected dataset, score variable relationships, choose graph-specific X/Y/Z mappings, and cache the result in this project.")
        self.btn_scan_dataset.clicked.connect(lambda: self.start_dataset_scan(force=True))
        scan_row.addWidget(self.btn_scan_dataset)
        self.btn_scan_literature = QPushButton("Scan + Literature…")
        self.btn_scan_literature.setToolTip("Attach/read a paper or thesis and use its semantic context when scoring variable pairings and visualisations.")
        self.btn_scan_literature.clicked.connect(self.scan_dataset_with_literature)
        scan_row.addWidget(self.btn_scan_literature)
        v.addLayout(scan_row)
        self.lbl_scan_status = QLabel("Run Scan Dataset for graph-specific mappings; results are cached locally.")
        self.lbl_scan_status.setWordWrap(True); self.lbl_scan_status.setObjectName("DatasetScanStatus")
        v.addWidget(self.lbl_scan_status)

        self.lbl_recommended = QLabel("Recommended: load a dataset")
        self.lbl_recommended.setWordWrap(True)
        self.lbl_recommended.setObjectName("RecommendedGraphsLabel")
        v.addWidget(self.lbl_recommended)

        self.lbl_chart_desc = QLabel(CHART_DESCRIPTIONS.get(self.cb_chart.currentText(), ""))
        self.lbl_chart_desc.setWordWrap(True)
        self.lbl_chart_desc.setObjectName("GraphDescriptionLabel")
        v.addWidget(self.lbl_chart_desc)

        row = QHBoxLayout()
        row.addWidget(QLabel("Colourmap:"))
        self.cb_colourmap = QComboBox()
        populate_colormap_combo(self.cb_colourmap, "Parula")
        row.addWidget(self._staged_combo_widget(self.cb_colourmap, "Maps numeric response values to colours. Turbo is a strong scientific rainbow; perceptual maps are safer for quantitative comparison."), 1)
        v.addLayout(row)

        self.expr_box = QWidget()
        ef = QFormLayout(self.expr_box); ef.setContentsMargins(0,4,0,0)
        self.txt_expression = QLineEdit("sin(x)")
        self.txt_expression.setPlaceholderText("e.g. sin(x), x^2+y^2, or cos(t);sin(t);t/4")
        self.txt_expression.editingFinished.connect(self.queue_render)
        self.txt_expr_domain = QLineEdit("-10,10")
        self.txt_expr_domain.setPlaceholderText("Domains: xmin,xmax; ymin,ymax; zmin,zmax")
        self.txt_expr_domain.editingFinished.connect(self.queue_render)
        ef.addRow("Expression:", self.txt_expression); ef.addRow("Domain(s):", self.txt_expr_domain)
        self.expr_box.setVisible(False)
        v.addWidget(self.expr_box)
        self.picker.setCurrentEngine(self.cb_chart.currentText())
        return g

    def _mapping_for_unmapped_roles(self, mapping: dict | None) -> dict:
        """Filter a suggested mapping down to roles without a user selection.

        A role whose combo already holds a valid column keeps the user's
        choice; only genuinely unmapped roles receive the suggestion, so
        switching visualisation types preserves configured axes.
        """
        if not mapping:
            return {}
        combos = {"x": self.cb_x, "y": self.cb_y, "z": self.cb_z, "w": self.cb_w, "v": self.cb_v,
                  "matrix": self.cb_matrix, "gradient": self.cb_gradient}
        filtered = {}
        for role, value in mapping.items():
            cb = combos.get(role)
            if cb is None:
                filtered[role] = value
                continue
            if cb.currentData() is None:
                filtered[role] = value
        return filtered

    def _apply_mapping_dict(self, mapping: dict | None) -> None:
        if not mapping:
            return
        combos = {"x": self.cb_x, "y": self.cb_y, "z": self.cb_z, "w": self.cb_w, "v": self.cb_v,
                  "matrix": self.cb_matrix, "gradient": self.cb_gradient}
        for role, value in mapping.items():
            cb = combos.get(role)
            if cb is None or value is None:
                continue
            cb.blockSignals(True)
            try:
                if not self._combo_set_data(cb, value):
                    # matrix mappings are stored with an explicit prefix while
                    # the UI keeps the bare matrix/volume key as item data.
                    if isinstance(value, str) and ":" in value:
                        self._combo_set_data(cb, value.split(":", 1)[1])
            finally:
                cb.blockSignals(False)

    def _on_picker_entry_selected(self, entry: dict) -> None:
        """Stage a graph-library choice; it auto-applies after a short debounce."""
        engine = entry.get("engine") if isinstance(entry, dict) else None
        if not engine or self.cb_chart.findText(engine) < 0:
            return
        self._pending_graph_entry = dict(entry)
        self.lbl_chart_desc.setText(entry.get("description") or CHART_DESCRIPTIONS.get(engine, ""))
        if hasattr(self, "btn_apply_graph_choice"):
            self.btn_apply_graph_choice.setEnabled(True)
            self.btn_apply_graph_choice.setText(f"▶  Apply {entry.get('name') or engine}")
        if manual_apply_for(engine):
            self.statusBar().showMessage(
                f"Queued visualisation: {entry.get('name') or engine} — press ▶ Apply to render it.", 5000)
        else:
            self.statusBar().showMessage(f"Selected visualisation: {entry.get('name') or engine} — applying…", 4000)
            # Instant auto-apply: the staged graph selection commits automatically.
            self._staged_commit_timer.start()

    def _apply_pending_graph_selection(self, *, render: bool = True) -> None:
        entry = dict(self._pending_graph_entry or {})
        engine = entry.get("engine")
        if not engine or self.cb_chart.findText(engine) < 0:
            return
        record_activity("Graph applied", f"engine={engine}; library_name={entry.get('name', engine)}")
        self.cb_chart.blockSignals(True); self.cb_chart.setCurrentText(engine); self.cb_chart.blockSignals(False)
        # Persist user state across visualisation switches: the graph's scale
        # preset only fills axes still at their Linear default, and never
        # overrides scales the user configured explicitly.
        scale = entry.get("scale") or {"EIS: Bode": "Logarithmic Scale", "Power Spectral Density": "Semi-Log Y"}.get(engine)
        user_scales_customised = any(
            canonical_axis_scale(self._applied_combo_text(cb)) != "Linear"
            for cb in (self.cb_x_scale, self.cb_y_scale, self.cb_z_scale))
        if scale and not user_scales_customised:
            self._apply_legacy_axis_scale(scale)
        self.lbl_chart_desc.setText(entry.get("description") or CHART_DESCRIPTIONS.get(engine, ""))
        self._apply_axis_roles()
        # Cached/heuristic mappings only fill roles the user has not mapped —
        # existing selections carry across unless the new chart cannot use them.
        scan = self._scan_cache_for(self.primary_dataset())
        mapping = self._mapping_for_unmapped_roles(best_mapping_for_graph(scan, engine))
        if mapping:
            self._apply_mapping_dict(mapping)
            axes = ", ".join(f"{k.upper()}={v}" for k, v in mapping.items() if k in ("x", "y", "z") and v)
            if hasattr(self, "lbl_scan_status") and axes:
                self.lbl_scan_status.setText(f"Filled unmapped roles for {engine}: {axes}")
        else:
            ds = self.primary_dataset()
            if ds is not None:
                self._apply_mapping_dict(self._mapping_for_unmapped_roles(suggest_axis_mapping(ds)))
        for combo in (self.cb_x, self.cb_y, self.cb_z, self.cb_w, self.cb_v, self.cb_matrix, self.cb_gradient,
                      self.cb_x_scale, self.cb_y_scale, self.cb_z_scale):
            if combo in self._staged_combos:
                self._sync_staged_combo(combo)
        self._pending_graph_entry = None
        if hasattr(self, "btn_apply_graph_choice"):
            self.btn_apply_graph_choice.setEnabled(False); self.btn_apply_graph_choice.setText("▶  Apply selected visualisation")
        if render:
            self.queue_render()

    def _axis_mapping_row(self, variable_combo: QComboBox, scale_combo: QComboBox, axis_name: str) -> QWidget:
        """Staged variable selector row; the axis-scale combo is configured here
        but lives in the Axis & Label Settings panel."""
        row = QWidget(); lay = QHBoxLayout(row); lay.setContentsMargins(0, 0, 0, 0); lay.setSpacing(4)
        variable_combo.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        variable_combo.setToolTip(f"{axis_name} variable mapping. Choose the data field assigned to this graph role; it applies automatically.")
        scale_combo.addItems(list(AXIS_SCALE_OPTIONS)); scale_combo.setCurrentText("Linear")
        scale_combo.setMinimumHeight(28)
        scale_combo.setToolTip(
            f"{axis_name} scale. Log10 / Ln / Log2 use logarithmic spacing (and the log-space interpolation metric for "
            "spatial surface axes); Sqrt is a signed square-root scale; Symlog keeps a linear zone around zero for "
            "signed data; Logit is for probabilities strictly between 0 and 1; Date-Time formats numeric "
            "timestamps (epoch s/ms/ns or matplotlib datenums) chronologically; Categorical spaces the distinct "
            "mapped values evenly as discrete groups.")
        lay.addWidget(variable_combo, 1)
        lay.addWidget(self._register_staged_combo(variable_combo), 0)
        return row

    def _apply_legacy_axis_scale(self, scale_type: object) -> None:
        """Translate old graph-library scale presets to the new per-axis UI."""
        xs, ys, zs = legacy_scale_to_axes(scale_type)
        for combo, value in ((self.cb_x_scale, xs), (self.cb_y_scale, ys), (self.cb_z_scale, zs)):
            combo.blockSignals(True)
            combo.setCurrentText(value)
            combo.blockSignals(False)
            if combo in self._staged_combos:
                self._sync_staged_combo(combo)

    def _group_mapping(self):
        g = QGroupBox("Variable mapping (independent axis scaling)")
        f = QFormLayout(g)
        self.map_labels = {}
        self.cb_x, self.cb_y, self.cb_z, self.cb_w, self.cb_v = (QComboBox() for _ in range(5))
        self.cb_x_scale, self.cb_y_scale, self.cb_z_scale = (QComboBox() for _ in range(3))
        self.cb_y2 = QComboBox()
        self.cb_matrix, self.cb_gradient = QComboBox(), QComboBox()
        for _cb in (self.cb_x, self.cb_y, self.cb_z, self.cb_w, self.cb_v, self.cb_y2, self.cb_matrix, self.cb_gradient):
            _cb.setItemDelegate(MutedSuffixDelegate(_cb))
            try:
                _cb.setLabelDrawingMode(QComboBox.LabelDrawingMode.UseDelegate)
            except Exception:
                pass
        self.cb_fifth_mode = QComboBox()
        self.cb_fifth_mode.addItems(FIFTH_AXIS_MODES)
        self.cb_fifth_mode.setToolTip("Controls how a fifth mapped variable affects points: size, opacity, or the supported geometry channel.")

        scale_rows = {
            'x': self._axis_mapping_row(self.cb_x, self.cb_x_scale, "X-axis"),
            'y': self._axis_mapping_row(self.cb_y, self.cb_y_scale, "Y-axis"),
            'z': self._axis_mapping_row(self.cb_z, self.cb_z_scale, "Z / response / colour"),
        }
        for key, cb, text in (('x', self.cb_x, "X axis:"), ('y', self.cb_y, "Y axis:"), ('z', self.cb_z, "Z / response:"),
                              ('w', self.cb_w, "4th axis (colour):"), ('v', self.cb_v, "5th axis (size/opacity):"),
                              ('matrix', self.cb_matrix, "Matrix / volume:"), ('gradient', self.cb_gradient, "Gradient field:")):
            cb.setMinimumHeight(28)
            lbl = QLabel(text)
            self.map_labels[key] = lbl
            role_help = {
                "x": "Horizontal independent coordinate.", "y": "Vertical independent coordinate.",
                "z": "Primary response: surface height or colour value.", "w": "Optional fourth variable, usually colour.",
                "v": "Optional fifth variable, usually size or opacity.", "matrix": "Pre-gridded N×M response matrix for structured surface estimators.",
                "gradient": "Variable used to calculate/overlay a gradient or vector field.",
            }.get(key, "Graph variable mapping.")
            lbl.setToolTip(role_help); cb.setToolTip(role_help + " Select a field, then press ▶ to commit it.")
            mapping_widget = scale_rows[key] if key in scale_rows else self._staged_combo_widget(cb)
            f.addRow(lbl, mapping_widget)
            if key == 'v':
                self.lbl_fifth_mode = QLabel("5th axis mode:")
                f.addRow(self.lbl_fifth_mode, self._staged_combo_widget(self.cb_fifth_mode))
                self.chk_fifth_invert = QCheckBox("Invert opacity")
                self.chk_fifth_invert.setToolTip("Flip the 5th-axis opacity mapping (alpha = 1 − normalized value): "
                                                 "high values fade instead of low ones, letting you dim dense clouds "
                                                 "of low-performing background points — or the reverse.")
                self.chk_fifth_invert.toggled.connect(self.queue_render)
                f.addRow("", self.chk_fifth_invert)
        self.cb_y2.setMinimumHeight(28)
        self.cb_y2.setToolTip("Optional second vertical response plotted on a separate Y scale when the selected graph supports it.")
        self.lbl_y2 = QLabel("Secondary Y axis:")
        f.addRow(self.lbl_y2, self._staged_combo_widget(self.cb_y2))
        self.btn_auto_map = QPushButton("Auto-map X / Y / Z from names, units & data shape")
        self.btn_auto_map.clicked.connect(self.auto_map_axes)
        f.addRow(self.btn_auto_map)
        self.btn_lhs_map = QPushButton("Map LHS sweep (flow→X, area→Y, Ea→Z, VHPR→colour)")
        self.btn_lhs_map.setToolTip("Enforce the canonical Latin-Hypercube assignment: X=lhs_flow, Y=lhs_area, "
                                    "Z=lhs_ea and 4th-axis colour=total_VHPR_mean (fuzzy column matching).")
        self.btn_lhs_map.clicked.connect(self.apply_lhs_canonical_mapping)
        f.addRow(self.btn_lhs_map)
        # Axis right-click unit conversion menus.
        for key, cb in (("x",self.cb_x),("y",self.cb_y),("z",self.cb_z),("w",self.cb_w),("v",self.cb_v),("y2",self.cb_y2)):
            cb.setContextMenuPolicy(Qt.CustomContextMenu)
            cb.customContextMenuRequested.connect(lambda pos,k=key,c=cb: self._axis_unit_menu(k,c,c.mapToGlobal(pos)))
        self.populate_axes(None)
        return g

    def _spin_row(self, form, label, lo, hi, key):
        lay = QHBoxLayout()
        smin, smax = QDoubleSpinBox(), QDoubleSpinBox()
        for s in (smin, smax):
            s.setRange(lo, hi); s.setDecimals(4); s.setMinimumHeight(26)
            s.valueChanged.connect(self.queue_render)
        smax.setValue(100.0)
        lay.addWidget(smin); lay.addWidget(QLabel("to")); lay.addWidget(smax)
        b = QPushButton("Fit"); b.setMaximumWidth(48)
        b.clicked.connect(lambda _=False, k=key: self.fit_axis_bounds(k))
        lay.addWidget(b)
        lbl = QLabel(label)
        form.addRow(lbl, lay)
        self.clip_labels[key] = lbl
        return smin, smax

    def _group_clipping(self):
        g = QGroupBox("Interactive per-axis range clipping")
        f = QFormLayout(g)
        self.clip_labels = {}
        self.spin_xmin, self.spin_xmax = self._spin_row(f, "X min/max:", -1e100, 1e100, 'x')
        self.spin_ymin, self.spin_ymax = self._spin_row(f, "Y min/max:", -1e100, 1e100, 'y')
        self.spin_zmin, self.spin_zmax = self._spin_row(f, "Z / colour min/max:", -1e100, 1e100, 'z')
        self.spin_wmin, self.spin_wmax = self._spin_row(f, "4th axis min/max:", -1e100, 1e100, 'w')
        self.spin_vmin, self.spin_vmax = self._spin_row(f, "5th axis min/max:", -1e100, 1e100, 'v')
        self.chk_use_clip = QCheckBox("Apply clipping ranges (unchecked = auto-fit)")
        self.chk_use_clip.toggled.connect(self.queue_render)
        f.addRow("", self.chk_use_clip)
        b = QPushButton("Fit all axes to data")
        b.clicked.connect(self.fit_all_axes)
        f.addRow("", b)
        return g

    def _group_surface_estimation(self):
        g = QGroupBox("Surface estimation & noise control")
        f = QFormLayout(g)

        self.cb_estimator = QComboBox()
        # QComboBox uses a QStandardItemModel internally. Disabled, bold items
        # act as category headers while preserving keyboard/search behaviour.
        for category, methods in ESTIMATOR_CATEGORIES:
            self.cb_estimator.addItem(f"— {category} —")
            header_index = self.cb_estimator.count() - 1
            item = self.cb_estimator.model().item(header_index)
            if item is not None:
                item.setEnabled(False)
                font = item.font(); font.setBold(True); item.setFont(font)
            for method in methods:
                self.cb_estimator.addItem(method)
        self.cb_estimator.setCurrentText("Auto (data-aware)")
        self.cb_estimator.setToolTip(
            "Choose a structured-grid, scattered, RBF, kriging or local-regression estimator. "
            "Log10 X/Y axes automatically use normalized log-space distances."
        )
        self.cb_estimator.currentTextChanged.connect(self._surface_estimator_changed)
        f.addRow("Estimator:", self._staged_combo_widget(self.cb_estimator))

        self.cb_bin_stat = QComboBox()
        self.cb_bin_stat.addItems(["mean", "median", "std", "count"])
        self.cb_bin_stat.setToolTip("How duplicate X/Y coordinates are combined before interpolation.")
        f.addRow("Duplicate/bin statistic:", self._staged_combo_widget(self.cb_bin_stat))

        self.spin_surface_neighbors = QSpinBox()
        self.spin_surface_neighbors.setRange(4, 128); self.spin_surface_neighbors.setValue(32)
        self.spin_surface_neighbors.setToolTip("Local neighbour count for IDW, Shepard, Sibson, RBF, kriging, MLS and LOESS. Higher is smoother but slower.")
        self.spin_surface_neighbors.valueChanged.connect(self.queue_render)
        f.addRow("Local neighbours:", self.spin_surface_neighbors)

        self.spin_idw_power = QDoubleSpinBox()
        self.spin_idw_power.setRange(0.25, 8.0); self.spin_idw_power.setDecimals(2); self.spin_idw_power.setSingleStep(0.25); self.spin_idw_power.setValue(2.0)
        self.spin_idw_power.setToolTip("IDW exponent p in 1/d^p. Typical scientific values are 1–3.")
        self.spin_idw_power.valueChanged.connect(self.queue_render)
        f.addRow("IDW power:", self.spin_idw_power)

        self.spin_loess_fraction = QDoubleSpinBox()
        self.spin_loess_fraction.setRange(0.02, 1.0); self.spin_loess_fraction.setDecimals(2); self.spin_loess_fraction.setSingleStep(0.05); self.spin_loess_fraction.setValue(0.25)
        self.spin_loess_fraction.setToolTip("Fraction of samples considered by 2-D LOESS/LOWESS before the local-neighbour cap is applied.")
        self.spin_loess_fraction.valueChanged.connect(self.queue_render)
        f.addRow("LOESS span:", self.spin_loess_fraction)

        self.cb_kriging_variogram = QComboBox()
        self.cb_kriging_variogram.addItems(["Exponential", "Spherical", "Gaussian"])
        self.cb_kriging_variogram.setToolTip("Semivariogram family used by Ordinary Kriging in the normalized spatial metric.")
        f.addRow("Kriging variogram:", self._staged_combo_widget(self.cb_kriging_variogram))

        self.cb_surface_value_policy = QComboBox()
        self.cb_surface_value_policy.addItems(["Allow estimator overshoot", "Clamp to observed response range"])
        self.cb_surface_value_policy.setToolTip("Spline/RBF/local estimators can overshoot between samples. Optional clamping prevents nonphysical values without changing colorbar clipping.")
        f.addRow("Estimator bounds:", self._staged_combo_widget(self.cb_surface_value_policy))

        self.cb_surface_response_space = QComboBox()
        self.cb_surface_response_space.addItems(["Linear values", "Log10 values"])
        self.cb_surface_response_space.setToolTip("Controls the response space used by the estimator itself. Log10 response interpolation is useful for strictly positive quantities spanning orders of magnitude; this is independent of colorbar Log10 normalization.")
        f.addRow("Interpolate response in:", self._staged_combo_widget(self.cb_surface_response_space))

        metric_row = QHBoxLayout()
        self.spin_surface_x_metric = QDoubleSpinBox(); self.spin_surface_x_metric.setRange(0.05, 20.0); self.spin_surface_x_metric.setDecimals(2); self.spin_surface_x_metric.setValue(1.0); self.spin_surface_x_metric.setSingleStep(0.1)
        self.spin_surface_y_metric = QDoubleSpinBox(); self.spin_surface_y_metric.setRange(0.05, 20.0); self.spin_surface_y_metric.setDecimals(2); self.spin_surface_y_metric.setValue(1.0); self.spin_surface_y_metric.setSingleStep(0.1)
        metric_tip = "Relative weighting of X and Y in the normalized interpolation distance metric. 1:1 is auto-equalized; increase one axis only when scientific anisotropy is known."
        self.spin_surface_x_metric.setToolTip(metric_tip); self.spin_surface_y_metric.setToolTip(metric_tip)
        self.spin_surface_x_metric.valueChanged.connect(self.queue_render); self.spin_surface_y_metric.valueChanged.connect(self.queue_render)
        metric_row.addWidget(QLabel("X")); metric_row.addWidget(self.spin_surface_x_metric); metric_row.addWidget(QLabel("Y")); metric_row.addWidget(self.spin_surface_y_metric)
        f.addRow("Distance metric weights:", metric_row)

        self.cb_surface_extrapolation = QComboBox()
        self.cb_surface_extrapolation.addItems([
            "Mask outside convex hull",
            "Nearest fill outside hull",
            "IDW full-domain extension",
            "Linear + nearest full rectangle",
            "Edge-clamped full rectangle",
        ])
        self.cb_surface_extrapolation.setToolTip("Convex-hull masking is the conservative scientific default. Full-domain modes deliberately extend the estimate to the complete rectangular X×Y sweep.")
        f.addRow("Extrapolation:", self._staged_combo_widget(self.cb_surface_extrapolation))

        self.cb_invalid_policy = QComboBox()
        self.cb_invalid_policy.addItems(["Sample cell only", "Conservative local region", "Voronoi failure region", "None (fit through failures)"])
        self.cb_invalid_policy.setToolTip("Controls how much area a NaN/Inf solver failure masks. 'Sample cell only' avoids the large white Voronoi holes seen in dense MEC sweeps.")
        f.addRow("Failed-point footprint:", self._staged_combo_widget(self.cb_invalid_policy))

        bridge_row = QHBoxLayout()
        self.cb_failure_bridge = QComboBox()
        self.cb_failure_bridge.addItems(["Preserve all failures", "Bridge isolated failures", "Bridge small enclosed holes", "Bridge all interior holes"])
        self.cb_failure_bridge.setCurrentText("Bridge isolated failures")
        self.cb_failure_bridge.setToolTip("Topology-aware NaN handling. Small enclosed solver dropouts can be shown using the surrounding estimator while large or boundary-connected washout/failure zones remain masked.")
        self.spin_failure_bridge_cells = QSpinBox(); self.spin_failure_bridge_cells.setRange(1, 2500); self.spin_failure_bridge_cells.setValue(4)
        self.spin_failure_bridge_cells.setToolTip("Maximum connected failed area (in output grid cells) that may be bridged by 'small enclosed holes'. Boundary-connected failures are never bridged.")
        self.spin_failure_bridge_cells.valueChanged.connect(self.queue_render)
        bridge_row.addWidget(self._staged_combo_widget(self.cb_failure_bridge), 1); bridge_row.addWidget(QLabel("max cells")); bridge_row.addWidget(self.spin_failure_bridge_cells)
        f.addRow("Solver-dropout bridging:", bridge_row)
        self.chk_show_imputed_cells = QCheckBox("Mark bridged cells")
        self.chk_show_imputed_cells.setToolTip("Show a small outline over cells whose response was inferred across an ODE/solver dropout, preserving provenance instead of presenting imputed values as measured simulations.")
        self.chk_show_imputed_cells.toggled.connect(self.queue_render)
        f.addRow("Imputation provenance:", self.chk_show_imputed_cells)

        invalid_row = QHBoxLayout()
        self.cb_invalid_mode = QComboBox()
        self.cb_invalid_mode.addItems(["Transparent", "Fallback colour",
                                       "Nearest Neighbor Fill", "Local Mean Imputation",
                                       "Baseline Clamp (colour-scale minimum)", "Symmetric Mirror Fill"])
        self.cb_invalid_mode.setToolTip(
            "How failed/non-convergent solver cells render. Transparent leaves holes; Fallback colour paints them a "
            "flat colour; Nearest Neighbor copies the closest valid cell; Local Mean averages active neighbours; "
            "Baseline Clamp forces the colour-scale minimum; Symmetric Mirror reflects surrounding topography across "
            "the gap. Imputation never touches cells outside the convex hull, and imputed cells are flagged in the "
            "provenance overlay.")
        self.cb_invalid_mode.setToolTip("NaN/Inf response coordinates are excluded from the fit and preserved as a failure mask.")
        self.btn_invalid_color = ColorButton("#DDDDDD", "Fallback colour for failed/invalid simulation coordinates")
        self.btn_invalid_color.colorChanged.connect(lambda _: self.queue_render())
        invalid_row.addWidget(self._staged_combo_widget(self.cb_invalid_mode), 1); invalid_row.addWidget(self.btn_invalid_color, 0)
        f.addRow("Invalid / failed data:", invalid_row)

        self.cb_colorbar_extend = QComboBox()
        self.cb_colorbar_extend.addItems(["Auto (from clipping)", "Neither", "Min", "Max", "Both"])
        self.cb_colorbar_extend.setToolTip("Auto detects values beyond active Z clipping bounds. Max/both keeps clipped spikes at the terminal colormap colour instead of leaving gaps.")
        f.addRow("Colourbar extend:", self._staged_combo_widget(self.cb_colorbar_extend))

        render_row = QHBoxLayout()
        self.cb_field_render_mode = QComboBox(); self.cb_field_render_mode.addItems(["Continuous shading", "Discrete contour bands"])
        self.cb_field_render_mode.setToolTip("Continuous uses MATLAB-like interpolated shading; discrete uses filled contour bands for exact visual bins.")
        self.spin_contour_levels = QSpinBox(); self.spin_contour_levels.setRange(3, 100); self.spin_contour_levels.setValue(10)
        self.spin_contour_levels.setToolTip("Number of stepped colour bands / primary contour levels.")
        self.spin_contour_levels.valueChanged.connect(self.queue_render)
        render_row.addWidget(self._staged_combo_widget(self.cb_field_render_mode), 1); render_row.addWidget(QLabel("Levels")); render_row.addWidget(self.spin_contour_levels)
        f.addRow("Field shading:", render_row)

        contour_overlay_row = QHBoxLayout()
        self.chk_contour_overlay = QCheckBox("Overlay iso-lines")
        self.chk_contour_overlay.setToolTip("Draw an independent contour-line layer over the continuous heatmap, equivalent to combining MATLAB imagesc/pcolor with contour.")
        self.chk_contour_overlay.toggled.connect(self.queue_render)
        self.spin_contour_overlay_levels = QSpinBox(); self.spin_contour_overlay_levels.setRange(2, 100); self.spin_contour_overlay_levels.setValue(10)
        self.spin_contour_overlay_levels.setToolTip("Number of overlaid iso-lines."); self.spin_contour_overlay_levels.valueChanged.connect(self.queue_render)
        self.btn_contour_overlay_color = ColorButton("#FFFFFF", "Contour overlay line colour")
        self.btn_contour_overlay_color.colorChanged.connect(lambda _: self.queue_render())
        self.spin_contour_overlay_width = QDoubleSpinBox(); self.spin_contour_overlay_width.setRange(0.1, 4.0); self.spin_contour_overlay_width.setDecimals(2); self.spin_contour_overlay_width.setValue(0.6)
        self.spin_contour_overlay_width.setToolTip("Contour overlay line width."); self.spin_contour_overlay_width.valueChanged.connect(self.queue_render)
        self.chk_contour_overlay_labels = QCheckBox("labels"); self.chk_contour_overlay_labels.setToolTip("Label overlaid contour values."); self.chk_contour_overlay_labels.toggled.connect(self.queue_render)
        contour_overlay_row.addWidget(self.chk_contour_overlay); contour_overlay_row.addWidget(QLabel("N")); contour_overlay_row.addWidget(self.spin_contour_overlay_levels); contour_overlay_row.addWidget(self.btn_contour_overlay_color); contour_overlay_row.addWidget(self.spin_contour_overlay_width); contour_overlay_row.addWidget(self.chk_contour_overlay_labels)
        f.addRow("Contour overlay:", contour_overlay_row)

        self.btn_series_color = ColorButton("#2980B9", "Select series colour")
        self.btn_series_color.colorChanged.connect(lambda _: self.queue_render())
        f.addRow("Series colour:", self.btn_series_color)

        self.slider_res = QSlider(Qt.Horizontal)
        self.slider_res.setRange(32, 500); self.slider_res.setValue(160)
        self.slider_res.setToolTip("Final interpolation grid resolution per axis. Interactive preview may be capped for responsiveness; export uses the requested value.")
        self.slider_res.sliderPressed.connect(self._start_surface_slider_drag)
        self.slider_res.valueChanged.connect(self._queue_surface_slider_preview)
        self.slider_res.sliderReleased.connect(self._finish_surface_slider_drag)
        f.addRow("Grid resolution:", self.slider_res)

        self.slider_smoothing = QSlider(Qt.Horizontal)
        self.slider_smoothing.setRange(0, 1000); self.slider_smoothing.setValue(12)
        self.slider_smoothing.setToolTip("Mask-aware Gaussian post-smoothing sigma ×10. Failed regions remain masked instead of being filled by the filter.")
        self.slider_smoothing.sliderPressed.connect(self._start_surface_slider_drag)
        self.slider_smoothing.valueChanged.connect(self._queue_surface_slider_preview)
        self.slider_smoothing.sliderReleased.connect(self._finish_surface_slider_drag)
        f.addRow("Gaussian smoothing (σ×10, up to 100):", self.slider_smoothing)

        self.chk_progressive_surface = QCheckBox("Progressive preview while dragging surface sliders")
        self.chk_progressive_surface.setChecked(True)
        self.chk_progressive_surface.setToolTip("During an active slider drag GraphVis renders a ~48×48/coarsened surface, then automatically restores full fidelity on release.")
        self.chk_progressive_surface.toggled.connect(self.queue_render)
        f.addRow("", self.chk_progressive_surface)

        self.chk_auto_surface_presentation = QCheckBox("Auto scientific surface presentation (tight rectangular axes)")
        self.chk_auto_surface_presentation.setChecked(True)
        self.chk_auto_surface_presentation.setToolTip("Uses exact field extents and compact publication-style surface layout without changing your data or estimator.")
        self.chk_auto_surface_presentation.toggled.connect(self.queue_render)
        f.addRow("", self.chk_auto_surface_presentation)
        self.btn_matlab_surface = QPushButton("MATLAB-style surface preset")
        self.btn_matlab_surface.setCheckable(True)
        self.btn_matlab_surface.setToolTip("Toggle: applies Turbo + continuous shading + compact failed-point masks + "
                                           "full rectangular edge fill (source data untouched). Click again to revert "
                                           "to the configuration that was active before the preset.")
        self.btn_matlab_surface.clicked.connect(self._toggle_matlab_surface_preset)
        self._matlab_preset_backup: dict | None = None
        f.addRow("", self.btn_matlab_surface)

        self.lbl_surface_metric = QLabel("Log-aware metric: automatic — Log10 X/Y axes are interpolated in normalized logarithmic coordinates.")
        self.lbl_surface_metric.setWordWrap(True)
        self.lbl_surface_metric.setStyleSheet("font-size:9px;")
        f.addRow("", self.lbl_surface_metric)

        self.chk_pareto_constrain = QCheckBox("Constrain frontier to X/Y clip box"); self.chk_pareto_constrain.setChecked(True)
        self.chk_pareto_max = QCheckBox("Maximise Y (uncheck to minimise)"); self.chk_pareto_max.setChecked(True)
        self.chk_pareto_boot = QCheckBox("Bootstrap confidence band"); self.chk_pareto_boot.setChecked(True)
        self.chk_pareto_only = QCheckBox("Pareto view: show frontier / optimal points only")
        for chk in (self.chk_pareto_constrain, self.chk_pareto_max, self.chk_pareto_boot, self.chk_pareto_only):
            chk.toggled.connect(self.queue_render)
            f.addRow("", chk)
        robust = QPushButton("Robust auto-clip all axes (1st–99th percentile)")
        robust.clicked.connect(self.robust_fit_all_axes)
        f.addRow("", robust)
        self._sync_surface_estimator_controls()
        return g

    def _surface_estimator_changed(self, *_):
        # Enable/disable estimator-specific options immediately, but leave the
        # rendered graph untouched until the estimator's ▶ button is pressed.
        self._sync_surface_estimator_controls()

    def _sync_surface_estimator_controls(self):
        if not hasattr(self, "cb_estimator"):
            return
        method = canonical_estimator(self.cb_estimator.currentText())
        local = method in {
            "Natural Neighbor (Sibson’s)", "Inverse Distance Weighting (IDW)", "Modified Shepard's Method",
            "Thin Plate Spline (TPS)", "Multiquadric RBF", "Gaussian RBF", "Ordinary Kriging",
            "Moving Least Squares (MLS)", "LOESS / LOWESS",
        }
        self.spin_surface_neighbors.setEnabled(local or method == "Auto (data-aware)")
        self.spin_idw_power.setEnabled(method in {"Inverse Distance Weighting (IDW)", "Auto (data-aware)"})
        self.spin_loess_fraction.setEnabled(method in {"LOESS / LOWESS", "Auto (data-aware)"})
        self.cb_kriging_variogram.setEnabled(method in {"Ordinary Kriging", "Auto (data-aware)"})

    def _toggle_matlab_surface_preset(self, checked: bool) -> None:
        """MATLAB preset acts as a toggle: second click reverts to the state
        captured immediately before the preset was applied."""
        if checked:
            self._matlab_preset_backup = self.snapshot_ui_state()
            self.apply_matlab_surface_preset()
            self.btn_matlab_surface.setText("MATLAB-style surface preset ✓ (click to revert)")
        else:
            backup, self._matlab_preset_backup = self._matlab_preset_backup, None
            self.btn_matlab_surface.setText("MATLAB-style surface preset")
            if backup is not None:
                self.apply_ui_state(copy.deepcopy(backup))
                self.statusBar().showMessage("MATLAB surface preset reverted to the previous configuration.", 3500)

    def apply_matlab_surface_preset(self) -> None:
        """Apply presentation defaults matching the supplied MATLAB heatmap."""
        self.cb_colourmap.setCurrentText("Turbo")
        self.cb_estimator.setCurrentText("Auto (data-aware)")
        self.cb_surface_extrapolation.setCurrentText("Linear + nearest full rectangle")
        self.cb_invalid_policy.setCurrentText("Sample cell only")
        self.cb_failure_bridge.setCurrentText("Bridge isolated failures")
        self.cb_invalid_mode.setCurrentText("Fallback colour")
        self.cb_field_render_mode.setCurrentText("Continuous shading")
        self.cb_colorbar_extend.setCurrentText("Auto (from clipping)")
        self.chk_auto_surface_presentation.setChecked(True)
        # The reference MEC plot has a logarithmic flow-rate axis. Apply that
        # only when the mapped Y data are positive and span at least two decades.
        ds = self.primary_dataset()
        ykey = self.cb_y.currentData()
        if ds is not None and ykey in getattr(ds, "df", {}).columns:
            try:
                vals = pd.to_numeric(ds.df[ykey], errors="coerce").to_numpy(float)
                vals = vals[np.isfinite(vals) & (vals > 0)]
                if vals.size and float(np.nanmax(vals) / np.nanmin(vals)) >= 100.0:
                    self.cb_y_scale.setCurrentText("Log10")
            except Exception:
                pass
        self._sync_all_staged_combos()
        self._apply_axis_roles(); self.queue_render()
        self.statusBar().showMessage("Applied MATLAB-style surface presentation preset.", 3500)

    def _group_limits(self):
        g = QGroupBox("Automated pattern recognition & limit detection")
        f = QFormLayout(g)
        self.chk_lim_plateau = QCheckBox("Steady-state plateau")
        self.chk_lim_asym = QCheckBox("Asymptotic limit (saturation fit)")
        self.chk_lim_conf = QCheckBox("Upper / lower confidence envelope")
        self.chk_lim_knee = QCheckBox("Knee / optimal trade-off point")
        self.chk_lim_pareto = QCheckBox("Pareto upper & lower bounds")
        for chk in (self.chk_lim_plateau, self.chk_lim_asym, self.chk_lim_conf, self.chk_lim_knee, self.chk_lim_pareto):
            chk.toggled.connect(self.queue_render)
            f.addRow("", chk)
        self.spin_level = QDoubleSpinBox(); self.spin_level.setRange(50, 99.9); self.spin_level.setValue(90); self.spin_level.setSuffix(" %")
        self.spin_level.valueChanged.connect(self.queue_render)
        f.addRow("Envelope level:", self.spin_level)
        self.spin_slope_tol = QDoubleSpinBox(); self.spin_slope_tol.setRange(0.001, 0.5); self.spin_slope_tol.setDecimals(3)
        self.spin_slope_tol.setSingleStep(0.005); self.spin_slope_tol.setValue(0.03)
        self.spin_slope_tol.valueChanged.connect(self.queue_render)
        f.addRow("Plateau slope tolerance:", self.spin_slope_tol)
        self.cb_limit_ls = QComboBox(); self.cb_limit_ls.addItems(["--", "-", ":", "-."])
        self.cb_limit_ls.setToolTip("Line pattern used for detected plateau/asymptote/limit boundaries. Press ▶ to commit the choice.")
        f.addRow("Boundary line style:", self._staged_combo_widget(self.cb_limit_ls))
        self.btn_limit_color = ColorButton(DEFAULT_LIMIT_STYLE["color"], "Boundary colour")
        self.btn_limit_color.colorChanged.connect(lambda _: self.queue_render())
        f.addRow("Boundary colour:", self.btn_limit_color)
        self.spin_band_alpha = QDoubleSpinBox(); self.spin_band_alpha.setRange(0.02, 0.8); self.spin_band_alpha.setSingleStep(0.02); self.spin_band_alpha.setValue(0.18)
        self.spin_band_alpha.valueChanged.connect(self.queue_render)
        f.addRow("Band opacity:", self.spin_band_alpha)
        self.chk_limit_labels = QCheckBox("Label detected limits in legend"); self.chk_limit_labels.setChecked(True)
        self.chk_limit_labels.toggled.connect(self.queue_render)
        f.addRow("", self.chk_limit_labels)
        # ---- data cleaning (moved here from the styling panel) -----------
        self.chk_outlier_mask = QCheckBox("Apply outlier mask")
        self.chk_outlier_mask.setToolTip("Clean the plotted subset before pattern recognition and rendering.")
        self.chk_outlier_mask.toggled.connect(self.queue_render)
        self.cb_mask_method = QComboBox()
        self.cb_mask_method.addItems(["Percentile Capping", "Z-Score Clip", "k-NN Distance Filter"])
        self.cb_mask_method.setToolTip("Outlier masking rule used when Apply outlier mask is enabled. "
                                       "Percentile Capping clips to the 1st–99th percentile; Z-Score Clip caps at ±3σ; "
                                       "k-NN Distance Filter drops isolated points with too few local neighbours.")
        f.addRow("", self.chk_outlier_mask)
        f.addRow("Mask method:", self._staged_combo_widget(self.cb_mask_method))
        self.spin_min_neighbors = QSpinBox()
        self.spin_min_neighbors.setRange(1, 64)
        self.spin_min_neighbors.setValue(8)
        self.spin_min_neighbors.setToolTip("Minimum local neighbours (k) for the k-NN distance outlier filter: a point whose "
                                           "k-th nearest neighbour is beyond the robust local-density fence is masked out.")
        self.spin_min_neighbors.valueChanged.connect(self.queue_render)
        f.addRow("Min local neighbours (k-NN):", self.spin_min_neighbors)
        self.txt_limits = QPlainTextEdit()
        self.txt_limits.setReadOnly(True)
        self.txt_limits.setMaximumHeight(90)
        self.txt_limits.setPlaceholderText("Detected limits appear here after rendering.")
        f.addRow(self.txt_limits)
        return g

    def _group_styling(self):
        g = QGroupBox("Axis & Label Settings")
        f = QFormLayout(g)
        # ---- explicit per-axis scale configuration -----------------------
        f.addRow(QLabel("Axis scales (Linear · Log10 · Ln · Log2 · Sqrt · Symlog · Logit · Date-Time · Categorical):"))
        f.addRow("X axis scale:", self._staged_combo_widget(self.cb_x_scale))
        f.addRow("Y axis scale:", self._staged_combo_widget(self.cb_y_scale))
        f.addRow("Z / response scale:", self._staged_combo_widget(self.cb_z_scale))
        # ---- per-element text / colour / size ----------------------------
        self.cb_target_element = QComboBox()
        self.cb_target_element.addItems(STYLE_ELEMENTS)
        self.cb_target_element.setToolTip("Choose which figure element to customise: title, X/Y/Z labels, tick labels, colourbar title or legend.")
        self.cb_target_element.currentTextChanged.connect(self._load_style_target)
        self.txt_element_value = QLineEdit()
        self.txt_element_value.setPlaceholderText("leave blank for automatic text")
        self.txt_element_value.textChanged.connect(self._store_style_target)
        self.btn_text_color = ColorButton("#1F2D3D", "Element colour")
        self.btn_text_color.colorChanged.connect(lambda _: self._store_style_target())
        self.spin_element_size = QSpinBox(); self.spin_element_size.setRange(0, 40); self.spin_element_size.setSpecialValueText("auto")
        self.spin_element_size.valueChanged.connect(self._store_style_target)
        f.addRow("Target element:", self.cb_target_element)
        f.addRow("Text:", self.txt_element_value)
        f.addRow("Colour:", self.btn_text_color)
        f.addRow("Font size:", self.spin_element_size)
        self.slider_fontsize = QSlider(Qt.Horizontal); self.slider_fontsize.setRange(6, 24); self.slider_fontsize.setValue(10)
        self.slider_fontsize.valueChanged.connect(self._queue_render_if_slider_idle); self.slider_fontsize.sliderReleased.connect(self.queue_render)
        f.addRow("Base font size:", self.slider_fontsize)
        self.slider_padding = QSlider(Qt.Horizontal); self.slider_padding.setRange(2, 30); self.slider_padding.setValue(8)
        self.slider_padding.valueChanged.connect(self._queue_render_if_slider_idle); self.slider_padding.sliderReleased.connect(self.queue_render)
        f.addRow("Label padding:", self.slider_padding)
        self.chk_bg_grid = QCheckBox("Show background grid"); self.chk_bg_grid.setChecked(True)
        self.chk_bg_grid.toggled.connect(self.queue_render)
        self.chk_legend = QCheckBox("Show legend (draggable)"); self.chk_legend.setChecked(True)
        self.chk_legend.toggled.connect(self.queue_render)
        f.addRow("", self.chk_bg_grid)
        f.addRow("", self.chk_legend)
        b = QPushButton("Reset all element colours / text")
        b.clicked.connect(self._reset_styling)
        f.addRow("", b)
        self._load_style_target()
        return g

    def _group_alias_manager(self):
        g = QGroupBox("Variable alias manager")
        v = QVBoxLayout(g)
        b = QPushButton("⚙ Configure variable aliases (JSON)")
        b.clicked.connect(self.open_alias_editor)
        v.addWidget(b)
        return g

    def _group_actions(self):
        g = QGroupBox("Actions")
        v = QVBoxLayout(g)
        # Rendering is automatic in GraphVis 16; Ctrl+R remains as a manual
        # refresh shortcut but the explicit button is retired.
        b1 = QPushButton("Generate / preview   [Ctrl+R]")
        b1.clicked.connect(self.generate_preview)
        b1.setVisible(False)
        b2 = QPushButton("Save graph to new tab   [Ctrl+S]")
        b2.clicked.connect(self.save_graph_to_tab)
        b3 = QPushButton("Smart map suite")
        b3.clicked.connect(self.auto_generate_smart_suite)
        b4 = QPushButton("Intelligent visualisation advisor")
        b4.clicked.connect(self.run_intelligent_advisor)
        b5 = QPushButton("Reset all controls to defaults")
        b5.setToolTip("Restore every mapping, scale, styling, limit and clipping control to its pristine startup "
                      "state. The action is undoable (Ctrl+Z).")
        b5.clicked.connect(self.reset_all_controls)
        for b in (b1, b2, b3, b4, b5):
            b.setMinimumHeight(28); b.setMaximumHeight(28)
            b.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
            v.addWidget(b)
        return g

    # ------------------------------------------------------------ styling
    def _load_style_target(self):
        target = self.cb_target_element.currentText()
        st = self.styling.get(target, DEFAULT_STYLING.get(target, {}))
        for w in (self.txt_element_value, self.btn_text_color, self.spin_element_size):
            w.blockSignals(True)
        self.txt_element_value.setText(st.get("text", "") or "")
        self.btn_text_color.setColor(st.get("color", "#1F2D3D"), emit=False)
        self.spin_element_size.setValue(int(st.get("size") or 0))
        for w in (self.txt_element_value, self.btn_text_color, self.spin_element_size):
            w.blockSignals(False)

    def _store_style_target(self, *_):
        target = self.cb_target_element.currentText()
        self.styling[target] = {"text": self.txt_element_value.text().strip(), "color": self.btn_text_color.color(),
                                "size": self.spin_element_size.value() or None}
        self.queue_render()

    def _reset_styling(self):
        self.styling = copy.deepcopy(DEFAULT_STYLING)
        self._load_style_target()
        self.queue_render()

    # ------------------------------------------------------------ modes
    def set_ui_mode(self, advanced: bool):
        self.btn_mode_simple.setChecked(not advanced)
        self.btn_mode_advanced.setChecked(advanced)
        self.advanced_container.setVisible(advanced)
        if hasattr(self, "picker"):
            # The catalogue itself always exposes the complete 200+ library.
            # Simple/Advanced View now controls editing/analysis complexity, not
            # whether a graph type exists in search. Categories start collapsed.
            self.picker.setAdvanced(True)
        self.settings.setValue("ui/advanced", advanced)
        for i in range(self.tabs.count()):
            w = self.tabs.widget(i)
            if isinstance(w, ScientificPlotCanvas):
                w.set_drawer_visible(advanced)

    def set_diagnostics_visible(self, on: bool):
        self.debug_container.setVisible(on)
        if on:
            self.debug_drawer.reload()
        self.act_diag.blockSignals(True); self.act_diag.setChecked(on); self.act_diag.blockSignals(False)
        self.settings.setValue("ui/show_diagnostics", on)

    def reset_all_controls(self):
        """Return every sidebar/canvas control to the pristine startup state."""
        if not self._default_ui_state:
            self.statusBar().showMessage("No default state captured for this session.", 4000)
            return
        self.history.record(self.snapshot_ui_state(), "Before reset")   # make the reset undoable
        state = copy.deepcopy(self._default_ui_state)
        state["selected"] = [i.text() for i in self.list_datasets.selectedItems()]   # keep the dataset selection
        self.apply_ui_state(state)
        if hasattr(self, "btn_matlab_surface"):
            self.btn_matlab_surface.setChecked(False)
            self.btn_matlab_surface.setText("MATLAB-style surface preset")
            self._matlab_preset_backup = None
        self.statusBar().showMessage("All controls reset to defaults (Ctrl+Z to undo).", 5000)

    def save_workspace_snapshot_file(self):
        """Serialize the full workspace to a portable JSON file.

        Captures the complete UI state, every open graph tab's spec, dataset
        source paths, literature profile references and a bounded undo trail,
        so a session can be restored seamlessly on any machine with the data.
        """
        path, _ = QFileDialog.getSaveFileName(self, "Save workspace snapshot",
                                              str(self.project.root / "workspace_snapshot.json"),
                                              "GraphVis workspace (*.json)")
        if not path:
            return
        try:
            tabs = []
            for i in range(self.tabs.count()):
                w = self.tabs.widget(i)
                if isinstance(w, ScientificPlotCanvas):
                    tabs.append({"title": self.tabs.tabText(i), "spec": w.spec.to_dict(),
                                 "active": w is self.tabs.currentWidget()})
            undo_trail = []
            if self._last_good_ui_state:
                undo_trail.append(copy.deepcopy(self._last_good_ui_state))
            payload = {
                "format": "graphvis-workspace",
                "version": 2,
                "saved_utc": __import__("datetime").datetime.now(__import__("datetime").timezone.utc).isoformat(),
                "ui_state": self.snapshot_ui_state(),
                "tabs": tabs,
                "datasets": {name: getattr(ds, "path", "") for name, ds in self.datasets.items()},
                "literature": [getattr(ext, "path", "") for ext in (self.literature or [])],
                "active_group": getattr(self.project_context.get_group(), "name", None)
                                if hasattr(self, "project_context") else None,
                "advanced_view": self.btn_mode_advanced.isChecked(),
                "undo_trail": undo_trail,
            }
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(payload, fh, indent=2, default=str)
            self.statusBar().showMessage(f"Workspace snapshot saved: {path}", 6000)
        except Exception as exc:
            QMessageBox.warning(self, "Save workspace snapshot", str(exc))

    def restore_workspace_snapshot_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "Restore workspace snapshot",
                                              str(self.project.root), "GraphVis workspace (*.json)")
        if not path:
            return
        try:
            with open(path, encoding="utf-8") as fh:
                payload = json.load(fh)
            if payload.get("format") != "graphvis-workspace":
                raise ValueError("Not a GraphVis workspace snapshot file.")
            missing = []
            for name, src in (payload.get("datasets") or {}).items():
                if name in self.datasets:
                    continue
                if src and os.path.exists(src):
                    try:
                        from graphvis.data.loader import load_dataset
                        ds = load_dataset(src); ds.name = name
                        self._register_dataset(ds)
                    except Exception as exc:
                        missing.append(f"{name} ({exc})")
                else:
                    missing.append(name)
            self.set_ui_mode(advanced=bool(payload.get("advanced_view", False)))
            for trail_state in payload.get("undo_trail") or []:
                self.history.record(copy.deepcopy(trail_state), "Snapshot history")
            self.apply_ui_state(payload.get("ui_state") or {})
            restored_tabs = 0
            for tab in payload.get("tabs") or []:
                try:
                    spec = PlotSpec.from_dict(tab.get("spec") or {}, registry=self.datasets)
                    if tab.get("active") and hasattr(self, "preview_canvas"):
                        continue   # the active view is rebuilt by apply_ui_state
                    self._add_tab(spec, tab.get("title") or spec.chart_type, autorender=False)
                    restored_tabs += 1
                except Exception as exc:
                    log_line(f"Snapshot tab skipped: {exc}", "WORKSPACE")
            msg = f"Workspace restored — {restored_tabs} extra tab(s)."
            if missing:
                msg += f" Missing datasets: {', '.join(missing[:4])}"
            self.statusBar().showMessage(msg, 8000)
        except Exception as exc:
            QMessageBox.warning(self, "Restore workspace snapshot", str(exc))

    def open_simulation_bridge(self):
        """Universal engine-agnostic simulation runner with direct ingestion.

        Executes an external computational model (Python, ANSYS MAPDL, COMSOL,
        AQUASIM or any CLI engine), intercepts the files the run produced —
        parameter-sweep matrices, time series, grids, point clouds — and
        registers them as ordinary GraphVis datasets, instantly bound to the
        mapping dropdowns, axis sliders, clipping tools and 3-D renderer.
        """
        from graphvis.automation.sim_bridge import ENGINE_PRESETS, MatlabEngineTask, SimulationRunTask

        dlg = QDialog(self)
        dlg.setWindowTitle("External simulation bridge")
        dlg.setMinimumWidth(640)
        form = QFormLayout(dlg)
        cb_engine = QComboBox(); cb_engine.addItems(list(ENGINE_PRESETS))
        form.addRow("Engine:", cb_engine)
        txt_cmd = QLineEdit(ENGINE_PRESETS["Python script"]["command"])
        txt_cmd.setToolTip("Command template. {script} and {workdir} are substituted before execution.")
        form.addRow("Command template:", txt_cmd)
        lbl_hint = QLabel(ENGINE_PRESETS["Python script"]["hint"]); lbl_hint.setWordWrap(True)
        lbl_hint.setStyleSheet("font-size:10px;color:#7F8C8D;")
        form.addRow("", lbl_hint)
        script_row = QHBoxLayout(); txt_script = QLineEdit(); txt_script.setPlaceholderText("Model / input / script file")
        b_script = QPushButton("Browse…")
        b_script.clicked.connect(lambda: txt_script.setText(
            QFileDialog.getOpenFileName(dlg, "Select model/script", "", "All files (*)")[0] or txt_script.text()))
        script_row.addWidget(txt_script, 1); script_row.addWidget(b_script)
        form.addRow("Input file:", script_row)
        wd_row = QHBoxLayout(); txt_wd = QLineEdit(str(self.project.imports_dir if hasattr(self.project, "imports_dir") else os.getcwd()))
        b_wd = QPushButton("Browse…")
        b_wd.clicked.connect(lambda: txt_wd.setText(
            QFileDialog.getExistingDirectory(dlg, "Working directory", txt_wd.text()) or txt_wd.text()))
        wd_row.addWidget(txt_wd, 1); wd_row.addWidget(b_wd)
        form.addRow("Working directory:", wd_row)
        txt_outputs = QLineEdit(ENGINE_PRESETS["Python script"]["outputs"])
        txt_outputs.setToolTip("Semicolon-separated glob patterns; files matching these that the run creates/updates are ingested.")
        form.addRow("Output patterns:", txt_outputs)
        spin_timeout = QSpinBox(); spin_timeout.setRange(1, 1440); spin_timeout.setValue(60); spin_timeout.setSuffix(" min")
        form.addRow("Time limit:", spin_timeout)

        def _engine_changed(name: str) -> None:
            preset = ENGINE_PRESETS.get(name, {})
            txt_cmd.setText(preset.get("command", txt_cmd.text()))
            txt_outputs.setText(preset.get("outputs", txt_outputs.text()))
            lbl_hint.setText(preset.get("hint", ""))
        cb_engine.currentTextChanged.connect(_engine_changed)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.button(QDialogButtonBox.Ok).setText("Run && ingest")
        buttons.accepted.connect(dlg.accept); buttons.rejected.connect(dlg.reject)
        form.addRow(buttons)
        if not dlg.exec():
            return
        if ENGINE_PRESETS.get(cb_engine.currentText(), {}).get("api") == "matlab":
            task = MatlabEngineTask(txt_script.text().strip(), txt_wd.text().strip())
        else:
            task = SimulationRunTask(txt_cmd.text().strip(), txt_wd.text().strip(), txt_script.text().strip(),
                                     txt_outputs.text().strip(), timeout_seconds=spin_timeout.value() * 60.0)
        overlay = self._operation_overlay()
        overlay.start("Running external simulation…", cancellable=True, immediate=True)
        overlay.cancel_requested.connect(task.cancel)
        task.signals.progress.connect(lambda msg, pct: overlay.set_message(msg, pct))
        task.signals.finished.connect(self._on_simulation_bridge_done)
        task.signals.failed.connect(lambda msg: (overlay.stop(),
                                                 QMessageBox.warning(self, "Simulation bridge", msg.splitlines()[0])))
        task.signals.cancelled.connect(overlay.stop)
        self._sim_bridge_task = task
        submit(task)

    def _on_simulation_bridge_done(self, result: dict):
        self._operation_overlay().stop()
        datasets = result.get("datasets") or []
        registered = []
        for ds in datasets:
            try:
                base = ds.name; n = 1
                while ds.name in self.datasets:
                    n += 1; ds.name = f"{base}_{n}"
                self._register_dataset(ds)
                registered.append(ds.name)
            except Exception as exc:
                log_line(f"Simulation output dataset skipped: {exc}", "SIMBRIDGE")
        if registered:
            self.populate_axes(self.primary_dataset())
        summary = (f"Engine exit code: {result.get('returncode')}   ({result.get('seconds', 0)} s)\n"
                   f"Files ingested: {len(result.get('files') or [])}\n"
                   f"Datasets registered: {', '.join(registered) if registered else 'none'}")
        errors = result.get("errors") or []
        if errors:
            summary += "\n\nParse issues:\n- " + "\n- ".join(errors[:6])
        tail = (result.get("stdout_tail") or "").strip()
        if tail:
            summary += "\n\nEngine output (tail):\n" + tail[-1200:]
        QMessageBox.information(self, "Simulation bridge complete", summary)
        record_activity("Simulation bridge run", f"datasets={len(registered)}; code={result.get('returncode')}")

    def open_settings(self):
        dlg = SettingsDialog(self)
        if dlg.exec():
            dlg.save()
            self.watchdog.set_enabled(self.settings.value("watchdog/enabled", True, bool))
            self.watchdog.set_threshold(self.settings.value("watchdog/threshold", 3.0, float))
            self.set_diagnostics_visible(self.settings.value("ui/show_diagnostics", False, bool))
            self._apply_dropdown_mode_ui()

    def _apply_dropdown_mode_ui(self) -> None:
        """Show/hide every ▶ Apply button to match the dropdown apply mode."""
        manual = manual_apply_mode()
        for combo in list(self._staged_combos):
            button = getattr(combo, "_graphvis_apply_button", None)
            if button is not None:
                button.setVisible(manual)
        if hasattr(self, "btn_apply_all_controls"):
            self.btn_apply_all_controls.setVisible(manual)
        if hasattr(self, "btn_apply_graph_choice"):
            self.btn_apply_graph_choice.setVisible(manual)
        for i in range(self.tabs.count()):
            w = self.tabs.widget(i)
            if isinstance(w, ScientificPlotCanvas):
                w.set_manual_apply(manual)
        if hasattr(self, "preview_canvas"):
            self.preview_canvas.set_manual_apply(manual)

    def open_instructions(self, query: str = ""):
        record_activity("Opened instructions", f"query={query}")
        dlg = InstructionsDialog(str(query or ""), self)
        dlg.exec()

    def _open_help_search(self):
        query = self.help_search.text().strip() if hasattr(self, "help_search") else ""
        self.open_instructions(query)

    def show_help(self):
        QMessageBox.information(self, "Shortcuts", (
            "Ctrl+R  generate preview\nCtrl+S  save graph to tab\nCtrl+E  export current tab\n"
            "Ctrl+Z / Ctrl+Shift+Z (Cmd on macOS)  undo / redo\nCtrl+B  collapse / expand sidebar\nCtrl+D  diagnostics drawer\n\n"
            "Touch: pinch to zoom, two-finger pan, one-finger drag pans 2-D and rotates 3-D plots.\n"
            "Mouse: wheel zooms at cursor, left-drag pans, right-click for callouts / export, drag the legend anywhere."))

    # ------------------------------------------------------------ project workspace
    def _switch_project(self, workspace):
        if self.controller is not None:
            self.controller.switch_project(workspace)
        else:
            self.app_model.switch_project(workspace)
        self.project = self.app_model.project
        self.project_context = ProjectContextStore(self.project)
        self.datasets = self.app_model.datasets
        self.dataset_dir = str(workspace.datasets_dir)
        try:
            if self.source_watcher.files():
                self.source_watcher.removePaths(self.source_watcher.files())
        except Exception:
            pass
        self._watch_source_map.clear()
        self._live_reload_pending.clear()
        self.datasets.clear()
        self.list_datasets.clear()
        self.literature = self.project_context.load_literature(); self.literature_overlays.clear()
        self._session_restored = False
        self._refresh_active_group_label()
        # close saved graph tabs while preserving live preview
        for i in range(self.tabs.count() - 1, 0, -1):
            w = self.tabs.widget(i); self.tabs.removeTab(i); w.deleteLater()
        self._refresh_display_aliases()
        self.setWindowTitle(f"GraphVis — {workspace.name}")
        self.statusBar().showMessage(f"Active project: {workspace.name}", 5000)
        self.auto_load_datasets_from_folder()

    def new_project(self):
        name, ok = QInputDialog.getText(self, "New GraphVis project", "Project name:")
        if not ok or not name.strip():
            return
        try:
            self._switch_project(create_project(name.strip()))
        except Exception as exc:
            QMessageBox.critical(self, "Project error", str(exc))

    def open_project_folder(self):
        path = QFileDialog.getExistingDirectory(self, "Open GraphVis project folder", str(getattr(self.project, 'root', APP_DIR)))
        if not path:
            return
        try:
            self._switch_project(open_project(path))
        except Exception as exc:
            QMessageBox.critical(self, "Project error", str(exc))

    def save_project_snapshot(self):
        name, ok = QInputDialog.getText(self, "Save comparison snapshot", "Snapshot name:")
        if not ok:
            return
        payload = {
            "ui": self.snapshot_ui_state(),
            "tabs": [{"title": self.tabs.tabText(i), "spec": self.tabs.widget(i).spec.to_dict()}
                     for i in range(self.tabs.count()) if isinstance(self.tabs.widget(i), ScientificPlotCanvas)],
            "datasets": {n: d.path for n, d in self.datasets.items()},
        }
        try:
            path = (self.controller.save_snapshot(name.strip() or "snapshot", payload)
                    if self.controller is not None else self.app_model.save_snapshot(name.strip() or "snapshot", payload))
            self.statusBar().showMessage(f"Snapshot saved: {os.path.basename(path)}", 5000)
        except Exception as exc:
            QMessageBox.critical(self, "Snapshot failed", str(exc))

    def restore_project_snapshot(self):
        snaps = self.controller.list_snapshots() if self.controller is not None else self.app_model.list_snapshots()
        if not snaps:
            QMessageBox.information(self, "Snapshots", "This project has no saved snapshots yet."); return
        labels = [s["name"] for s in snaps]
        label, ok = QInputDialog.getItem(self, "Restore comparison snapshot", "Snapshot:", labels, 0, False)
        if not ok:
            return
        rec = next(s for s in snaps if s["name"] == label)
        try:
            payload = self.controller.load_snapshot(rec["path"]) if self.controller is not None else self.app_model.load_snapshot(rec["path"])
            if payload.get("ui"):
                self.apply_ui_state(payload["ui"])
            for i in range(self.tabs.count()-1,0,-1):
                w=self.tabs.widget(i); self.tabs.removeTab(i); w.deleteLater()
            for tab in payload.get("tabs", [])[1:]:
                spec=PlotSpec.from_dict(tab.get("spec",{}), registry=self.datasets)
                if spec.datasets or spec.chart_type in EXPRESSION_CHARTS:
                    self._add_tab(spec, tab.get("title","Snapshot"))
            self.statusBar().showMessage(f"Restored snapshot: {label}", 5000)
        except Exception as exc:
            QMessageBox.critical(self, "Snapshot restore failed", str(exc))

    # ------------------------------------------------------------ context / metadata
    def open_code_context(self):
        dlg = QDialog(self); dlg.setWindowTitle("Code / variable context parser"); dlg.resize(760,520)
        lay=QVBoxLayout(dlg)
        lay.addWidget(QLabel("Paste Python/MATLAB variable definitions or comments. GraphVis will map short names to physical meanings and units."))
        edit=QPlainTextEdit(); edit.setPlaceholderText("T = 298.15  # Temperature [K]\nVapp = 0.8  % Applied voltage [V]\n...")
        edit.setPlainText(self.settings.value("variable_context/source", "", str)); lay.addWidget(edit,1)
        buttons=QDialogButtonBox(QDialogButtonBox.Ok|QDialogButtonBox.Cancel); buttons.accepted.connect(dlg.accept); buttons.rejected.connect(dlg.reject); lay.addWidget(buttons)
        if not dlg.exec(): return
        source=edit.toPlainText(); parsed=parse_variable_context(source)
        self.variable_context=parsed
        aliases={k: (f"{v.get('name',k)} [{v.get('unit')}]" if v.get('unit') else v.get('name',k)) for k,v in parsed.items()}
        set_context_aliases(aliases)
        self.settings.setValue("variable_context/source", source)
        for ds in self.datasets.values():
            ds.meta["variable_context"]=copy.deepcopy(parsed)
            for raw,rec in parsed.items():
                if raw in ds.df.columns and rec.get('unit'):
                    ds.units[raw]=rec['unit']
        self.populate_axes(self.primary_dataset())
        self.queue_render()
        self.statusBar().showMessage(f"Parsed {len(parsed)} variable definition(s)", 5000)

    def _axis_unit_menu(self, axis_key: str, combo: QComboBox, global_pos):
        ds=self.primary_dataset(); col=combo.currentData()
        if ds is None or col not in ds.df.columns:
            return
        unit=ds.units.get(col) or extract_unit(get_pretty_label(col)) or extract_unit(str(col))
        convs=conversions_for(unit)
        menu=QMenu(self)
        title=menu.addAction(f"{get_pretty_label(col)}   [{unit or 'unit unknown'}]"); title.setEnabled(False)
        if not convs:
            a=menu.addAction("No built-in conversions for this unit"); a.setEnabled(False)
        else:
            for conv in convs:
                a=menu.addAction(f"Convert {conv.source} → {conv.target}")
                a.triggered.connect(lambda _=False,c=conv,k=axis_key: self._apply_unit_conversion(k,c))
        menu.exec(global_pos)

    def _apply_unit_conversion(self, axis_key, conversion):
        combo={'x':self.cb_x,'y':self.cb_y,'z':self.cb_z,'w':self.cb_w,'v':self.cb_v,'y2':self.cb_y2}.get(axis_key)
        if combo is None: return
        raw=combo.currentData()
        if not raw: return
        new_name=f"{raw} [{conversion.target}]"
        records=[]

        def model_add(ds: Dataset, values, unit: str | None) -> None:
            if self.controller is not None:
                self.controller.add_derived_series(ds.name, new_name, values, unit)
            else:
                self.app_model.add_derived_series(ds.name, new_name, values, unit)

        def model_remove(ds: Dataset) -> None:
            if self.controller is not None:
                self.controller.remove_series(ds.name, new_name)
            else:
                self.app_model.remove_series(ds.name, new_name)

        for ds in self.selected_datasets().values():
            if raw not in ds.df.columns: continue
            previous = ds.df[new_name].copy(deep=True) if new_name in ds.df.columns else None
            prev_unit = ds.units.get(new_name)
            values = conversion.apply(pd.to_numeric(ds.df[raw], errors='coerce'))
            records.append((ds, previous, prev_unit, values.copy(deep=True) if hasattr(values,'copy') else values))
            model_add(ds, values, conversion.target)
        if not records:
            return

        def refresh(prefer=True):
            base=get_pretty_label(raw)
            aliases={k:(f"{v.get('name',k)} [{v.get('unit')}]" if v.get('unit') else v.get('name',k)) for k,v in self.variable_context.items()}
            if any(new_name in d.df.columns for d, *_ in records):
                aliases[new_name]=replace_unit(base, conversion.target)
            set_context_aliases(aliases)
            self.populate_axes(self.primary_dataset())
            if prefer and any(new_name in d.df.columns for d, *_ in records): self._combo_set_data(combo,new_name)
            self.queue_render()

        def redo():
            for ds, _previous, _prev_unit, values in records:
                model_add(ds, values.copy(deep=True) if hasattr(values,'copy') else values, conversion.target)
            refresh(True)

        def undo():
            for ds, previous, prev_unit, _values in records:
                if previous is None:
                    model_remove(ds)
                else:
                    model_add(ds, previous, prev_unit)
                    if prev_unit is None:
                        ds.units.pop(new_name, None)
            refresh(False)

        refresh(True)
        self.history.push_applied(redo, undo, f"Convert units {raw}: {conversion.source} to {conversion.target}")
        self.statusBar().showMessage(f"Converted {raw} → {new_name} for {len(records)} dataset(s)",5000)

    # ------------------------------------------------------------ optical digitizer
    def open_digitizer(self):
        from graphvis.literature.digitizer import FigureDigitizerDialog
        path,_=QFileDialog.getOpenFileName(self,"Digitize raster graph","","Images/PDF (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.pdf)")
        if not path: return
        dlg=FigureDigitizerDialog(path,self)
        if not dlg.exec(): return
        df=dlg.dataframe()
        name=f"digitized_{os.path.splitext(os.path.basename(path))[0]}.csv"
        out=os.path.join(str(self.project.digitized_dir),name)
        df.to_csv(out,index=False)
        # copy into datasets so normal project startup re-loads it automatically
        proj_path=self.project.ingest_file(out,"datasets")
        ds=Dataset(os.path.basename(proj_path),proj_path,df,meta={'source_image':path,'kind':'digitized','assumed':False})
        self.on_dataset_loaded(ds)
        self.statusBar().showMessage(f"Digitized {len(df):,} points → {proj_path}",6000)

    # ------------------------------------------------------------ analysis tools
    def open_peak_analysis(self):
        from graphvis.analysis.tools import polynomial_baseline, asymmetric_least_squares, fit_overlapping_peaks, fit_nonlinear_model
        ds=self.primary_dataset()
        if ds is None or len(ds.numeric_columns)<2:
            QMessageBox.warning(self,"Peak analysis","Select a dataset with at least two numeric columns."); return
        dlg=QDialog(self); dlg.setWindowTitle("Peak analysis & nonlinear fitting"); dlg.resize(560,420)
        form=QFormLayout(dlg)
        cbx=QComboBox(); cby=QComboBox(); cbx.addItems(ds.numeric_columns); cby.addItems(ds.numeric_columns)
        if len(ds.numeric_columns)>1: cby.setCurrentIndex(1)
        mode=QComboBox(); mode.addItems(["Polynomial baseline correction","Asymmetric baseline correction","Multi-peak Gaussian fit","Multi-peak Lorentzian fit","Multi-peak Pseudo-Voigt fit","Nonlinear: Exponential saturation","Nonlinear: Exponential decay","Nonlinear: Michaelis-Menten","Nonlinear: Logistic"])
        npeaks=QSpinBox(); npeaks.setRange(1,12); npeaks.setValue(3)
        order=QSpinBox(); order.setRange(0,6); order.setValue(2)
        form.addRow("X:",cbx);form.addRow("Y:",cby);form.addRow("Operation:",mode);form.addRow("Peak count:",npeaks);form.addRow("Baseline polynomial order:",order)
        buttons=QDialogButtonBox(QDialogButtonBox.Ok|QDialogButtonBox.Cancel);buttons.accepted.connect(dlg.accept);buttons.rejected.connect(dlg.reject);form.addRow(buttons)
        if not dlg.exec(): return
        xcol,ycol=cbx.currentText(),cby.currentText(); sub=ds.df[[xcol,ycol]].replace([np.inf,-np.inf],np.nan).dropna()
        try:
            op=mode.currentText(); x=sub[xcol].to_numpy(float); y=sub[ycol].to_numpy(float)
            if op.startswith("Polynomial"):
                xx,yy,base,corr,coef=polynomial_baseline(x,y,order.value()); fit=base; derived=corr; suffix="baseline_corrected"; summary=f"Polynomial baseline order {order.value()}: coefficients {coef}"
            elif op.startswith("Asymmetric"):
                idx=np.argsort(x);xx=x[idx];yy=y[idx];fit=asymmetric_least_squares(yy);derived=yy-fit;suffix="baseline_corrected";summary="Asymmetric least-squares baseline correction"
            elif op.startswith("Multi-peak"):
                kind="Gaussian" if "Gaussian" in op else ("Lorentzian" if "Lorentzian" in op else "Pseudo-Voigt")
                res=fit_overlapping_peaks(x,y,npeaks.value(),kind);xx,yy,fit,derived=res.x,res.y,res.fitted,res.residual;suffix=f"{kind.lower()}_residual";summary=f"{kind} {npeaks.value()}-peak fit, R²={res.r2:.5f}\n"+"\n".join(f"{k}: {v:.6g}" for k,v in res.params.items())
            else:
                model=op.split(":",1)[1].strip();res=fit_nonlinear_model(x,y,model);xx,yy,fit,derived=res.x,res.y,res.fitted,res.residual;suffix="fit_residual";summary=f"{model}, R²={res.r2:.5f}\n"+"\n".join(f"{k}: {v:.6g}" for k,v in res.params.items())
            out=pd.DataFrame({xcol:xx,ycol:yy,f"{ycol} fit":fit,f"{ycol} {suffix}":derived})
            out_name=f"{os.path.splitext(ds.name)[0]}_{suffix}.csv"; path=os.path.join(self.dataset_dir,out_name); out.to_csv(path,index=False)
            new_ds=Dataset(out_name,path,out,meta={'kind':'analysis-derived','parent':ds.path,'operation':op})
            self.on_dataset_loaded(new_ds)
            def redo_analysis():
                if not os.path.exists(path):
                    out.to_csv(path, index=False)
                self._commit_dataset_to_model(new_ds); self._register_dataset(new_ds); self.populate_axes(self.primary_dataset()); self.queue_render()
            def undo_analysis():
                self._unregister_dataset(new_ds.name)
                try:
                    if os.path.isfile(path):
                        os.remove(path)
                except OSError as exc:
                    log_line(f"Could not remove undo-derived dataset {path}: {exc}", "UNDO")
            self.history.push_applied(redo_analysis, undo_analysis, f"{op} — {ds.name}")
            QMessageBox.information(self,"Analysis complete",summary)
        except Exception as exc:
            QMessageBox.critical(self,"Analysis failed",str(exc))

    # ------------------------------------------------------------ templates / code export
    def save_graph_template(self):
        name,ok=QInputDialog.getText(self,"Save graph template","Template name:")
        if not ok or not name.strip(): return
        spec=self.current_canvas().spec.to_dict(); spec.pop('datasets',None)
        try:
            path=(self.controller.save_template(name.strip(), spec) if self.controller is not None
                  else self.app_model.save_template(name.strip(), spec))
            self.statusBar().showMessage(f"Template saved: {os.path.basename(path)}",5000)
        except Exception as exc: QMessageBox.critical(self,"Template failed",str(exc))

    def apply_graph_template(self):
        items=self.controller.list_templates() if self.controller is not None else self.app_model.list_templates()
        if not items: QMessageBox.information(self,"Templates","No templates saved in this project."); return
        label,ok=QInputDialog.getItem(self,"Apply graph template","Template:",[x['name'] for x in items],0,False)
        if not ok: return
        rec=next(x for x in items if x['name']==label)
        try:
            payload=self.controller.load_template(rec['path']) if self.controller is not None else self.app_model.load_template(rec['path'])
            spec=PlotSpec.from_dict({**payload,'datasets':list(self.selected_datasets().keys())},registry=self.datasets)
            # preserve current mapping if a template mapping cannot be resolved in incoming data
            current=self.build_spec()
            for k,v in list(spec.mappings.items()):
                if v and not any(v in d.df.columns for d in spec.datasets.values()): spec.mappings[k]=current.mappings.get(k)
            self._ensure_preview_tab().update_spec(spec)
            self.statusBar().showMessage(f"Applied template: {label}",5000)
        except Exception as exc: QMessageBox.critical(self,"Template failed",str(exc))

    def export_plot_code(self, language: str):
        ext='py' if language=='python' else 'm'
        path,_=QFileDialog.getSaveFileName(self,f"Export {language.title()} reproduction script",os.path.join(str(self.project.exports_dir),f"graphvis_plot.{ext}"),f"*.{ext}")
        if not path:return
        try:
            spec=self.current_canvas().spec.clone(); spec.view_state=self.current_canvas().capture_view_state()
            files=export_python(spec,path) if language=='python' else export_matlab(spec,path)
            self.statusBar().showMessage(f"Exported reproducibility script + {len(files)-1} data file(s)",6000)
        except Exception as exc: QMessageBox.critical(self,"Code export failed",str(exc))

    def clear_caches(self):
        SURFACE_CACHE.clear(); PARETO_CACHE.clear(); clear_figure_cache()
        self.statusBar().showMessage("Render caches cleared", 3000)

    def clear_smart_scan_cache(self) -> None:
        """Delete persisted deep-scan results/checkpoints for the active project."""
        answer = QMessageBox.question(
            self, "Clear Smart Suite saved scans",
            "Delete cached Smart Suite results and resumable scan checkpoints for this project?\n\n"
            "Literature text/context and datasets will not be deleted.",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No,
        )
        if answer != QMessageBox.Yes:
            return
        removed = 0
        try:
            for path in Path(self.scan_cache_dir).glob("*.json"):
                try:
                    path.unlink(); removed += 1
                except Exception as exc:
                    log_line(f"Could not remove Smart Suite cache {path}: {exc}", "CACHE")
            self._scan_results.clear()
            self._refresh_recommendations()
            self.statusBar().showMessage(f"Cleared {removed} Smart Suite saved scan file(s)", 5000)
        except Exception as exc:
            QMessageBox.warning(self, "Clear Smart Suite scans", str(exc))

    # ------------------------------------------------------------ axes
    @staticmethod
    def _combo_set_data(cb: QComboBox, data):
        idx = cb.findData(data)
        if idx >= 0:
            cb.setCurrentIndex(idx)
            return True
        return False

    def _ui_variable_label(self, raw) -> str:
        raw_text = str(raw)
        nickname = self.app_model.nicknames.get(raw_text, "").strip() if hasattr(self, "app_model") else ""
        if nickname:
            return f"{nickname} ({raw_text})"
        main, short, unit = infer_display(raw_text, self.variable_context)
        if unit and f"[{unit}]" not in main:
            main = f"{main} [{unit}]"
        return f"{main} ({short})" if short and short != main else main

    def _fill_combo(self, cb: QComboBox, ds: Dataset | None, allow_none: str | None, pin_matrices=False, include_aux=False):
        current = cb.currentData()
        cb.blockSignals(True)
        cb.clear()
        if allow_none:
            cb.addItem(allow_none, userData=None)
        cols = ds.numeric_columns if ds else []
        canon = ds.canonical if ds else {}
        pinned = set()
        for key in PRIORITY_KEYS:
            actual = canon.get(key)
            if actual and not str(actual).startswith("matrix:") and actual in cols:
                cb.addItem(f"★ {key} — {self._ui_variable_label(actual)}", userData=actual)
                pinned.add(actual)
            elif pin_matrices and actual and str(actual).startswith("matrix:"):
                cb.addItem(f"★ {key} — {actual[7:]}", userData=actual[7:])
                pinned.add(actual[7:])
            elif ds is not None:
                # Canonical thesis variables (vhpr, sCOD, VFA, applied_voltage)
                # stay visible in every dropdown even when a dataset lacks
                # them.  The entry is disabled so it can never be selected and
                # corrupt a mapping — it documents what the dataset is missing.
                cb.addItem(f"★ {key} — not present in this dataset", userData=None)
                try:
                    item = cb.model().item(cb.count() - 1)
                    if item is not None:
                        item.setEnabled(False)
                except Exception:
                    pass
        if pin_matrices and ds:
            for m in ds.matrices:
                if m not in pinned:
                    cb.addItem(f"{m}  [matrix {ds.matrices[m].shape}]", userData=m)
            for m, arr in getattr(ds, "volumes", {}).items():
                cb.addItem(f"{m}  [volume {arr.shape}]", userData=f"volume:{m}")
            for m, arr in getattr(ds, "tensors", {}).items():
                cb.addItem(f"{m}  [tensor {arr.shape}]", userData=f"tensor:{m}")
        else:
            for c in cols:
                if c not in pinned:
                    cb.addItem(self._ui_variable_label(c), userData=c)
            if include_aux and ds:
                for name, arr0 in getattr(ds, "aux", {}).items():
                    try:
                        arr = np.asarray(arr0)
                    except Exception:
                        continue
                    if arr.ndim == 1 and arr.size >= 2 and np.issubdtype(arr.dtype, np.number):
                        cb.addItem(f"{self._ui_variable_label(name)}  [matrix axis {arr.size}]", userData=f"aux:{name}")
                        cb.setItemData(cb.count() - 1, "1-D auxiliary vector available as an axis for pre-gridded surface matrices.", Qt.ToolTipRole)
        cb.blockSignals(False)
        if current is not None and self._combo_set_data(cb, current):
            return
        # default to the first enabled real column
        for i in range(cb.count()):
            if cb.itemData(i) is not None and cb.model().item(i).isEnabled():
                cb.blockSignals(True); cb.setCurrentIndex(i); cb.blockSignals(False)
                return
        cb.blockSignals(True); cb.setCurrentIndex(0); cb.blockSignals(False)

    def populate_axes(self, ds: Dataset | None):
        # Remember what the user had mapped before refilling, so re-population
        # (dataset refresh, chart change, state restore) never silently
        # discards a still-valid choice.  Only selections that no longer exist
        # in the dataset fall back to the heuristic suggestion.
        previous = {cb: cb.currentData() for cb in
                    (self.cb_x, self.cb_y, self.cb_z, self.cb_gradient)}
        self._fill_combo(self.cb_x, ds, None, include_aux=True)
        self._fill_combo(self.cb_y, ds, None, include_aux=True)
        self._fill_combo(self.cb_z, ds, None)
        self._fill_combo(self.cb_w, ds, "(none)")
        self._fill_combo(self.cb_v, ds, "(none)")
        self._fill_combo(self.cb_y2, ds, "(none)")
        self._fill_combo(self.cb_matrix, ds, "(auto-detect)", pin_matrices=True)
        self._fill_combo(self.cb_gradient, ds, "(same as Z)")
        if ds:
            suggestion = suggest_axis_mapping(ds)
            for key, cb in (("x", self.cb_x), ("y", self.cb_y), ("z", self.cb_z)):
                survived = previous.get(cb) is not None and cb.currentData() == previous.get(cb)
                if suggestion.get(key) and not survived:
                    self._combo_set_data(cb, suggestion[key])
            if not (previous.get(self.cb_gradient) is not None
                    and self.cb_gradient.currentData() == previous.get(self.cb_gradient)):
                self._combo_set_data(self.cb_gradient, None)
        self._apply_axis_roles()
        for cb in (self.cb_x, self.cb_y, self.cb_z, self.cb_w, self.cb_v, self.cb_y2, self.cb_matrix, self.cb_gradient):
            if cb in self._staged_combos:
                self._sync_staged_combo(cb)

    def apply_lhs_canonical_mapping(self):
        """Explicitly enforce the canonical LHS role assignment for this thesis."""
        ds = self.primary_dataset()
        if ds is None:
            self.statusBar().showMessage("Select a dataset first.", 4000)
            return
        from graphvis.analysis.lhs_pipeline import resolve_lhs_mapping
        mapping = resolve_lhs_mapping(ds.numeric_columns)
        found = {k: v for k, v in mapping.items() if v}
        if not any(found.get(r) for r in ("x", "y", "z")):
            self.statusBar().showMessage("No LHS sweep columns (lhs_flow / lhs_area / lhs_ea) found in this dataset.", 6000)
            return
        self._apply_mapping_dict(found)
        for combo in (self.cb_x, self.cb_y, self.cb_z, self.cb_w):
            if combo in self._staged_combos:
                self._sync_staged_combo(combo)
        self._apply_axis_roles()
        pretty = ", ".join(f"{k.upper()}={v}" for k, v in found.items())
        self.statusBar().showMessage(f"Canonical LHS mapping applied: {pretty}", 6000)
        self.queue_render()

    def auto_map_axes(self):
        ds = self.primary_dataset()
        if ds is None:
            return
        suggestion = suggest_axis_mapping(ds)
        for key, cb in (("x", self.cb_x), ("y", self.cb_y), ("z", self.cb_z)):
            if suggestion.get(key):
                self._combo_set_data(cb, suggestion[key])
        for cb in (self.cb_x, self.cb_y, self.cb_z):
            self._sync_staged_combo(cb)
        self.statusBar().showMessage("Suggested mapping: " + ", ".join(f"{k.upper()}={v}" for k, v in suggestion.items() if v), 5000)
        self.queue_render()

    def auto_render_current_chart(self) -> None:
        """Make the selected chart renderable, then render it immediately.

        Unlike Preview, this command is allowed to change mapping controls. It
        uses deep-scan recommendations first, then semantic heuristics, and
        finally distinct numeric columns. It never fabricates a missing 3-D
        volume or vector field because doing so would change the science.
        """
        chart = self.cb_chart.currentText()
        record_activity("Auto Render requested", f"chart={chart}")
        self._apply_axis_roles()

        if chart in EXPRESSION_CHARTS:
            if not self.txt_expression.text().strip():
                defaults = {
                    "Function Plot": ("sin(x)", "-10,10"),
                    "Function 3D Parametric": ("cos(t); sin(t); t/4", "0,25"),
                    "Implicit Function": ("x^2 + y^2 = 1", "-2,2; -2,2"),
                    "Implicit Surface": ("x^2 + y^2 + z^2 = 1", "-1.5,1.5; -1.5,1.5; -1.5,1.5"),
                    "Function Contour": ("sin(sqrt(x^2+y^2))", "-8,8; -8,8"),
                    "Function Surface": ("sin(sqrt(x^2+y^2))", "-8,8; -8,8"),
                    "Function Mesh": ("sin(sqrt(x^2+y^2))", "-8,8; -8,8"),
                }
                expression, domain = defaults.get(chart, ("sin(x)", "-10,10"))
                self.txt_expression.setText(expression); self.txt_expr_domain.setText(domain)
            self.generate_preview()
            self.statusBar().showMessage(f"Auto Render: generated {chart}", 4500)
            return

        ds = self.primary_dataset()
        if ds is None:
            QMessageBox.information(self, "Auto Render", "Load or select a dataset first. Function/implicit plots can render without a dataset.")
            return

        ok, why = chart_capabilities(ds).get(chart, (True, ""))
        if not ok:
            reason = why or "The active dataset does not contain the dimensionality/data type this graph requires."
            record_activity("Auto Render blocked", f"chart={chart}; reason={reason}")
            QMessageBox.information(
                self, "Auto Render - data requirement",
                f"{chart} cannot be generated from the current dataset yet.\n\n{reason}\n\n"
                "GraphVis will not invent missing scientific dimensions. Use Help > Instructions & graph guide for the exact mapping/data requirement."
            )
            return

        roles = axis_roles(chart)
        scan = self._scan_cache_for(ds)
        deep = best_mapping_for_graph(scan, chart) if scan else {}
        heuristic = suggest_axis_mapping(ds)
        combos = {"x": self.cb_x, "y": self.cb_y, "z": self.cb_z, "w": self.cb_w, "v": self.cb_v,
                  "matrix": self.cb_matrix, "gradient": self.cb_gradient}
        numeric = list(ds.numeric_columns)
        used: set[str] = set()
        mapping: dict[str, object] = {}

        # Geographic plots should never accidentally map arbitrary columns as
        # longitude/latitude when clearly named coordinates are present.
        lower_names = {str(c).lower(): c for c in ds.df.columns}
        lon = next((c for c in ds.numeric_columns if str(c).lower() in ("lon", "lng", "longitude") or "longitude" in str(c).lower()), None)
        lat = next((c for c in ds.numeric_columns if str(c).lower() in ("lat", "latitude") or "latitude" in str(c).lower()), None)
        special = {"x": lon, "y": lat} if chart.startswith("Geo ") and lon is not None and lat is not None else {}

        for key in ("x", "y", "z", "w", "v"):
            if roles.get(key) is None:
                continue
            candidates = [special.get(key), deep.get(key) if isinstance(deep, dict) else None,
                          heuristic.get(key), combos[key].currentData()]
            candidates.extend(numeric)
            chosen = None
            for candidate in candidates:
                if candidate is None or candidate not in numeric:
                    continue
                if candidate in used:
                    continue
                chosen = candidate
                break
            if chosen is not None:
                mapping[key] = chosen
                used.add(chosen)

        if roles.get("matrix") is not None:
            current = self.cb_matrix.currentData()
            if current is not None:
                mapping["matrix"] = current
            else:
                for i in range(self.cb_matrix.count()):
                    value = self.cb_matrix.itemData(i)
                    if value is not None:
                        mapping["matrix"] = value
                        break

        if roles.get("gradient") is not None:
            candidate = deep.get("gradient") if isinstance(deep, dict) else None
            if candidate not in numeric:
                candidate = mapping.get("z") or mapping.get("y")
            if candidate is not None:
                mapping["gradient"] = candidate

        self._apply_mapping_dict(mapping)
        entry = self.picker.currentEntry() if hasattr(self, "picker") else None
        preset_scale = entry.get("scale") if entry else None
        if not preset_scale:
            preset_scale = {"EIS: Bode": "Logarithmic Scale", "Power Spectral Density": "Semi-Log Y"}.get(chart)
        scales_customised = any(
            canonical_axis_scale(self._applied_combo_text(cb)) != "Linear"
            for cb in (self.cb_x_scale, self.cb_y_scale, self.cb_z_scale))
        if preset_scale and not scales_customised:
            self._apply_legacy_axis_scale(preset_scale)

        required_missing = []
        matrix_surface_ready = chart in SURFACE_FIELD_CHARTS and self.cb_matrix.currentData() is not None
        for key in ("x", "y", "z", "matrix"):
            role = roles.get(key)
            if role is None or "optional" in str(role).lower():
                continue
            if matrix_surface_ready and key in ("x", "y", "z"):
                continue
            if combos[key].currentData() is None:
                required_missing.append(f"{key.upper()}: {role}")
        if required_missing:
            reason = "\n".join(required_missing)
            record_activity("Auto Render missing mappings", f"chart={chart}; missing={reason}")
            QMessageBox.information(self, "Auto Render - mappings required",
                                    f"GraphVis could not fill every required mapping for {chart}:\n\n{reason}\n\n"
                                    "Choose matching data in Variable mapping or search this graph in the Instructions guide.")
            return

        axes = ", ".join(f"{k.upper()}={v}" for k, v in mapping.items() if v is not None)
        record_activity("Auto Render mapped chart", f"chart={chart}; {axes}")
        self.generate_preview()
        self.statusBar().showMessage(f"Auto Render: {chart} - {axes or 'automatic data'}", 6000)

    def _apply_axis_roles(self):
        chart = self.cb_chart.currentText()
        roles = axis_roles(chart)
        combos = {'x': self.cb_x, 'y': self.cb_y, 'z': self.cb_z, 'w': self.cb_w, 'v': self.cb_v,
                  'matrix': self.cb_matrix, 'gradient': self.cb_gradient}
        defaults = {'x': "X axis:", 'y': "Y axis:", 'z': "Z / response:", 'w': "4th axis (colour):",
                    'v': "5th axis (size/opacity):", 'matrix': "Profile matrix:", 'gradient': "Gradient field:"}
        for key, cb in combos.items():
            role = roles.get(key)
            on = role is not None
            cb.setEnabled(on)
            if key in ("x", "y"):
                allow_aux = chart in SURFACE_FIELD_CHARTS
                model = cb.model()
                for i in range(cb.count()):
                    if str(cb.itemData(i) or "").startswith("aux:"):
                        item = model.item(i)
                        if item is not None:
                            item.setEnabled(allow_aux)
            if key in ("x", "y", "z"):
                getattr(self, f"cb_{key}_scale").setEnabled(on)
            lbl = self.map_labels[key]
            lbl.setText((role + ":") if on else defaults[key])
            lbl.setEnabled(on)
            lbl.setToolTip("" if on else f"Not used by '{chart}'")
            if key in getattr(self, "clip_labels", {}):
                self.clip_labels[key].setEnabled(on)
        self.cb_fifth_mode.setEnabled(roles.get('v') is not None)
        self.lbl_fifth_mode.setEnabled(roles.get('v') is not None)
        y2_on = chart in ("Line Chart", "Stairs", "Error Bar", "Area", "Stacked Lines")
        self.cb_y2.setEnabled(y2_on)
        self.lbl_y2.setEnabled(y2_on)
        self.lbl_y2.setToolTip("Independent secondary Y axis" if y2_on else f"Not used by '{chart}'")
        self.expr_box.setVisible(chart in EXPRESSION_CHARTS)
        # grey out chart types the dataset can't support
        caps = chart_capabilities(self.primary_dataset())
        model = self.cb_chart.model()
        for i in range(self.cb_chart.count()):
            name = self.cb_chart.itemText(i)
            ok, why = caps.get(name, (True, ""))
            item = model.item(i)
            item.setEnabled(ok or name == chart)
            self.cb_chart.setItemData(i, why or CHART_DESCRIPTIONS.get(name, ""), Qt.ToolTipRole)

    def on_chart_changed(self):
        chart = self.cb_chart.currentText()
        self.lbl_chart_desc.setText(CHART_DESCRIPTIONS.get(chart, ""))
        if hasattr(self, "picker") and self.picker.currentEngine() != chart:
            self.picker.setCurrentEngine(chart)
        defaults = {
            "Function Plot": ("sin(x)", "-10,10"),
            "Function 3D Parametric": ("cos(t); sin(t); t/4", "0,25"),
            "Implicit Function": ("x^2 + y^2 = 1", "-2,2; -2,2"),
            "Implicit Surface": ("x^2 + y^2 + z^2 = 1", "-1.5,1.5; -1.5,1.5; -1.5,1.5"),
            "Function Contour": ("sin(sqrt(x^2+y^2))", "-8,8; -8,8"),
            "Function Surface": ("sin(sqrt(x^2+y^2))", "-8,8; -8,8"),
            "Function Mesh": ("sin(sqrt(x^2+y^2))", "-8,8; -8,8"),
        }
        if chart in defaults and getattr(self, '_last_expression_chart', None) != chart:
            expression, domain = defaults[chart]
            self.txt_expression.setText(expression)
            self.txt_expr_domain.setText(domain)
            self._last_expression_chart = chart
        self._apply_axis_roles()
        self.queue_render()

    def fit_axis_bounds(self, axis: str):
        ds = self.primary_dataset()
        if not ds or ds.df.empty:
            return
        combo, smin, smax = {'x': (self.cb_x, self.spin_xmin, self.spin_xmax), 'y': (self.cb_y, self.spin_ymin, self.spin_ymax),
                             'z': (self.cb_z, self.spin_zmin, self.spin_zmax), 'w': (self.cb_w, self.spin_wmin, self.spin_wmax),
                             'v': (self.cb_v, self.spin_vmin, self.spin_vmax)}[axis]
        col = combo.currentData()
        if col in ds.df.columns:
            v = ds.df[col].to_numpy(float)
            v = v[np.isfinite(v)]
            if v.size:
                smin.blockSignals(True); smax.blockSignals(True)
                smin.setValue(float(v.min())); smax.setValue(float(v.max()))
                smin.blockSignals(False); smax.blockSignals(False)
        self.queue_render()

    def fit_all_axes(self):
        for k in 'xyzwv':
            self.fit_axis_bounds(k)

    def robust_fit_all_axes(self):
        """Set clipping bounds to finite 1st–99th percentiles for extreme-scale data."""
        ds = self.primary_dataset()
        if not ds or ds.df.empty:
            return
        mapping = {
            'x': (self.cb_x, self.spin_xmin, self.spin_xmax),
            'y': (self.cb_y, self.spin_ymin, self.spin_ymax),
            'z': (self.cb_z, self.spin_zmin, self.spin_zmax),
            'w': (self.cb_w, self.spin_wmin, self.spin_wmax),
            'v': (self.cb_v, self.spin_vmin, self.spin_vmax),
        }
        changed = 0
        for _key, (combo, smin, smax) in mapping.items():
            col = combo.currentData()
            if col not in ds.df.columns:
                continue
            v = ds.df[col].to_numpy(float); v = v[np.isfinite(v)]
            if not v.size:
                continue
            lo, hi = np.percentile(v, [1, 99]) if v.size >= 20 else (float(v.min()), float(v.max()))
            if not np.isfinite(lo) or not np.isfinite(hi) or lo == hi:
                continue
            for spin, val in ((smin, float(lo)), (smax, float(hi))):
                spin.blockSignals(True); spin.setValue(val); spin.blockSignals(False)
            changed += 1
        if changed:
            self.chk_use_clip.setChecked(True)
            self.statusBar().showMessage(f"Robust clipping applied to {changed} mapped axes (1st–99th percentile).", 5000)
            self.queue_render()

    # ------------------------------------------------------------ datasets
    def set_live_telemetry(self, enabled: bool) -> None:
        self.live_telemetry_enabled = bool(enabled)
        if self.controller is not None:
            self.controller.set_live_telemetry(self.live_telemetry_enabled)
        else:
            self.app_model.live_telemetry_enabled = self.live_telemetry_enabled
        self.settings.setValue("telemetry/enabled", self.live_telemetry_enabled)
        self._sync_telemetry_watchers()
        self.statusBar().showMessage("Live Telemetry enabled" if enabled else "Live Telemetry disabled", 4000)

    def _sync_telemetry_watchers(self) -> None:
        try:
            current = list(self.source_watcher.files())
            if current:
                self.source_watcher.removePaths(current)
        except Exception:
            pass
        self._watch_source_map.clear()
        if not self.live_telemetry_enabled:
            return
        active = [i.text() for i in self.list_datasets.selectedItems()] if hasattr(self, "list_datasets") else []
        names = active or list(self.datasets)
        for name in names:
            ds = self.datasets.get(name)
            if ds is None or not ds.path:
                continue
            try:
                project_path = os.path.abspath(ds.path)
                if os.path.isfile(project_path) and project_path not in self.source_watcher.files():
                    self.source_watcher.addPath(project_path)
                source = self.project.source_for(project_path)
                if source and os.path.isfile(source):
                    source = os.path.abspath(source)
                    self._watch_source_map[source] = project_path
                    if source not in self.source_watcher.files():
                        self.source_watcher.addPath(source)
            except Exception as exc:
                log_line(f"Telemetry watcher setup failed for {name}: {exc}", "LOAD")

    def refresh_dataset_folder(self) -> None:
        """Immediate project-folder rescan without blocking the GUI parser path."""
        try:
            paths = self.project.dataset_files()
        except Exception as exc:
            QMessageBox.critical(self, "Refresh failed", str(exc)); return
        disk = {os.path.abspath(p): p for p in paths}
        registered = {os.path.abspath(ds.path): name for name, ds in self.datasets.items() if ds.path}
        for path, name in list(registered.items()):
            if path.startswith(os.path.abspath(str(self.project.datasets_dir)) + os.sep) and path not in disk:
                self._unregister_dataset(name)
        if not paths:
            self.statusBar().showMessage("Dataset folder is empty.", 4000); return
        self._operation_overlay().start(f"Rescanning {len(paths)} dataset file(s)…", 0)
        for path in paths:
            self.load_dataset_file(path)
        self.statusBar().showMessage(f"Rescan started for {len(paths)} dataset file(s).", 5000)

    def remove_selected_dataset(self) -> None:
        items = self.list_datasets.selectedItems()
        if not items:
            QMessageBox.information(self, "Remove dataset", "Select one or more project datasets first."); return
        names = [i.text() for i in items]
        dlg = QDialog(self); dlg.setWindowTitle("Remove dataset from project auto-scan"); dlg.resize(560, 300)
        root = QVBoxLayout(dlg)
        root.addWidget(QLabel(
            f"<b>Remove {len(names)} selected dataset(s) from this project's auto-scanned list?</b><br><br>"
            "Default: GraphVis unregisters them and moves the project-local copies from <code>datasets</code> to "
            "<code>detached_datasets</code>. Original external source files are never deleted by this action."))
        permanent = QCheckBox("Permanently delete the project-local copied file(s) instead of moving them")
        confirm = QCheckBox("I understand permanent deletion cannot be undone")
        confirm.setEnabled(False)
        permanent.toggled.connect(confirm.setEnabled)
        permanent.toggled.connect(lambda on: confirm.setChecked(False) if not on else None)
        root.addWidget(permanent); root.addWidget(confirm)
        path_box = QPlainTextEdit(); path_box.setReadOnly(True); path_box.setMaximumHeight(90)
        path_box.setPlainText("\n".join(str(getattr(self.datasets.get(n), "path", "")) for n in names))
        root.addWidget(path_box)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.button(QDialogButtonBox.Ok).setText("Remove from project")
        buttons.accepted.connect(dlg.accept); buttons.rejected.connect(dlg.reject); root.addWidget(buttons)
        if dlg.exec() != QDialog.Accepted:
            return
        if permanent.isChecked() and not confirm.isChecked():
            QMessageBox.warning(self, "Permanent deletion not confirmed", "Tick the second confirmation box before permanently deleting project-local copies.")
            return
        if self.controller is None:
            self.show_error("Controller unavailable", "Application controller has not been attached."); return
        for name in names:
            self.controller.detach_dataset(name, delete_local=bool(permanent.isChecked()))
        try:
            self.project_context.remove_dataset_references(names)
        except Exception:
            pass
        self._sync_telemetry_watchers(); self._refresh_active_group_label(); self._refresh_recommendations()

    def hard_delete_selected_dataset(self) -> None:
        """Compatibility alias retained for old plugins/menu bindings."""
        self.remove_selected_dataset()

    def restore_detached_dataset(self) -> None:
        folder = str(self.project.detached_datasets_dir)
        os.makedirs(folder, exist_ok=True)
        path, _ = QFileDialog.getOpenFileName(self, "Restore removed dataset", folder, "All files (*.*)")
        if not path:
            return
        try:
            restored = self.project.restore_detached_dataset(path)
        except Exception as exc:
            return self.show_error("Restore failed", f"{type(exc).__name__}: {exc}")
        self.load_dataset_file(restored)
        self.statusBar().showMessage(f"Restored {os.path.basename(restored)} to project datasets.", 4000)

    def load_dataset_file(self, filepath=None):
        if not filepath:
            filepath, _ = QFileDialog.getOpenFileName(
                self, "Select dataset", "",
                "Datasets (*.mat *.csv *.tsv *.txt *.asc *.dat *.json *.xlsx *.xls *.xlsm *.h5 *.hdf *.hdf5 *.nc *.netcdf *.tdm *.tdms *.html *.htm *.xml *.wav *.flac *.mgf *.mzml *.mzxml *.fcs);;All files (*.*)")
        if not filepath:
            return
        filepath = os.path.abspath(filepath)
        project_root = os.path.abspath(str(self.project.root))
        outside_project = not (filepath == project_root or filepath.startswith(project_root + os.sep))
        self._pending_loads += 1
        background_scan = bool(self._auto_scan_in_progress)
        self.pill_render.set_state(("scanning datasets" if background_scan else f"loading {os.path.basename(filepath)}"), "#2980B9")
        if hasattr(self, "preview_canvas") and not background_scan:
            self._operation_overlay().start(f"Loading {os.path.basename(filepath)}…", 0, cancellable=True)
        # Copying a large external HDF/MAT file can itself be expensive, so it
        # runs inside the same worker after the progress overlay is visible.
        task = LoadTask(filepath, project=self.project if outside_project else None)
        if not background_scan:
            self._set_active_background_task(task, f"Loading {os.path.basename(filepath)}…")
            task.signals.progress.connect(lambda msg, pct: self._operation_overlay().set_message(msg, pct) if hasattr(self, "preview_canvas") else None)
        else:
            task.signals.progress.connect(lambda msg, pct: self.statusBar().showMessage(f"Background dataset scan: {msg}", 1200))
        task.signals.finished.connect(self.on_dataset_loaded)
        task.signals.failed.connect(self._on_load_failed)
        task.signals.cancelled.connect(self._on_dataset_load_cancelled)
        submit(task)

    def _on_load_failed(self, msg):
        self._pending_loads = max(0, self._pending_loads - 1)
        if hasattr(self, "preview_canvas") and not self._auto_scan_in_progress:
            self._operation_overlay().stop()
        self._clear_active_background_task()
        log_line(f"Dataset load failed: {msg}", "LOAD")
        self.pill_render.set_state("load failed", "#C0392B")
        if not self._auto_scan_in_progress:
            QMessageBox.critical(self, "Dataset load error", msg.splitlines()[0])
        else:
            self.statusBar().showMessage("One project dataset could not be indexed; see Logs/Errors and Logs/Debug.", 5000)
        self._finish_auto_scan_if_ready()

    def _on_dataset_load_cancelled(self):
        self._pending_loads = max(0, self._pending_loads - 1)
        self._on_background_cancelled()
        self._finish_auto_scan_if_ready()

    def _finish_auto_scan_if_ready(self):
        if self._auto_scan_in_progress and self._pending_loads <= 0:
            self._auto_scan_in_progress = False
            self._suppress_auto_render = False
            if hasattr(self, "preview_canvas") and self.preview_canvas.last_result is None:
                n = len(self.datasets)
                self.preview_canvas.show_blank_workspace(
                    f"Blank workspace — {n} project dataset(s) scanned. Select data and click Generate / Preview, or use Add Literature and Smart Suite."
                )
            self.statusBar().showMessage(f"Project scan complete: {len(self.datasets)} dataset(s) available; no plots were generated.", 5000)

    def load_csv_dataset(self, filepath: str) -> None:
        try:
            ds = load_csv_dataset(filepath)
            self._commit_dataset_to_model(ds)
            self._register_dataset(ds)
        except Exception as exc:
            QMessageBox.critical(self, "Dataset load error", str(exc))

    def _commit_dataset_to_model(self, ds: Dataset) -> None:
        """MVC boundary for datasets produced by asynchronous/services paths."""
        if self.controller is not None:
            self.controller.register_loaded_dataset(ds)
        else:
            self.app_model.register_dataset(ds)

    def _register_dataset(self, ds: Dataset) -> None:
        """Synchronize View widgets for a dataset already committed to the Model."""
        new = not any(self.list_datasets.item(i).text() == ds.name for i in range(self.list_datasets.count()))
        if new:
            item = QListWidgetItem(ds.name)
            item.setToolTip(ds.describe())
            self.list_datasets.addItem(item)
            if self.list_datasets.count() == 1:
                self.list_datasets.setCurrentRow(0)
        try:
            if self.live_telemetry_enabled and ds.path and os.path.isfile(ds.path):
                project_path = os.path.abspath(ds.path)
                if project_path not in self.source_watcher.files():
                    self.source_watcher.addPath(project_path)
                source = self.project.source_for(project_path) if hasattr(self.project, "source_for") else None
                if source and os.path.isfile(source):
                    source = os.path.abspath(source)
                    self._watch_source_map[source] = project_path
                    if source not in self.source_watcher.files():
                        self.source_watcher.addPath(source)
        except Exception:
            pass

    def _unregister_dataset(self, name: str):
        ds = self.datasets.get(name)
        if self.controller is not None:
            self.controller.unregister_dataset(name)
        else:
            self.app_model.unregister_dataset(name)
        if ds is not None:
            try:
                project_path = os.path.abspath(ds.path) if ds.path else ""
                if project_path and project_path in self.source_watcher.files():
                    self.source_watcher.removePath(project_path)
                for src, target in list(self._watch_source_map.items()):
                    if target == project_path:
                        if src in self.source_watcher.files():
                            self.source_watcher.removePath(src)
                        self._watch_source_map.pop(src, None)
            except Exception:
                pass
        for i in range(self.list_datasets.count() - 1, -1, -1):
            if self.list_datasets.item(i).text() == name:
                self.list_datasets.takeItem(i)
        if self.list_datasets.count() and not self.list_datasets.selectedItems():
            self.list_datasets.setCurrentRow(0)
        self.populate_axes(self.primary_dataset())
        self.queue_render()
        return ds

    def _on_source_file_changed(self, path: str):
        if not self.live_telemetry_enabled:
            return
        path = os.path.abspath(path)
        if path in self._live_reload_pending:
            return
        project_path = self._watch_source_map.get(path)
        reload_path = project_path or path
        self._live_reload_pending.add(path)
        self._live_reload_pending.add(reload_path)
        self.statusBar().showMessage(f"Source changed — refreshing {os.path.basename(reload_path)}…", 5000)

        def reload_now():
            try:
                if project_path:
                    if not os.path.exists(path):
                        raise FileNotFoundError(path)
                    reload_target = self.project.sync_from_source(project_path)
                else:
                    reload_target = reload_path
                if not os.path.exists(reload_target):
                    raise FileNotFoundError(reload_target)
                for watched in (path, reload_target):
                    if os.path.exists(watched) and watched not in self.source_watcher.files():
                        self.source_watcher.addPath(watched)
                self.load_dataset_file(reload_target)
            except Exception as exc:
                log_line(f"Live source refresh failed for {path}: {exc}", "LOAD")
            finally:
                self._live_reload_pending.discard(path)
                self._live_reload_pending.discard(reload_path)
        # Atomic saves can briefly replace the watched path. A short debounce
        # lets the file settle before project synchronisation and re-parsing.
        QTimer.singleShot(450, reload_now)

    def on_dataset_loaded(self, ds: Dataset):
        self._pending_loads = max(0, self._pending_loads - 1)
        if self.variable_context:
            ds.meta["variable_context"] = copy.deepcopy(self.variable_context)
            for raw, rec in self.variable_context.items():
                if raw in ds.df.columns and rec.get("unit"):
                    ds.units[raw] = rec["unit"]
        if hasattr(self, "preview_canvas"):
            self._operation_overlay().stop()
        self._clear_active_background_task()
        self._commit_dataset_to_model(ds)
        self._register_dataset(ds)
        if not self._auto_scan_in_progress:
            try:
                self.project_context.add_to_group(datasets=[ds.name])
                self._refresh_active_group_label()
            except Exception as exc:
                log_line(f"Project-group dataset link skipped: {exc}", "CONTEXT")
        prim = self.primary_dataset()
        if ds.meta.get('kind') != 'csv' and (prim is None or prim.meta.get('kind') == 'csv'):
            self.list_datasets.blockSignals(True)
            self.list_datasets.clearSelection()
            for i in range(self.list_datasets.count()):
                if self.list_datasets.item(i).text() == ds.name:
                    self.list_datasets.setCurrentRow(i)
            self.list_datasets.blockSignals(False)
        if self.primary_dataset() is ds:
            self.populate_axes(ds)
            self.fit_all_axes()
            # Loading a dataset never renders implicitly.  GraphVis stays in
            # the blank/manual-generation state until the user chooses a graph
            # or presses Preview.
        self.pill_render.set_state("idle", "#7F8C8D")
        self.statusBar().showMessage(f"Loaded {ds.name}: {ds.describe()}", 5000)
        self._finish_auto_scan_if_ready()

    def on_selection_changed(self):
        ds = self.primary_dataset()
        if self.live_telemetry_enabled:
            self._sync_telemetry_watchers()
        self.populate_axes(ds)
        self._sync_assumption_controls(ds)
        self._refresh_recommendations()
        # Dataset switches must never retain data-specific overlays, limit
        # stars or literature lines from the previous selection.  Clear the
        # live canvas but keep the project/dataset registry intact.
        self.current_experimental_overlay = None
        self.literature_overlays = []
        if hasattr(self, "preview_canvas"):
            self.preview_canvas.clear_for_dataset_change()
        self.pill_render.set_state("idle", "#7F8C8D")
        self.statusBar().showMessage("Dataset selected — choose a graph or press Preview to generate.", 3000)

    def _sync_assumption_controls(self, ds: Dataset | None):
        self.chk_assumed_dataset.blockSignals(True)
        self.txt_assumption_note.blockSignals(True)
        self.chk_assumed_dataset.setChecked(bool(ds.assumed) if ds else False)
        self.txt_assumption_note.setText(ds.assumption_note if ds else "")
        self.chk_assumed_dataset.blockSignals(False)
        self.txt_assumption_note.blockSignals(False)

    def _set_selected_assumed(self, on: bool) -> None:
        for item in self.list_datasets.selectedItems():
            name = item.text(); ds = self.datasets.get(name)
            if ds is not None:
                if self.controller is not None:
                    self.controller.set_dataset_assumption(name, bool(on))
                else:
                    self.app_model.set_dataset_assumption(name, bool(on))
                item.setToolTip(ds.describe() + ("\nAssumed/incomplete data" if ds.assumed else ""))
        self.queue_render()

    def _set_selected_assumption_note(self) -> None:
        note = self.txt_assumption_note.text().strip()
        for item in self.list_datasets.selectedItems():
            name = item.text()
            if name in self.datasets:
                if self.controller is not None:
                    self.controller.set_dataset_assumption_note(name, note)
                else:
                    self.app_model.set_dataset_assumption_note(name, note)

    def _stored_ai_graph_hints(self, group_name: str | None = None) -> list[tuple[str, str]]:
        if not hasattr(self, "project_context"):
            return []
        result = self.project_context.load_ai_advice(group_name)
        out = []
        for rec in result.get("recommendations", []) if isinstance(result, dict) else []:
            if not isinstance(rec, dict):
                continue
            raw = str(rec.get("graph", "")).strip()
            if not raw:
                continue
            match = raw if raw in CHART_TYPES else None
            if match is None:
                matches = difflib.get_close_matches(raw, CHART_TYPES, n=1, cutoff=0.48)
                match = matches[0] if matches else None
            if match:
                out.append((match, "AI Project Advisor: " + str(rec.get("reason", "recommended from project context"))))
        return out

    def _active_group_literature(self) -> list[LiteratureExtraction]:
        try:
            group = self.project_context.get_group()
            if group and group.literature:
                cached = self.project_context.load_literature(group.literature)
                return cached or self.literature
        except Exception:
            pass
        return self.literature

    def _scan_cache_for(self, ds: Dataset | None) -> dict | None:
        if ds is None:
            return None
        cached = self._scan_results.get(ds.name)
        literature = self._active_group_literature()
        if cached and cached.get("dataset_fingerprint") == dataset_fingerprint(ds):
            # A context change invalidates the mapping, so do not silently reuse
            # axes learned from a different paper/thesis group.
            from graphvis.analysis.intelligent_scan import literature_fingerprint
            if cached.get("literature_fingerprint") == literature_fingerprint(literature):
                return cached
        cached = load_cached_scan(ds, self.scan_cache_dir, literature)
        if cached:
            self._scan_results[ds.name] = cached
        return cached

    def _refresh_recommendations(self):
        ds = self.primary_dataset()
        caps = chart_capabilities(ds)
        if hasattr(self, "picker"):
            self.picker.setCapabilities(caps)
        group = self.project_context.get_group() if hasattr(self, "project_context") else None
        if ds is None:
            suffix = f" — active group: {group.name}" if group else ""
            self.lbl_recommended.setText("Recommended: select a project dataset" + suffix)
            if hasattr(self, "picker"):
                self.picker.setRecommendations([])
            if hasattr(self, "lbl_scan_status"):
                self.lbl_scan_status.setText("No dataset selected")
            return

        scan = self._scan_cache_for(ds)
        scan_recs = list(scan.get("recommendations", [])) if scan else []
        names: list[str] = []
        if scan_recs:
            for rec in scan_recs:
                chart = str(rec.get("graph") or "")
                if chart in CHART_TYPES and chart not in names:
                    names.append(chart)
            top = scan_recs[0] if scan_recs else {}
            mp = top.get("mappings") or {}
            axes = ", ".join(f"{k.upper()}={v}" for k, v in mp.items() if k in ("x","y","z") and v)
            prefix = f"Deep scan for {group.name}: " if group else "Deep scan: "
            self.lbl_recommended.setText(prefix + " • ".join(names[:3]) + (f"  |  {axes}" if axes else ""))
            if hasattr(self, "lbl_scan_status"):
                when = time.strftime("%Y-%m-%d %H:%M", time.localtime(float(scan.get("generated", time.time()))))
                self.lbl_scan_status.setText(f"Cached intelligent scan loaded • {when} • {len(scan_recs)} mappings")
        else:
            advice = intelligent_visualization_advisor(ds, literature=self._active_group_literature())
            if hasattr(self, "project_context"):
                advice = (self._stored_ai_graph_hints(group.name if group else None) +
                          self.project_context.script_graph_hints(group.name if group else None) +
                          self.project_context.keyword_graph_hints(group.name if group else None) + advice)
            for chart, _why in advice:
                if chart in CHART_TYPES and chart not in names:
                    names.append(chart)
            if not names:
                names = [k for k, (ok, _) in caps.items() if ok][:5]
            prefix = f"Recommended for {group.name}: " if group else "Recommended: "
            self.lbl_recommended.setText(prefix + " • ".join(names[:3]))
            if hasattr(self, "lbl_scan_status"):
                self.lbl_scan_status.setText("Run Scan Dataset for graph-specific axis mapping and cached recommendations")
        if hasattr(self, "picker"):
            self.picker.setRecommendations(names[:10])

    def start_dataset_scan(self, *, literature: list[LiteratureExtraction] | None = None, force: bool = True) -> None:
        ds = self.primary_dataset()
        if ds is None:
            QMessageBox.information(self, "Scan Dataset", "Select a project dataset first.")
            return
        literature = list(self._active_group_literature() if literature is None else literature)
        budget = float(self._smart_budget_seconds())
        if not force:
            cached = load_cached_scan(ds, self.scan_cache_dir, literature)
            if cached and float(cached.get("time_budget_seconds", 0.0)) >= budget * 0.85:
                self._scan_results[ds.name] = cached; self._refresh_recommendations(); return
        self._operation_overlay().start(f"Scanning {ds.name}…", 2, cancellable=True)
        self.pill_render.set_state("scanning dataset", "#2471A3")
        if hasattr(self, "lbl_scan_status"):
            self.lbl_scan_status.setText("Deep scan running — profiling variables and testing relationships…")
        task = DatasetScanTask(ds, literature, str(self.scan_cache_dir), self._smart_budget_seconds())
        self._set_active_background_task(task, f"Scanning dataset: {ds.name}")
        task.signals.progress.connect(lambda msg, pct: self._operation_overlay().set_message(msg, pct))
        task.signals.finished.connect(lambda result, name=ds.name: self._finish_dataset_scan(name, result))
        task.signals.failed.connect(self._dataset_scan_failed)
        task.signals.cancelled.connect(self._on_background_cancelled)
        submit(task)

    def _finish_dataset_scan(self, dataset_name: str, result: dict) -> None:
        self._operation_overlay().stop(); self._clear_active_background_task(); self.pill_render.set_state("idle", "#7F8C8D")
        self._scan_results[dataset_name] = result
        self._refresh_recommendations()
        recs = result.get("recommendations", [])
        if recs:
            top = recs[0]; mappings = top.get("mappings") or {}
            axes = ", ".join(f"{k.upper()}={v}" for k,v in mappings.items() if v)
            self.statusBar().showMessage(f"Dataset scan cached: {top.get('graph')} ({axes})", 7000)
        else:
            self.statusBar().showMessage("Dataset scan completed; no strong graph mappings were found.", 5000)

    def _dataset_scan_failed(self, msg: str) -> None:
        self._operation_overlay().stop(); self._clear_active_background_task(); self.pill_render.set_state("idle", "#7F8C8D")
        log_line(f"Dataset scan failed: {msg}", "SCAN")
        if hasattr(self, "lbl_scan_status"):
            self.lbl_scan_status.setText("Scan failed — see Logs/Errors and Logs/Debug")
        QMessageBox.critical(self, "Dataset scan failed", msg.splitlines()[0])

    def scan_dataset_with_literature(self) -> None:
        """Attach/read a paper or thesis, then deep-scan the current dataset with that context."""
        if self.primary_dataset() is None:
            return QMessageBox.information(self, "Scan + Literature", "Select a dataset first.")
        path, _ = QFileDialog.getOpenFileName(self, "Attach literature to dataset scan", str(self.project.literature_dir),
                                               "Literature (*.pdf *.txt *.md);;All files (*.*)")
        if not path:
            return
        def ready(ext: LiteratureExtraction):
            self._record_literature_context(ext)
            self.start_dataset_scan(literature=self._active_group_literature(), force=True)
        self._run_literature(path, ready)

    def primary_dataset(self) -> Dataset | None:
        sel = self.selected_datasets()
        return next(iter(sel.values()), None)

    def selected_datasets(self) -> dict:
        return {i.text(): self.datasets[i.text()] for i in self.list_datasets.selectedItems() if i.text() in self.datasets}

    def auto_load_datasets_from_folder(self):
        """Scan the active project's dataset folder without generating plots."""
        self._auto_scan_in_progress = True
        self._suppress_auto_render = True
        found = False
        try:
            paths = self.project.dataset_files()
        except Exception:
            exts = ('.mat','.csv','.tsv','.txt','.json','.xlsx','.xls','.h5','.hdf','.hdf5','.nc','.netcdf',
                    '.tdm','.tdms','.html','.htm','.xml')
            paths = [os.path.join(self.dataset_dir, f) for f in sorted(os.listdir(self.dataset_dir))
                     if f.lower().endswith(exts)] if os.path.exists(self.dataset_dir) else []
        for path in paths:
            if os.path.basename(path) in self.datasets:
                continue
            self.load_dataset_file(path); found = True
        if not found and self._pending_loads <= 0:
            self._auto_scan_in_progress = False
            self._suppress_auto_render = False
            if hasattr(self, "preview_canvas"):
                self.preview_canvas.show_blank_workspace(
                    f"Blank workspace — {len(self.datasets)} project dataset(s) scanned. Add/select data or read literature, then generate a graph when ready."
                )


    # ------------------------------------------------------------ literature
    def _run_literature(self, path, on_done):
        self._operation_overlay().start(f"Reading {os.path.basename(path)}…", 0, cancellable=True)
        self.pill_render.set_state("reading literature", "#8E44AD")
        task = LiteratureTask(path, extract_dataset=self.chk_extract_lit.isChecked(), ocr=self.chk_ocr.isChecked(),
                              project=self.project, time_budget_seconds=self._literature_budget_seconds())
        self._set_active_background_task(task, f"Reading literature: {os.path.basename(path)}")
        task.signals.progress.connect(lambda msg, pct: self._operation_overlay().set_message(msg, pct))
        task.signals.finished.connect(lambda ext: self._finish_literature_task(ext, on_done))
        task.signals.failed.connect(self._on_literature_failed)
        task.signals.cancelled.connect(self._on_background_cancelled)
        submit(task)

    def _finish_literature_task(self, ext, on_done):
        self._operation_overlay().stop()
        self._clear_active_background_task()
        self.pill_render.set_state("idle", "#7F8C8D")
        on_done(ext)

    def _on_literature_failed(self, msg):
        self._operation_overlay().stop()
        self._clear_active_background_task()
        self.pill_render.set_state("idle", "#7F8C8D")
        log_line(f"Literature extraction failed: {msg}", "LITERATURE")
        QMessageBox.critical(self, "Literature read error", msg.splitlines()[0])

    def _record_literature_context(self, ext: LiteratureExtraction, group_name: str | None = None) -> list[str]:
        """Persist an extraction, register extracted datasets, and link it to a project group."""
        if not any(getattr(x, "path", None) == ext.path for x in self.literature):
            self.literature.append(ext)
        remap = {}
        dataset_names = []
        for p in list(ext.saved_paths):
            try:
                project_path = self.project.ingest_file(p, "datasets")
            except Exception:
                project_path = p
            remap[p] = project_path
            try:
                self.load_csv_dataset(project_path)
                dataset_names.append(os.path.basename(project_path))
            except Exception as exc:
                log_line(f"Extracted literature dataset registration failed: {project_path}: {exc}", "LITERATURE")
        ext.saved_paths = [remap.get(p, p) for p in ext.saved_paths]
        for d in ext.datasets:
            if d.saved_path in remap:
                d.saved_path = remap[d.saved_path]
        self.project_context.cache_literature(ext)
        self.project_context.add_to_group(literature=[ext.path], datasets=dataset_names, group_name=group_name)
        self._refresh_active_group_label()
        self._refresh_recommendations()
        self.refresh_object_manager()
        log_line("Literature context saved:\n" + ext.summary(), "LITERATURE")
        return dataset_names

    def read_literature_only(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Read literature / thesis", str(self.project.literature_dir),
            "Literature (*.pdf *.txt *.md);;PDF (*.pdf);;Text (*.txt *.md);;All files (*.*)")
        if not path:
            return
        self._run_literature(path, self._on_literature_context_ready)

    def _on_literature_context_ready(self, ext: LiteratureExtraction):
        names = self._record_literature_context(ext)
        opened = 0
        overlaid = 0
        if getattr(self, "chk_lit_overlay", None) is not None and self.chk_lit_overlay.isChecked() and ext.datasets:
            # Explicit opt-in: recreate the experimental data as overlays on
            # the active simulation curves for direct model-vs-literature
            # validation.
            for i in range(len(ext.datasets)):
                try:
                    self.add_overlay_from(ext, i)
                    overlaid += 1
                except Exception as exc:
                    log_line(f"Literature overlay skipped: {exc}", "LITERATURE")
        if self.chk_auto_graphs.isChecked() and ext.datasets:
            for i in range(len(ext.datasets)):
                try:
                    self.recreate_literature_graph(ext, i)
                    opened += 1
                except Exception as exc:
                    log_line(f"Auto recreation skipped: {exc}", "LITERATURE")
        group = self.project_context.get_group()
        msg = (
            f"Read and remembered:\n{ext.title}\n\n"
            f"Context saved to project group: {group.name if group else 'Project Overview'}\n"
            f"Detected parameters: {len(ext.parameters)}\n"
            f"Extracted numeric datasets: {len(names)}\n\n"
            "The document will be reused by Smart Map Suite on later starts; you do not need to select it again."
        )
        if overlaid:
            msg += f"\n\nOverlaid {overlaid} literature dataset(s) on the active graph."
        if opened:
            msg += f"\n\nOpened {opened} reconstructed graph tab(s)."
        QMessageBox.information(self, "Literature context saved", msg)

    def upload_literature_file(self):
        paths, _ = QFileDialog.getOpenFileNames(
            self, "Add linked literature bundle", str(self.project.literature_dir),
            "Documents/data (*.pdf *.txt *.md *.csv *.tsv *.json *.xlsx *.xls);;All files (*.*)")
        if not paths:
            return
        current = self.project_context.active_group_name or "Project Overview"
        group_name, ok = QInputDialog.getText(
            self, "Linked literature bundle",
            "Which Project Group should this paper/thesis + supplements belong to?\nYou can type a new group name:",
            text=current)
        if not ok or not group_name.strip():
            return
        group_name = group_name.strip()
        if not self.project_context.get_group(group_name):
            self.project_context.save_group(ProjectGroup(name=group_name), True)
        else:
            self.project_context.set_active(group_name)
        self._refresh_active_group_label()
        paths = [os.path.abspath(src) for src in paths]
        self._pending_literature_group = group_name
        if len(paths) == 1:
            self._run_literature(paths[0], lambda ext: self._on_literature_context_ready_for_group(ext, group_name))
            return
        self._operation_overlay().start(f"Reading linked bundle: {len(paths)} files…", 0, cancellable=True)
        self.pill_render.set_state("literature bundle", "#8E44AD")
        task = LiteratureBatchTask(paths, extract_dataset=self.chk_extract_lit.isChecked(), ocr=self.chk_ocr.isChecked(),
                                   project=self.project, time_budget_seconds=self._literature_budget_seconds())
        self._set_active_background_task(task, f"Reading {len(paths)} linked literature files…")
        task.signals.progress.connect(lambda msg, pct: self._operation_overlay().set_message(msg, pct))
        task.signals.finished.connect(self._on_literature_batch_ready)
        task.signals.failed.connect(self._on_literature_failed)
        task.signals.cancelled.connect(self._on_background_cancelled)
        submit(task)

    def _on_literature_context_ready_for_group(self, ext: LiteratureExtraction, group_name: str):
        self._record_literature_context(ext, group_name)
        QMessageBox.information(
            self, "Literature context saved",
            f"'{ext.title}' was read and assigned to Project Group '{group_name}'.\n"
            f"Recovered {len(ext.datasets)} numeric dataset(s). No graph was opened automatically unless that option is enabled."
        )

    def _on_literature_batch_ready(self, extractions):
        self._operation_overlay().stop()
        self._clear_active_background_task()
        self.pill_render.set_state("idle", "#7F8C8D")
        group_name = getattr(self, "_pending_literature_group", None) or self.project_context.active_group_name
        n_data = 0
        for ext in extractions:
            n_data += len(self._record_literature_context(ext, group_name))
        opened = 0
        if self.chk_auto_graphs.isChecked() and n_data:
            for ext in extractions:
                for idx in range(len(ext.datasets)):
                    try:
                        self.recreate_literature_graph(ext, idx)
                        opened += 1
                    except Exception as exc:
                        log_line(f"Auto recreation skipped for {ext.title} dataset {idx}: {exc}", "LITERATURE")
        extra = f"\nOpened {opened} reconstructed graph tab(s)." if opened else "\nNo graph tabs were opened automatically."
        QMessageBox.information(
            self, "Linked literature bundle saved",
            f"Processed {len(extractions)} linked file(s) and recovered {n_data} numeric dataset(s).\n\n"
            f"Assigned to Project Group: {group_name or 'Project Overview'}\n"
            "The first PDF is treated as the primary document; the remaining files are retained as linked supplements. "
            "You can change membership at any time in Project Groups." + extra
        )

    def read_data_and_plot_literature(self):
        """Legacy explicit overlay workflow retained for users who want it."""
        path, _ = QFileDialog.getOpenFileName(
            self, "Read literature and overlay extracted data", "",
            "Files (*.pdf *.txt *.md *.csv *.tsv *.json *.xlsx *.xls)")
        if path:
            self._run_literature(path, lambda ext: self._on_literature_ready(ext, overlay_first=True))

    def _on_literature_ready(self, ext: LiteratureExtraction, overlay_first=False):
        self._record_literature_context(ext)
        if not ext.datasets:
            QMessageBox.information(
                self, "Literature ingested",
                f"'{ext.title}' was read ({ext.method}, {len(ext.text):,} characters) but no numeric tables or series were found.\n"
                f"Parameters: {len(ext.parameters)} detected."
            )
            return
        dlg = LiteratureResultDialog(ext, self)
        if overlay_first:
            dlg.choice = "overlay"
            idx = 0
        else:
            if not dlg.exec():
                return
            idx = dlg.selected_index()
        if dlg.choice == "overlay" and idx is not None:
            self.add_overlay_from(ext, idx)
        elif dlg.choice == "recreate" and idx is not None:
            self.recreate_literature_graph(ext, idx)
        elif dlg.choice == "recreate_predictive" and idx is not None:
            self.recreate_literature_graph(ext, idx, predictive=True)
        elif dlg.choice == "recreate_all":
            for i in range(len(ext.datasets)):
                self.recreate_literature_graph(ext, i)

    def add_overlay_from(self, ext: LiteratureExtraction, idx: int):
        d = ext.datasets[idx]
        num = d.df.select_dtypes(include=[np.number])
        if len(num.columns) < 2:
            return
        cols = [c for c in num.columns if not str(c).endswith("_err")]
        if len(cols) < 2:
            cols = list(num.columns[:2])
        x, y = num[cols[0]].to_numpy(float), num[cols[1]].to_numpy(float)
        yerr = num[d.yerr_col].to_numpy(float) if d.yerr_col in num.columns else None
        ov = {'x': x.tolist(), 'y': y.tolist(), 'yerr': yerr.tolist() if yerr is not None else None,
              'label': f"Lit: {ext.title[:40]} — {d.name}"}
        self.literature_overlays.append(ov)
        self.generate_preview()
        self.statusBar().showMessage(f"Overlay applied: {ov['label']}", 5000)

    def clear_overlays(self):
        self.literature_overlays.clear()
        self.current_experimental_overlay = None
        self.generate_preview()

    def recreate_literature_graph(self, ext: LiteratureExtraction, idx: int, predictive: bool = False):
        d = ext.datasets[idx]
        if not d.saved_path or os.path.basename(d.saved_path) not in self.datasets:
            return
        ds = self.datasets[os.path.basename(d.saved_path)]
        cols = [c for c in ds.numeric_columns if not str(c).endswith("_err")] or ds.numeric_columns
        semantic = ext.semantic_context or {}
        chart = semantic.get("inferred_plot_type") or {
            "gompertz": "Gompertz H₂ Kinetics", "polarisation": "Polarisation & Power Curve",
            "nyquist": "EIS: Nyquist", "bode": "EIS: Bode"
        }.get(d.kind_hint, "Line Chart")
        if chart not in CHART_TYPES:
            chart = "Line Chart"

        def semantic_col(role: str, fallback_idx: int):
            cand = semantic.get(f"{role}_variable")
            if cand in ds.df.columns and cand in ds.numeric_columns:
                return cand
            # Semantic engine may have inferred against a different extracted table; match normalized tokens.
            if cand:
                norm = lambda v: "".join(ch.lower() for ch in str(v) if ch.isalnum())
                nc = norm(cand)
                for c in cols:
                    if norm(c) == nc or (nc and (nc in norm(c) or norm(c) in nc)):
                        return c
            return cols[fallback_idx] if len(cols) > fallback_idx else None

        xcol, ycol, zcol = semantic_col("x", 0), semantic_col("y", 1), semantic_col("z", 2)
        if xcol == ycol:
            ycol = next((c for c in cols if c != xcol), None)
        spec = self.build_spec()
        spec.datasets = {ds.name: ds}
        spec.chart_type = chart
        spec.mappings = {'x': xcol, 'y': ycol, 'z': zcol, 'w': None, 'v': None, 'matrix': None, 'gradient': None}
        spec.clipping_ranges = {}
        spec.literature_overlays = []
        spec.experimental_overlay = None
        spec.styling = copy.deepcopy(DEFAULT_STYLING)
        confidence = semantic.get("confidence")
        suffix = f" · semantic {float(confidence):.0%}" if isinstance(confidence, (float, int)) else ""
        spec.styling["Main Title"]["text"] = f"{ext.title[:58]} — {d.name}{suffix}"
        if predictive and xcol and ycol:
            try:
                sub = ds.df[[xcol, ycol]].replace([np.inf, -np.inf], np.nan).dropna()
                pred = forecast_series(sub[xcol].to_numpy(float), sub[ycol].to_numpy(float), extension=0.25, confidence=0.95)
                spec.literature_overlays.append({
                    "kind": "prediction", "x": pred.x.tolist(), "y": pred.y.tolist(),
                    "lower": pred.lower.tolist(), "upper": pred.upper.tolist(),
                    "observed_x_max": pred.original_x_max,
                    "label": f"{pred.model} extrapolation (R²={pred.r2:.3f})",
                    "band_label": "95% prediction band",
                })
            except Exception as exc:
                QMessageBox.warning(self, "Predictive recreation", f"Graph was recreated, but predictive extrapolation could not be fitted:\n{exc}")
        self._add_tab(spec, f"Lit: {d.name}" + (" + prediction" if predictive else ""))

    # ------------------------------------------------------------ layers / publication / native figures
    def open_layer_manager(self) -> None:
        dlg = LayerManagerDialog(self.datasets.keys(), self.selected_datasets().keys(), self)
        if not dlg.exec():
            return
        active = set(dlg.active_names())
        self.list_datasets.blockSignals(True)
        try:
            for i in range(self.list_datasets.count()):
                item = self.list_datasets.item(i)
                item.setSelected(item.text() in active)
        finally:
            self.list_datasets.blockSignals(False)
        self.on_selection_changed()
        self.generate_preview()

    # ------------------------------------------------------------ graph workspace appearance
    @staticmethod
    def _graph_background_default_brightness(mode: str, base: str) -> int:
        if mode == "Light":
            return 96
        if mode == "White":
            return 100
        if mode == "Dark":
            return 12
        return int(round(colour_lightness(base)))

    def _graph_background_metadata(self) -> dict:
        """Resolve the UI-only figure/axes palette used by interactive plots."""
        ui_name = canonical_theme_name(self.settings.value("ui/theme", "Light", str))
        ui = colours_for(ui_name)
        mode = str(self.settings.value("graph/background_mode", "Light", str) or "Light")
        custom = str(self.settings.value("graph/background_custom", "#F3F6FA", str) or "#F3F6FA")
        if mode == "White":
            base_figure, base_axes = "#FFFFFF", "#FFFFFF"
        elif mode == "Dark":
            base_figure, base_axes = "#181D23", "#20272F"
        elif mode == "Theme":
            base_figure, base_axes = ui.background, ui.surface
        elif mode == "Custom":
            base_figure, base_axes = custom, custom
        else:
            mode = "Light"
            base_figure, base_axes = "#F3F6FA", "#FFFFFF"

        brightness_key = f"graph/background_brightness/{mode.lower()}"
        default_brightness = self._graph_background_default_brightness(mode, base_figure)
        brightness = max(0, min(100, self.settings.value(brightness_key, default_brightness, int)))
        delta = colour_lightness(base_axes) - colour_lightness(base_figure)
        figure = colour_with_lightness(base_figure, brightness)
        axes = colour_with_lightness(base_axes, max(0.0, min(100.0, brightness + delta)))

        merge = self.settings.value("graph/background_merge_theme", False, bool)
        merge_amount = max(0, min(100, self.settings.value("graph/background_merge_amount", 35, int))) / 100.0
        if merge and mode != "Theme":
            figure = blend_colours(figure, ui.background, merge_amount)
            axes = blend_colours(axes, ui.surface, merge_amount)

        contrast = contrasting_graph_colours(axes)
        return {
            "name": f"Graph background: {mode}", "mode": mode,
            "figure": figure, "axes": axes,
            "text": contrast["text"], "muted": contrast["muted"],
            "border": contrast["border"], "grid": contrast["grid"],
            "brightness": brightness, "merge": bool(merge), "merge_amount": merge_amount,
        }

    def _refresh_graph_background_action_checks(self) -> None:
        mode = str(self.settings.value("graph/background_mode", "Light", str) or "Light")
        for name, action in getattr(self, "_graph_background_actions", {}).items():
            action.blockSignals(True); action.setChecked(name == mode); action.blockSignals(False)

    def _set_graph_background_mode(self, mode: str) -> None:
        if mode not in ("Light", "White", "Dark", "Theme", "Custom"):
            mode = "Light"
        self.settings.setValue("graph/background_mode", mode)
        self._refresh_graph_background_action_checks()
        self._apply_graph_background_to_canvases()
        meta = self._graph_background_metadata()
        self.statusBar().showMessage(f"Graph background: {mode} ({meta['brightness']}% brightness)", 3500)

    def _choose_graph_background_custom(self, *_args) -> None:
        previous = str(self.settings.value("graph/background_mode", "Light", str) or "Light")
        initial = QColor(str(self.settings.value("graph/background_custom", "#F3F6FA", str)))
        colour = QColorDialog.getColor(initial, self, "Choose graph background colour")
        if not colour.isValid():
            self.settings.setValue("graph/background_mode", previous)
            self._refresh_graph_background_action_checks()
            return
        self.settings.setValue("graph/background_custom", colour.name(QColor.HexRgb))
        self.settings.setValue("graph/background_mode", "Custom")
        key = "graph/background_brightness/custom"
        if self.settings.value(key) is None:
            self.settings.setValue(key, int(round(colour_lightness(colour.name(QColor.HexRgb)))))
        self._refresh_graph_background_action_checks()
        self._apply_graph_background_to_canvases()

    def _set_graph_background_brightness(self) -> None:
        meta = self._graph_background_metadata()
        value, ok = QInputDialog.getInt(
            self, "Graph background brightness", "Brightness (0 = black, 100 = white):",
            int(meta.get("brightness", 96)), 0, 100, 1,
        )
        if not ok:
            return
        mode = str(meta.get("mode", "Light")).lower()
        self.settings.setValue(f"graph/background_brightness/{mode}", int(value))
        self._apply_graph_background_to_canvases()

    def _toggle_graph_background_merge(self, enabled: bool) -> None:
        self.settings.setValue("graph/background_merge_theme", bool(enabled))
        self._apply_graph_background_to_canvases()

    def _set_graph_background_merge_amount(self) -> None:
        current = self.settings.value("graph/background_merge_amount", 35, int)
        value, ok = QInputDialog.getInt(self, "Theme blend", "Blend graph colour toward UI theme (%):", current, 0, 100, 5)
        if ok:
            self.settings.setValue("graph/background_merge_amount", int(value))
            self._apply_graph_background_to_canvases()

    def _apply_graph_background_to_canvases(self) -> None:
        meta = self._graph_background_metadata()
        canvases = []
        if hasattr(self, "preview_canvas"):
            canvases.append(self.preview_canvas)
        if hasattr(self, "tabs"):
            canvases.extend(self.tabs.widget(i) for i in range(self.tabs.count()))
        seen = set()
        for canvas in canvases:
            if isinstance(canvas, ScientificPlotCanvas) and id(canvas) not in seen:
                seen.add(id(canvas))
                canvas.apply_workspace_background(meta, redraw=True)
        if hasattr(self, "blank_stage"):
            self.blank_stage.setStyleSheet(f"QFrame#GraphVisBlankStage{{background:{meta['figure']};border:0;}}")
        self.settings.sync()

    def _refresh_graph_preview_preferences(self) -> None:
        app = QApplication.instance()
        compact = self.settings.value("graph_library/compact_previews", True, bool)
        merge = self.settings.value("graph_library/merge_previews", True, bool)
        changed = True
        if app is not None:
            changed = app.property("graphvis_compact_previews") != compact or app.property("graphvis_merge_previews") != merge
            app.setProperty("graphvis_compact_previews", compact)
            app.setProperty("graphvis_merge_previews", merge)
        if not changed:
            return
        GraphLibraryDialog._ICON_CACHE.clear()
        if hasattr(self, "picker"):
            self.picker.applyPreviewPreferences()

    def _set_graph_preview_compact(self, enabled: bool) -> None:
        self.settings.setValue("graph_library/compact_previews", bool(enabled))
        self._refresh_graph_preview_preferences()

    def _set_graph_preview_merge(self, enabled: bool) -> None:
        self.settings.setValue("graph_library/merge_previews", bool(enabled))
        self._refresh_graph_preview_preferences()

    def apply_theme(self, name: str) -> None:
        name = canonical_theme_name(name)
        css = THEMES.get(name, THEMES.get("Light", ""))
        app = QApplication.instance()
        app.setStyle("Fusion")
        colours = colours_for(name)
        palette = QPalette()
        palette.setColor(QPalette.Window, QColor(colours.background))
        palette.setColor(QPalette.WindowText, QColor(colours.text))
        palette.setColor(QPalette.Base, QColor(colours.field))
        palette.setColor(QPalette.AlternateBase, QColor(colours.panel))
        palette.setColor(QPalette.Text, QColor(colours.text))
        palette.setColor(QPalette.Button, QColor(colours.surface))
        palette.setColor(QPalette.ButtonText, QColor(colours.text))
        palette.setColor(QPalette.Highlight, QColor(colours.selection))
        palette.setColor(QPalette.HighlightedText, QColor(colours.selection_text))
        palette.setColor(QPalette.ToolTipBase, QColor(colours.tooltip_bg))
        palette.setColor(QPalette.ToolTipText, QColor(colours.tooltip_text))
        app.setPalette(palette)
        app.setStyleSheet(css)
        # Small informational labels use theme-aware muted text instead of
        # fixed light-theme greys, which previously became unreadable in dark
        # modes unless their text was selected.
        for widget_name in ("lbl_chart_desc", "lbl_recommended"):
            widget = getattr(self, widget_name, None)
            if widget is not None:
                widget.setStyleSheet(f"color:{colours.muted}; background:transparent; padding:3px;")
        if hasattr(self, "picker"):
            self.picker.info.setStyleSheet(f"color:{colours.muted}; background:transparent; padding:3px;")
        self.settings.setValue("ui/theme", name)
        self._refresh_graph_preview_preferences()
        self._apply_graph_background_to_canvases()
        self.settings.sync()
        self.statusBar().showMessage(f"Theme applied: {name}", 3000)

    def _choose_publication_profile(self) -> PublicationProfile | None:
        profiles = self.controller.publication_profiles() if self.controller is not None else self.app_model.publication_profiles.list()
        if not profiles:
            return None
        names = [p.name for p in profiles]
        name, ok = QInputDialog.getItem(self, "Publication Ready", "Profile:", names, 0, False)
        if not ok:
            return None
        return next((p for p in profiles if p.name == name), None)

    def apply_publication_ready(self) -> None:
        profile = self._choose_publication_profile()
        if profile is None:
            return
        canvas = self.current_canvas()
        spec = profile.apply_to_spec(canvas.spec)
        canvas.update_spec(spec)
        if canvas is self.preview_canvas:
            self.styling = copy.deepcopy(spec.styling)
            self.cb_colourmap.setCurrentText(spec.colourmap)
            self.chk_bg_grid.setChecked(spec.grid_visible)
        self.history.record(self.snapshot_ui_state(), f"Publication profile: {profile.name}")
        self.statusBar().showMessage(
            f"Publication Ready: {profile.name} · {profile.dpi} dpi · {profile.color_space} · {profile.font_family}", 6000)

    def manage_publication_profiles(self) -> None:
        actions = ["Create new profile", "Edit profile", "Delete custom profile"]
        action, ok = QInputDialog.getItem(self, "Publication profiles", "Action:", actions, 0, False)
        if not ok:
            return
        store = self.app_model.publication_profiles
        if action == "Create new profile":
            dlg = PublicationProfileDialog(parent=self)
            if dlg.exec():
                profile = dlg.profile()
                path = self.controller.save_publication_profile(profile) if self.controller is not None else str(store.save(profile))
                self.statusBar().showMessage(f"Saved publication profile: {path}", 4000)
            return
        profiles = store.list()
        names = [p.name for p in profiles]
        name, ok = QInputDialog.getItem(self, "Publication profiles", "Profile:", names, 0, False)
        if not ok:
            return
        profile = next((p for p in profiles if p.name == name), None)
        if profile is None:
            return
        if action == "Edit profile":
            dlg = PublicationProfileDialog(profile, self)
            if dlg.exec():
                profile2 = dlg.profile()
                if self.controller is not None:
                    self.controller.save_publication_profile(profile2)
                else:
                    store.save(profile2)
                self.statusBar().showMessage(f"Saved publication profile: {profile2.name}", 4000)
        else:
            if name in BUILTIN_PROFILES:
                QMessageBox.information(self, "Publication profiles", "Built-in profiles cannot be deleted. Save a customized copy instead.")
                return
            path = store.directory / f"{store._safe(name)}.json"
            if path.exists() and QMessageBox.question(self, "Delete profile", f"Delete custom profile '{name}'?") == QMessageBox.Yes:
                if self.controller is not None:
                    self.controller.delete_publication_profile(name)
                else:
                    path.unlink()

    def open_gpu_preview_for_current(self) -> None:
        caps = gpu_capabilities()
        try:
            win = open_gpu_preview(self.current_canvas().spec.clone(), self)
            self._gpu_preview_window = win
            self.statusBar().showMessage(
                f"GPU preview opened · VisPy={caps.vispy} · PyVista={caps.pyvista} · PyVistaQt={caps.pyvistaqt}", 5000)
        except Exception as exc:
            QMessageBox.warning(self, "GPU preview", str(exc))

    def save_current_native_figure(self) -> None:
        canvas = self.current_canvas()
        default = str(self.project.figures_dir / f"{self.tabs.tabText(self.tabs.currentIndex()) or 'figure'}.gvfig")
        path, _ = QFileDialog.getSaveFileName(self, "Save editable GraphVis figure", default,
                                              "GraphVis figure (*.gvfig *.gvis)")
        if not path:
            return
        try:
            spec = canvas.spec.clone()
            spec.view_state = canvas.capture_view_state()
            out = save_native_figure(path, spec, metadata={"project": self.project.name, "tab": self.tabs.tabText(self.tabs.currentIndex())})
            self.statusBar().showMessage(f"Editable figure saved: {out}", 5000)
        except Exception as exc:
            log_line(f"Native figure save failed: {exc}", "FIGURE")
            QMessageBox.critical(self, "Save .gvfig", str(exc))

    def open_native_figure(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Open editable GraphVis figure", str(self.project.figures_dir),
                                              "GraphVis figure (*.gvfig *.gvis)")
        if not path:
            return
        try:
            bundle = load_native_figure(path)
            # Embedded arrays are authoritative. Register them without modifying the original source paths.
            embedded_registry = {}
            for name, ds in bundle.datasets.items():
                base = name
                if base in self.datasets and self.datasets[base] is not ds:
                    stem, ext = os.path.splitext(base); n = 2
                    while f"{stem} ({n}){ext}" in self.datasets: n += 1
                    ds.name = f"{stem} ({n}){ext}"; base = ds.name
                self._commit_dataset_to_model(ds)
                self._register_dataset(ds)
                embedded_registry[base] = ds
            bundle.spec.datasets = embedded_registry
            title = str(bundle.metadata.get("tab") or os.path.basename(path))
            canvas = self._add_tab(bundle.spec, title)
            if bundle.spec.view_state:
                QTimer.singleShot(150, lambda st=bundle.spec.view_state, c=canvas: c.apply_external_view(st))
            self.statusBar().showMessage(f"Opened editable GraphVis figure: {path}", 5000)
        except Exception as exc:
            log_line(f"Native figure open failed: {exc}", "FIGURE")
            QMessageBox.critical(self, "Open .gvfig", str(exc))

    def _set_tab_active_checkbox(self, index: int, active: bool = True) -> None:
        if index < 0 or index >= self.tabs.count():
            return
        chk = QCheckBox()
        chk.setChecked(bool(active))
        chk.setToolTip("Active: include this graph in batch export. Uncheck to keep the tab open but skip batch operations.")
        chk.setMaximumWidth(20)
        self.tabs.tabBar().setTabButton(index, QTabBar.ButtonPosition.LeftSide, chk)
        chk.toggled.connect(lambda _=False: self.save_session())
        QTimer.singleShot(0, self._style_tab_navigation_buttons)

    def _tab_is_active(self, index: int) -> bool:
        w = self.tabs.tabBar().tabButton(index, QTabBar.ButtonPosition.LeftSide)
        return bool(w.isChecked()) if isinstance(w, QCheckBox) else True

    def batch_export_active_tabs(self) -> None:
        active: list[tuple[str, ScientificPlotCanvas]] = []
        for i in range(self.tabs.count()):
            w = self.tabs.widget(i)
            if isinstance(w, ScientificPlotCanvas) and self._tab_is_active(i):
                active.append((self.tabs.tabText(i), w))
        if not active:
            QMessageBox.information(self, "Batch export", "No active graph tabs are checked.")
            return
        directory = QFileDialog.getExistingDirectory(self, "Export active graph tabs", str(self.project.exports_dir))
        if not directory:
            return
        fmt, ok = QInputDialog.getItem(self, "Batch export", "Format:", list(EXPORT_FORMATS), 0, False)
        if not ok:
            return
        default_dpi = int(getattr(active[0][1].spec, "export_dpi", 300) or 300)
        dpi_text, ok = QInputDialog.getText(
            self, "Batch export",
            "DPI (any positive integer; very high values may use substantial RAM/disk):",
            text=str(max(1, default_dpi)),
        )
        if not ok:
            return
        try:
            dpi = int(str(dpi_text).strip())
            if dpi <= 0:
                raise ValueError
        except Exception:
            QMessageBox.warning(self, "Batch export", "Enter a positive whole-number DPI.")
            return
        _safe, dpi_note = memory_verdict(active[0][1].spec.figsize, dpi, fmt)
        if dpi > 3000 or "WARNING:" in dpi_note:
            QMessageBox.warning(self, "High-resolution batch export", dpi_note + "\n\nThe requested DPI will not be blocked; GraphVis will attempt the export.")
        jobs = []
        for name, canvas in active:
            spec = canvas.spec.clone(); spec.view_state = canvas.capture_view_state()
            jobs.append({"name": name, "spec": spec})
        color_mode = getattr(active[0][1].spec, "export_color_mode", "RGB")
        task = BatchExportTask(jobs, directory, fmt, dpi, color_mode=color_mode,
                               icc_profile=getattr(active[0][1].spec, "export_icc_profile", ""))
        overlay = active[0][1].overlay
        overlay.start(f"Batch exporting {len(jobs)} graph(s)…")
        task.signals.progress.connect(lambda msg, pct: overlay.set_message(msg, pct))
        def done(results):
            overlay.stop(); self.statusBar().showMessage(f"Batch export complete: {len(results)} graph(s) → {directory}", 7000)
        def failed(msg):
            overlay.stop(); log_line(f"Batch export failed: {msg}", "EXPORT"); QMessageBox.critical(self, "Batch export failed", msg.splitlines()[0])
        task.signals.finished.connect(done); task.signals.failed.connect(failed)
        HEAVY_POOL.start(task)

    # ------------------------------------------------------------ advisor
    def open_graph_library(self):
        caps = chart_capabilities(self.primary_dataset())
        dlg = GraphLibraryDialog(caps, self)
        dlg.advanced.setChecked(self.btn_mode_advanced.isChecked())
        if dlg.exec() and dlg.selected_entry:
            self._on_picker_entry_selected(dlg.selected_entry)
            if hasattr(self, "picker"):
                self.picker._select_entry(dlg.selected_entry, emit=False)

    def run_intelligent_advisor(self):
        ds = self.primary_dataset()
        scan = self._scan_cache_for(ds)
        if scan and scan.get("recommendations"):
            advice = [(r.get("graph", "Line Chart"), r.get("reason", "Deep dataset scan")) for r in scan.get("recommendations", [])]
        else:
            advice = intelligent_visualization_advisor(ds, literature=self._active_group_literature())
        dlg = IntelligentAdvisorDialog(advice, self)
        if dlg.exec() and dlg.selected_chart:
            entry = next((e for e in GRAPH_LIBRARY if e.get("engine") == dlg.selected_chart),
                         {"engine": dlg.selected_chart, "description": CHART_DESCRIPTIONS.get(dlg.selected_chart, ""), "scale": None})
            self._on_picker_entry_selected(entry)

    # ------------------------------------------------------------ spec
    def build_spec(self) -> PlotSpec:
        clips = {}
        if self.chk_use_clip.isChecked():
            for k, (smin, smax) in {'x': (self.spin_xmin, self.spin_xmax), 'y': (self.spin_ymin, self.spin_ymax),
                                    'z': (self.spin_zmin, self.spin_zmax), 'w': (self.spin_wmin, self.spin_wmax),
                                    'v': (self.spin_vmin, self.spin_vmax)}.items():
                if smin.value() != smax.value():
                    clips[k] = (smin.value(), smax.value())
        prev = self.preview_canvas if hasattr(self, "preview_canvas") else None
        limits = {"plateau": self.chk_lim_plateau.isChecked(), "asymptote": self.chk_lim_asym.isChecked(),
                  "confidence": self.chk_lim_conf.isChecked(), "knee": self.chk_lim_knee.isChecked(),
                  "pareto_bounds": self.chk_lim_pareto.isChecked(), "level": self.spin_level.value(),
                  "slope_tol": self.spin_slope_tol.value(), "maximise_y": self.chk_pareto_max.isChecked()}
        limit_style = {"color": self.btn_limit_color.color(), "linestyle": self._applied_combo_text(self.cb_limit_ls),
                       "linewidth": 1.4, "band_alpha": self.spin_band_alpha.value(),
                       "show_labels": self.chk_limit_labels.isChecked()}
        spec = PlotSpec(
            datasets=self.selected_datasets(),
            chart_type=self.cb_chart.currentText(),
            mappings={'x': self._applied_combo_data(self.cb_x), 'y': self._applied_combo_data(self.cb_y), 'z': self._applied_combo_data(self.cb_z),
                      'w': self._applied_combo_data(self.cb_w), 'v': self._applied_combo_data(self.cb_v),
                      'matrix': self._applied_combo_data(self.cb_matrix), 'gradient': self._applied_combo_data(self.cb_gradient)},
            secondary_y=self._applied_combo_data(self.cb_y2),
            expression=self.txt_expression.text().strip(),
            expression_domain=self.txt_expr_domain.text().strip(),
            colourmap=self._applied_combo_text(self.cb_colourmap),
            series_color=self.btn_series_color.color(),
            grid_visible=self.chk_bg_grid.isChecked(),
            show_legend=self.chk_legend.isChecked(),
            styling=copy.deepcopy(self.styling),
            clipping_ranges=clips,
            smoothing=self.slider_smoothing.value() / 10.0,
            x_scale=canonical_axis_scale(self._applied_combo_text(self.cb_x_scale)),
            y_scale=canonical_axis_scale(self._applied_combo_text(self.cb_y_scale)),
            z_scale=canonical_axis_scale(self._applied_combo_text(self.cb_z_scale)),
            estimator=canonical_estimator(self._applied_combo_text(self.cb_estimator)),
            bin_statistic=self._applied_combo_text(self.cb_bin_stat),
            grid_resolution=self.slider_res.value(),
            surface_neighbors=self.spin_surface_neighbors.value(),
            idw_power=self.spin_idw_power.value(),
            loess_fraction=self.spin_loess_fraction.value(),
            kriging_variogram=self._applied_combo_text(self.cb_kriging_variogram),
            surface_value_policy=self._applied_combo_text(self.cb_surface_value_policy),
            surface_extrapolation=self._applied_combo_text(self.cb_surface_extrapolation),
            invalid_mask_policy=self._applied_combo_text(self.cb_invalid_policy),
            failure_bridge_policy=self._applied_combo_text(self.cb_failure_bridge),
            failure_bridge_max_cells=self.spin_failure_bridge_cells.value(),
            show_imputed_cells=self.chk_show_imputed_cells.isChecked(),
            surface_response_space=self._applied_combo_text(self.cb_surface_response_space),
            surface_x_metric_weight=self.spin_surface_x_metric.value(),
            surface_y_metric_weight=self.spin_surface_y_metric.value(),
            invalid_data_mode=self._applied_combo_text(self.cb_invalid_mode),
            invalid_color=self.btn_invalid_color.color(),
            colorbar_extend=self._applied_combo_text(self.cb_colorbar_extend),
            field_render_mode=self._applied_combo_text(self.cb_field_render_mode),
            contour_levels=self.spin_contour_levels.value(),
            contour_overlay=self.chk_contour_overlay.isChecked(),
            contour_overlay_levels=self.spin_contour_overlay_levels.value(),
            contour_overlay_color=self.btn_contour_overlay_color.color(),
            contour_overlay_width=self.spin_contour_overlay_width.value(),
            contour_overlay_labels=self.chk_contour_overlay_labels.isChecked(),
            progressive_rendering=self.chk_progressive_surface.isChecked(),
            auto_surface_presentation=self.chk_auto_surface_presentation.isChecked(),
            pareto_options={'constrain': self.chk_pareto_constrain.isChecked(), 'maximise_y': self.chk_pareto_max.isChecked(),
                            'bootstrap': self.chk_pareto_boot.isChecked()},
            pareto_only=self.chk_pareto_only.isChecked(),
            font_size=self.slider_fontsize.value(),
            label_padding=self.slider_padding.value(),
            outlier_mask=self.chk_outlier_mask.isChecked(),
            mask_method=self._applied_combo_text(self.cb_mask_method),
            mask_min_neighbors=self.spin_min_neighbors.value(),
            fifth_axis_mode=self._applied_combo_text(self.cb_fifth_mode),
            fifth_axis_invert=self.chk_fifth_invert.isChecked(),
            limits=limits, limit_style=limit_style,
            experimental_overlay=self.current_experimental_overlay,
            literature_overlays=[dict(o) for o in self.literature_overlays],
        )
        # Interactive graph background is independently configurable from the
        # application chrome. Export remains publication-white unless the
        # export format itself is explicitly made transparent.
        spec.metadata = dict(spec.metadata or {})
        spec.metadata["ui_theme"] = self._graph_background_metadata()
        if self._surface_drag_active and self.chk_progressive_surface.isChecked():
            spec.metadata["progressive_surface_preview"] = True
        if prev is not None:   # carry canvas-local controls, markers and callouts across
            st = prev.local_state()
            # The bottom-drawer Axis clipping range sliders live on the canvas;
            # a sidebar-driven rebuild must not silently discard them.  Canvas
            # clips are the base; explicit sidebar spin-box clips win per axis.
            canvas_clips = dict(st.get("clips") or {})
            canvas_clips.update(spec.clipping_ranges)
            spec.clipping_ranges = canvas_clips
            spec.point_budget = int(st.get("point_budget", 0) or 0)
            spec.density_alpha = bool(st.get("density_alpha", False))
            spec.auto_aggregate = bool(st.get("auto_aggregate", False))
            spec.color_gate = bool(st.get("color_gate", False))
            spec.colourmap = self._applied_combo_text(self.cb_colourmap)
            spec.grid_visible = st.get("grid", spec.grid_visible)
            spec.surface_alpha = st.get("opacity", 1.0)
            spec.assumed_alpha = st.get("assumed_alpha", 0.28)
            spec.line_width = st.get("line_width", spec.line_width)
            spec.marker = st.get("marker", spec.marker)
            spec.animation_speed = st.get("animation_speed", spec.animation_speed)
            spec.animation_trail = st.get("animation_trail", spec.animation_trail)
            spec.linked_layers = bool(getattr(self, 'act_link_views', None) and self.act_link_views.isChecked())
            spec.noise_filter_enabled = st["noise"]; spec.noise_filter_method = st["noise_method"]
            spec.noise_filter_threshold = st["threshold"]
            spec.quiver_overlay = st["quiver"]; spec.quiver_type = st["quiver_type"]
            if not st["smooth_on"]:
                spec.smoothing = 0.0
            spec.annotations = [dict(a) for a in prev.spec.annotations]
            if prev.spec.metadata.get("data_markers"):
                spec.metadata["data_markers"] = [dict(m) for m in prev.spec.metadata.get("data_markers", [])]
        return spec

    # ------------------------------------------------------------ undo state
    def snapshot_ui_state(self) -> dict:
        st = {
            "chart": self.cb_chart.currentText(), "cmap": self._applied_combo_text(self.cb_colourmap),
            "axis_scales": {"x": self._applied_combo_text(self.cb_x_scale), "y": self._applied_combo_text(self.cb_y_scale), "z": self._applied_combo_text(self.cb_z_scale)},
            "map": {k: self._applied_combo_data(cb) for k, cb in (('x', self.cb_x), ('y', self.cb_y), ('z', self.cb_z), ('w', self.cb_w),
                                                       ('v', self.cb_v), ('matrix', self.cb_matrix), ('gradient', self.cb_gradient))},
            "secondary_y": self._applied_combo_data(self.cb_y2),
            "expression": self.txt_expression.text(), "expression_domain": self.txt_expr_domain.text(),
            "fifth": self._applied_combo_text(self.cb_fifth_mode),
            "fifth_invert": self.chk_fifth_invert.isChecked(),
            "clip": {k: (a.value(), b.value()) for k, (a, b) in {'x': (self.spin_xmin, self.spin_xmax), 'y': (self.spin_ymin, self.spin_ymax),
                                                                   'z': (self.spin_zmin, self.spin_zmax), 'w': (self.spin_wmin, self.spin_wmax),
                                                                   'v': (self.spin_vmin, self.spin_vmax)}.items()},
            "use_clip": self.chk_use_clip.isChecked(),
            "estimator": canonical_estimator(self._applied_combo_text(self.cb_estimator)), "bin_stat": self._applied_combo_text(self.cb_bin_stat),
            "surface_options": {
                "neighbors": self.spin_surface_neighbors.value(), "idw_power": self.spin_idw_power.value(),
                "loess_fraction": self.spin_loess_fraction.value(), "kriging_variogram": self._applied_combo_text(self.cb_kriging_variogram),
                "value_policy": self._applied_combo_text(self.cb_surface_value_policy), "response_space": self._applied_combo_text(self.cb_surface_response_space),
                "x_metric_weight": self.spin_surface_x_metric.value(), "y_metric_weight": self.spin_surface_y_metric.value(),
                "extrapolation": self._applied_combo_text(self.cb_surface_extrapolation),
                "invalid_policy": self._applied_combo_text(self.cb_invalid_policy), "failure_bridge": self._applied_combo_text(self.cb_failure_bridge),
                "failure_bridge_cells": self.spin_failure_bridge_cells.value(), "show_imputed_cells": self.chk_show_imputed_cells.isChecked(),
                "invalid_mode": self._applied_combo_text(self.cb_invalid_mode), "invalid_color": self.btn_invalid_color.color(),
                "colorbar_extend": self._applied_combo_text(self.cb_colorbar_extend), "field_render_mode": self._applied_combo_text(self.cb_field_render_mode),
                "contour_levels": self.spin_contour_levels.value(),
                "contour_overlay": self.chk_contour_overlay.isChecked(), "contour_overlay_levels": self.spin_contour_overlay_levels.value(),
                "contour_overlay_color": self.btn_contour_overlay_color.color(), "contour_overlay_width": self.spin_contour_overlay_width.value(),
                "contour_overlay_labels": self.chk_contour_overlay_labels.isChecked(),
                "progressive": self.chk_progressive_surface.isChecked(),
                "auto_presentation": self.chk_auto_surface_presentation.isChecked(),
            },
            "series_color": self.btn_series_color.color(), "res": self.slider_res.value(), "smooth": self.slider_smoothing.value(),
            "pareto": (self.chk_pareto_constrain.isChecked(), self.chk_pareto_max.isChecked(), self.chk_pareto_boot.isChecked(),
                       self.chk_pareto_only.isChecked()),
            "limits": (self.chk_lim_plateau.isChecked(), self.chk_lim_asym.isChecked(), self.chk_lim_conf.isChecked(),
                       self.chk_lim_knee.isChecked(), self.chk_lim_pareto.isChecked(), self.spin_level.value(),
                       self.spin_slope_tol.value(), self._applied_combo_text(self.cb_limit_ls), self.btn_limit_color.color(),
                       self.spin_band_alpha.value(), self.chk_limit_labels.isChecked()),
            "styling": copy.deepcopy(self.styling), "target": self.cb_target_element.currentText(),
            "font": self.slider_fontsize.value(), "pad": self.slider_padding.value(),
            "grid": self.chk_bg_grid.isChecked(), "legend": self.chk_legend.isChecked(),
            "outlier": self.chk_outlier_mask.isChecked(), "mask": self._applied_combo_text(self.cb_mask_method),
            "mask_min_neighbors": self.spin_min_neighbors.value(),
            "selected": [i.text() for i in self.list_datasets.selectedItems()],
            "overlays": copy.deepcopy(self.literature_overlays),
        }
        if hasattr(self, "preview_canvas"):
            st["canvas"] = self.preview_canvas.local_state()
            st["annotations"] = [dict(a) for a in self.preview_canvas.spec.annotations]
        return st

    def apply_ui_state(self, st: dict, *, render: bool = True):
        self._applying_state = True
        self.history.suspend()
        widgets = [self.cb_chart, self.cb_colourmap, self.cb_x, self.cb_y, self.cb_z, self.cb_x_scale, self.cb_y_scale, self.cb_z_scale,
                   self.cb_w, self.cb_v, self.cb_y2, self.cb_matrix, self.cb_gradient, self.cb_fifth_mode, self.chk_fifth_invert,
                   self.txt_expression, self.txt_expr_domain,
                   self.spin_xmin, self.spin_xmax, self.spin_ymin, self.spin_ymax,
                   self.spin_zmin, self.spin_zmax, self.spin_wmin, self.spin_wmax, self.spin_vmin, self.spin_vmax, self.chk_use_clip,
                   self.cb_estimator, self.cb_bin_stat, self.spin_surface_neighbors, self.spin_idw_power, self.spin_loess_fraction,
                   self.cb_kriging_variogram, self.cb_surface_value_policy, self.cb_surface_response_space, self.spin_surface_x_metric, self.spin_surface_y_metric, self.cb_surface_extrapolation, self.cb_invalid_policy,
                   self.cb_failure_bridge, self.spin_failure_bridge_cells, self.chk_show_imputed_cells, self.cb_invalid_mode, self.btn_invalid_color, self.cb_colorbar_extend,
                   self.cb_field_render_mode, self.spin_contour_levels, self.chk_contour_overlay, self.spin_contour_overlay_levels, self.btn_contour_overlay_color,
                   self.spin_contour_overlay_width, self.chk_contour_overlay_labels, self.chk_progressive_surface, self.chk_auto_surface_presentation,
                   self.btn_series_color, self.slider_res, self.slider_smoothing,
                   self.chk_pareto_constrain, self.chk_pareto_max, self.chk_pareto_boot, self.chk_pareto_only,
                   self.chk_lim_plateau, self.chk_lim_asym,
                   self.chk_lim_conf, self.chk_lim_knee, self.chk_lim_pareto, self.spin_level, self.spin_slope_tol, self.cb_limit_ls,
                   self.btn_limit_color, self.spin_band_alpha, self.chk_limit_labels, self.cb_target_element, self.slider_fontsize,
                   self.slider_padding, self.chk_bg_grid, self.chk_legend, self.chk_outlier_mask, self.cb_mask_method,
                   self.spin_min_neighbors, self.list_datasets]
        for w in widgets:
            w.blockSignals(True)
        try:
            self.list_datasets.clearSelection()
            for i in range(self.list_datasets.count()):
                if self.list_datasets.item(i).text() in st.get("selected", []):
                    self.list_datasets.item(i).setSelected(True)
            self.cb_chart.setCurrentText(st["chart"])
            self.lbl_chart_desc.setText(CHART_DESCRIPTIONS.get(st["chart"], ""))
            self.populate_axes(self.primary_dataset())
            self.cb_colourmap.setCurrentText(st["cmap"])
            scales = dict(st.get("axis_scales") or {})
            if not scales and (st.get("canvas") or {}).get("scale"):
                xs, ys, zs = legacy_scale_to_axes(st["canvas"]["scale"])
                scales = {"x": xs, "y": ys, "z": zs}
            self.cb_x_scale.setCurrentText(canonical_axis_scale(scales.get("x", "Linear")))
            self.cb_y_scale.setCurrentText(canonical_axis_scale(scales.get("y", "Linear")))
            self.cb_z_scale.setCurrentText(canonical_axis_scale(scales.get("z", "Linear")))
            for k, cb in (('x', self.cb_x), ('y', self.cb_y), ('z', self.cb_z), ('w', self.cb_w), ('v', self.cb_v),
                          ('matrix', self.cb_matrix), ('gradient', self.cb_gradient)):
                cb.blockSignals(True)
                self._combo_set_data(cb, st.get("map", {}).get(k))
            self._combo_set_data(self.cb_y2, st.get("secondary_y"))
            self.txt_expression.setText(st.get("expression", self.txt_expression.text()))
            self.txt_expr_domain.setText(st.get("expression_domain", self.txt_expr_domain.text()))
            self.cb_fifth_mode.setCurrentText(st.get("fifth", self.cb_fifth_mode.currentText()))
            self.chk_fifth_invert.setChecked(bool(st.get("fifth_invert", False)))
            for k, (a, b) in {'x': (self.spin_xmin, self.spin_xmax), 'y': (self.spin_ymin, self.spin_ymax), 'z': (self.spin_zmin, self.spin_zmax),
                              'w': (self.spin_wmin, self.spin_wmax), 'v': (self.spin_vmin, self.spin_vmax)}.items():
                lo, hi = st["clip"].get(k, (0.0, 100.0))
                a.setValue(lo); b.setValue(hi)
            self.chk_use_clip.setChecked(st["use_clip"])
            self.cb_estimator.setCurrentText(canonical_estimator(st.get("estimator", "Auto (data-aware)"))); self.cb_bin_stat.setCurrentText(st.get("bin_stat", "mean"))
            surf = dict(st.get("surface_options") or {})
            self.spin_surface_neighbors.setValue(int(surf.get("neighbors", 32)))
            self.spin_idw_power.setValue(float(surf.get("idw_power", 2.0)))
            self.spin_loess_fraction.setValue(float(surf.get("loess_fraction", 0.25)))
            self.cb_kriging_variogram.setCurrentText(str(surf.get("kriging_variogram", "Exponential")))
            self.cb_surface_value_policy.setCurrentText(str(surf.get("value_policy", "Allow estimator overshoot")))
            self.cb_surface_response_space.setCurrentText(str(surf.get("response_space", "Linear values")))
            self.spin_surface_x_metric.setValue(float(surf.get("x_metric_weight", 1.0)))
            self.spin_surface_y_metric.setValue(float(surf.get("y_metric_weight", 1.0)))
            self.cb_surface_extrapolation.setCurrentText(str(surf.get("extrapolation", "Mask outside convex hull")))
            self.cb_invalid_policy.setCurrentText(str(surf.get("invalid_policy", "Sample cell only")))
            self.cb_failure_bridge.setCurrentText(str(surf.get("failure_bridge", "Bridge isolated failures")))
            self.spin_failure_bridge_cells.setValue(int(surf.get("failure_bridge_cells", 4)))
            self.chk_show_imputed_cells.setChecked(bool(surf.get("show_imputed_cells", False)))
            self.cb_invalid_mode.setCurrentText(str(surf.get("invalid_mode", "Transparent")))
            self.btn_invalid_color.setColor(str(surf.get("invalid_color", "#DDDDDD")), emit=False)
            self.cb_colorbar_extend.setCurrentText(str(surf.get("colorbar_extend", "Auto (from clipping)")))
            self.cb_field_render_mode.setCurrentText(str(surf.get("field_render_mode", "Continuous shading")))
            self.spin_contour_levels.setValue(int(surf.get("contour_levels", 10)))
            self.chk_contour_overlay.setChecked(bool(surf.get("contour_overlay", False)))
            self.spin_contour_overlay_levels.setValue(int(surf.get("contour_overlay_levels", 10)))
            self.btn_contour_overlay_color.setColor(str(surf.get("contour_overlay_color", "#FFFFFF")), emit=False)
            self.spin_contour_overlay_width.setValue(float(surf.get("contour_overlay_width", 0.6)))
            self.chk_contour_overlay_labels.setChecked(bool(surf.get("contour_overlay_labels", False)))
            self.chk_progressive_surface.setChecked(bool(surf.get("progressive", True)))
            self.chk_auto_surface_presentation.setChecked(bool(surf.get("auto_presentation", True)))
            self.btn_series_color.setColor(st["series_color"], emit=False)
            self.slider_res.setValue(st["res"]); self.slider_smoothing.setValue(st["smooth"])
            self._sync_surface_estimator_controls()
            pareto_state = st.get("pareto", (True, True, True, False))
            for chk, val in zip((self.chk_pareto_constrain, self.chk_pareto_max, self.chk_pareto_boot, self.chk_pareto_only), pareto_state):
                chk.setChecked(val)
            lim = st["limits"]
            for chk, val in zip((self.chk_lim_plateau, self.chk_lim_asym, self.chk_lim_conf, self.chk_lim_knee, self.chk_lim_pareto), lim[:5]):
                chk.setChecked(val)
            self.spin_level.setValue(lim[5]); self.spin_slope_tol.setValue(lim[6]); self.cb_limit_ls.setCurrentText(lim[7])
            self.btn_limit_color.setColor(lim[8], emit=False); self.spin_band_alpha.setValue(lim[9]); self.chk_limit_labels.setChecked(lim[10])
            self.styling = copy.deepcopy(st["styling"])
            self.cb_target_element.setCurrentText(st["target"])
            self._load_style_target()
            self.slider_fontsize.setValue(st["font"]); self.slider_padding.setValue(st["pad"])
            self.chk_bg_grid.setChecked(st["grid"]); self.chk_legend.setChecked(st["legend"])
            self.chk_outlier_mask.setChecked(st["outlier"]); self.cb_mask_method.setCurrentText(st["mask"])
            self.spin_min_neighbors.setValue(int(st.get("mask_min_neighbors", 8)))
            self.literature_overlays = copy.deepcopy(st.get("overlays", []))
            if "canvas" in st:
                self.preview_canvas.apply_local_state(st["canvas"])
                self.preview_canvas.spec.annotations = [dict(a) for a in st.get("annotations", [])]
        finally:
            for w in widgets:
                w.blockSignals(False)
            self._sync_all_staged_combos()
            self._pending_graph_entry = None
            if hasattr(self, "btn_apply_graph_choice"):
                self.btn_apply_graph_choice.setEnabled(False); self.btn_apply_graph_choice.setText("▶  Apply selected visualisation")
            self._apply_axis_roles()
            self.history.resume()
            self.history.baseline(self.snapshot_ui_state())
            self._applying_state = False
        if render:
            self.generate_preview()
            self.statusBar().showMessage("History state applied", 2000)

    # ------------------------------------------------------------ rendering
    def _start_surface_slider_drag(self):
        self._surface_drag_active = bool(self.chk_progressive_surface.isChecked())

    def _queue_surface_slider_preview(self, *_):
        sender = self.sender()
        if (self.chk_progressive_surface.isChecked() and isinstance(sender, QSlider) and sender.isSliderDown()):
            self._surface_drag_active = True
            self._surface_drag_debounce.start()
            return
        self._queue_render_if_slider_idle()

    def _render_progressive_surface_preview(self):
        if self._applying_state or not self._surface_drag_active or not self.chk_progressive_surface.isChecked():
            return
        # Do not record every intermediate handle position in undo history.
        # build_spec() tags this render so the backend caps both point count and
        # output grid while preserving the selected estimator/log metric.
        self.generate_preview()

    def _finish_surface_slider_drag(self):
        self._surface_drag_debounce.stop()
        self._surface_drag_active = False
        # Final render uses the full requested surface resolution.
        self.queue_render()

    def _queue_render_if_slider_idle(self, *_):
        sender = self.sender()
        if isinstance(sender, QSlider) and sender.isSliderDown():
            return
        self.queue_render()

    def queue_render(self, *_):
        if self._applying_state:
            return
        self.watchdog.pulse()
        self._debounce.start()

    def _on_debounce(self):
        self.history.record(self.snapshot_ui_state(), "Adjust plot")
        self.generate_preview()

    def generate_preview(self):
        if not self.selected_datasets() and self.cb_chart.currentText() not in EXPRESSION_CHARTS:
            return
        canvas = self._ensure_preview_tab()
        canvas.update_spec(self.build_spec())

    def _on_canvas_spec_changed(self, spec: PlotSpec):
        """Canvas-local edits (sliders, drawer, callouts) → keep sidebar + history in sync."""
        if self._applying_state:
            return
        self.cb_colourmap.blockSignals(True)
        self.cb_colourmap.setCurrentText(spec.colourmap)
        self.cb_colourmap.blockSignals(False)
        for combo, value in ((self.cb_x_scale, spec.x_scale), (self.cb_y_scale, spec.y_scale), (self.cb_z_scale, spec.z_scale)):
            combo.blockSignals(True)
            combo.setCurrentText(canonical_axis_scale(value))
            combo.blockSignals(False)
        self.chk_bg_grid.blockSignals(True)
        self.chk_bg_grid.setChecked(spec.grid_visible)
        self.chk_bg_grid.blockSignals(False)
        self.history.record(self.snapshot_ui_state(), "Canvas adjustment")

    def _init_preview_tab(self):
        # Construct the reusable preview editor without attaching it to the tab
        # widget.  The visible workspace therefore starts as a completely empty
        # dark stage.  The editor is attached only after an explicit Generate /
        # Preview action or a graph selection that requests generation.
        self.preview_canvas = ScientificPlotCanvas(self.build_spec(), title="Workspace", autorender=False)
        self._wire_canvas(self.preview_canvas)
        self.preview_canvas.overlay.cancel_requested.connect(self._cancel_active_background_task)
        self.preview_canvas.spec_changed.connect(self._on_canvas_spec_changed)

    def _ensure_preview_tab(self) -> ScientificPlotCanvas:
        idx = self.tabs.indexOf(self.preview_canvas)
        if idx < 0:
            idx = self.tabs.addTab(self.preview_canvas, "Workspace")
            self._set_tab_active_checkbox(idx, True)
        self.tabs.setCurrentIndex(idx)
        self.stage_stack.setCurrentWidget(self.right_split)
        return self.preview_canvas

    def _operation_overlay(self) -> LoadingOverlay:
        current = self.tabs.currentWidget() if hasattr(self, "tabs") else None
        if isinstance(current, ScientificPlotCanvas):
            return current.overlay
        return self.global_overlay

    def clear_workspace_view(self) -> None:
        """Hide all graph tabs without deleting project data or saved sessions."""
        for i in range(self.tabs.count() - 1, -1, -1):
            w = self.tabs.widget(i)
            if isinstance(w, ScientificPlotCanvas):
                w.cancel_active_render()
            self.tabs.removeTab(i)
            if isinstance(w, ScientificPlotCanvas) and w is not self.preview_canvas:
                w.deleteLater()
        self.stage_stack.setCurrentWidget(self.blank_stage)
        self.save_session()
        self.statusBar().showMessage("Workspace cleared — select a graph to generate when ready.", 3500)

    def _wire_canvas(self, canvas: ScientificPlotCanvas):
        canvas.preview_requested.connect(self.generate_preview)
        canvas.auto_render_requested.connect(self.auto_render_current_chart)
        canvas.status_message.connect(lambda m: self.statusBar().showMessage(m, 6000))
        canvas.analysis_summary.connect(self._on_analysis_summary)
        canvas.render_finished.connect(lambda r: self.pill_render.set_state("idle", "#7F8C8D"))
        canvas.render_finished.connect(self._remember_good_ui_state)
        if hasattr(canvas, "render_reverted"):
            canvas.render_reverted.connect(lambda src=canvas: self._on_render_reverted(src))
        if hasattr(canvas, 'view_state_changed'):
            canvas.view_state_changed.connect(lambda state, src=canvas: self._sync_linked_views(src, state))
        canvas.overlay._timer.timeout.connect(lambda: None)

    def _remember_good_ui_state(self, _result=None):
        """Record the sidebar state that produced a successful render."""
        if self.sender() is not self.current_canvas():
            return
        try:
            self._last_good_ui_state = self.snapshot_ui_state()
        except Exception:
            pass

    def _on_render_reverted(self, canvas):
        """A render was cancelled or failed: roll pending sidebar inputs back
        to the last confirmed applied state so the controls always describe the
        figure actually on screen."""
        if canvas is not self.current_canvas():
            return
        self._debounce.stop()
        self._staged_commit_timer.stop()
        if self._last_good_ui_state is not None:
            try:
                self.apply_ui_state(copy.deepcopy(self._last_good_ui_state), render=False)
            except Exception:
                self._revert_staged_combos()
        else:
            self._revert_staged_combos()
        self.statusBar().showMessage("Render stopped — controls reverted to the last applied state.", 5000)

    def _sync_linked_views(self, source, state):
        if not getattr(self, 'act_link_views', None) or not self.act_link_views.isChecked():
            return
        for i in range(self.tabs.count()):
            canvas = self.tabs.widget(i)
            if isinstance(canvas, ScientificPlotCanvas) and canvas is not source and hasattr(canvas, 'apply_external_view'):
                canvas.apply_external_view(state)

    def _on_analysis_summary(self, text):
        if self.sender() is self.current_canvas():
            self.txt_limits.setPlainText(text)

    def current_canvas(self) -> ScientificPlotCanvas:
        w = self.tabs.currentWidget()
        return w if isinstance(w, ScientificPlotCanvas) else self.preview_canvas

    def _on_tab_changed(self, idx):
        c = self.current_canvas()
        if bool(c.property("graphvis_lazy_render")) and c.last_result is None:
            c.setProperty("graphvis_lazy_render", False)
            QTimer.singleShot(0, c.request_render)
        if c.last_result is not None and c.last_result.limit_report is not None:
            self.txt_limits.setPlainText(c.last_result.limit_report.text())

    def _add_tab(self, spec: PlotSpec, name: str, *, autorender: bool = True):
        canvas = ScientificPlotCanvas(spec, title=name, autorender=autorender)
        canvas.setProperty("graphvis_lazy_render", not autorender)
        self._wire_canvas(canvas)
        canvas.set_drawer_visible(self.btn_mode_advanced.isChecked())
        idx = self.tabs.addTab(canvas, name[:32])
        self._set_tab_active_checkbox(idx, True)
        self.tabs.setCurrentWidget(canvas)
        self.stage_stack.setCurrentWidget(self.right_split)
        self.save_session()
        self.refresh_object_manager()
        return canvas

    def save_graph_to_tab(self):
        spec = self.build_spec()
        n = self.tabs.count()
        self._add_tab(spec, f"{spec.chart_type} ({n})")
        self.statusBar().showMessage(f"Saved graph tab: {spec.chart_type} ({n})", 3000)

    def close_tab(self, index):
        if index < 0 or index >= self.tabs.count():
            return
        w = self.tabs.widget(index)
        if isinstance(w, ScientificPlotCanvas):
            w.cancel_active_render()
        self.tabs.removeTab(index)
        if isinstance(w, ScientificPlotCanvas) and w is not self.preview_canvas:
            w.deleteLater()
        if self.tabs.count() == 0:
            self.stage_stack.setCurrentWidget(self.blank_stage)
        self.save_session()
        self.refresh_object_manager()

    def export_current(self):
        self.current_canvas().export_figure()

    def open_alias_editor(self):
        dlg = AliasEditorDialog(self)
        if dlg.exec():
            self.populate_axes(self.primary_dataset())
            self.queue_render()

    def auto_generate_smart_suite(self):
        """Analyse the active project group off-thread and build a diverse graph plan."""
        group = self.project_context.get_group() if hasattr(self, "project_context") else None
        group_datasets = [self.datasets[n] for n in (group.datasets if group else []) if n in self.datasets]
        if not group_datasets:
            ds = self.primary_dataset(); group_datasets = [ds] if ds is not None else []
        if not group_datasets:
            return QMessageBox.warning(self, "No dataset",
                "Select a dataset, or link project datasets to the active literature group first.")
        literature = self._active_group_literature()
        budget = float(self._smart_budget_seconds())
        self.settings.setValue("advisor/time_budget_seconds", int(budget))
        self.pill_render.set_state("Smart Suite analysing", "#2471A3")
        self.statusBar().showMessage(f"Smart Suite analysing {len(group_datasets)} dataset(s), budget {int(budget)} s…", 0)
        # The logo overlay follows the global >5 s rule; fast scans finish before it appears.
        self._operation_overlay().start("Smart Suite — analysing data and literature…", 1, cancellable=True)
        ai_advice = self.project_context.load_ai_advice(group.name if group else None) if hasattr(self, "project_context") else {}
        task = SmartSuiteTask(group_datasets, literature, str(self.scan_cache_dir), budget, ai_advice=ai_advice)
        self._set_active_background_task(task, "Smart Suite analysis running…")
        task.signals.progress.connect(lambda msg, pct: self._operation_overlay().set_message(msg, pct))
        task.signals.finished.connect(lambda result, group_name=(group.name if group else "current selection"): self._finish_smart_suite(result, group_name))
        task.signals.failed.connect(self._smart_suite_failed)
        task.signals.cancelled.connect(self._on_background_cancelled)
        submit(task)

    def _finish_smart_suite(self, result: dict, group_name: str) -> None:
        self._operation_overlay().stop(); self._clear_active_background_task(); self.pill_render.set_state("idle", "#7F8C8D")
        for name, scan in (result.get("scans") or {}).items():
            self._scan_results[name] = scan
        recs = list(result.get("recommendations") or [])
        if not recs:
            return QMessageBox.information(self, "Smart Suite", "The analysis completed but found no compatible graph plan.")
        built = 0
        for rec in recs:
            ds = self.datasets.get(str(rec.get("dataset_name", "")))
            graph = str(rec.get("graph", ""))
            if ds is None or graph not in CHART_TYPES or not chart_capabilities(ds).get(graph, (True, ""))[0]:
                continue
            spec = self.build_spec()
            spec.datasets = {ds.name: ds}; spec.chart_type = graph
            spec.mappings = {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None}
            spec.mappings.update({k: v for k, v in (rec.get("mappings") or {}).items() if k in spec.mappings})
            diagnostics = rec.get("diagnostics") or {}
            if diagnostics.get("recommended_colourmap"):
                spec.colourmap = str(diagnostics["recommended_colourmap"])
            if diagnostics.get("recommended_series_color"):
                spec.series_color = str(diagnostics["recommended_series_color"])
            axis_scales = diagnostics.get("recommended_axis_scales") or {}
            if axis_scales:
                spec.x_scale = canonical_axis_scale(axis_scales.get("x", spec.x_scale))
                spec.y_scale = canonical_axis_scale(axis_scales.get("y", spec.y_scale))
                spec.z_scale = canonical_axis_scale(axis_scales.get("z", spec.z_scale))
            elif diagnostics.get("recommended_scale"):
                spec.x_scale, spec.y_scale, spec.z_scale = legacy_scale_to_axes(diagnostics["recommended_scale"])
            spec.metadata = dict(getattr(spec, "metadata", {}) or {})
            spec.metadata.update({
                "smart_map_reason": rec.get("reason", ""),
                "project_group": group_name,
                "smart_score": float(rec.get("score", 0.0)),
                "smart_budget_seconds": float(result.get("budget_seconds", 0.0)),
            })
            # Only the best graph renders immediately. Other recommended tabs
            # are lazy and render when selected, avoiding a post-scan render queue.
            self._add_tab(spec, f"{graph} — {ds.name}", autorender=(built == 0))
            built += 1
            if built >= 6:
                break
        self._refresh_recommendations()
        self.statusBar().showMessage(f"Smart Suite created {built} context-aware graph(s) for '{group_name}'.", 7000)

    def _smart_suite_failed(self, msg: str) -> None:
        self._operation_overlay().stop(); self._clear_active_background_task(); self.pill_render.set_state("idle", "#7F8C8D")
        log_line(f"Smart Suite failed: {msg}", "SMART")
        QMessageBox.critical(self, "Smart Suite failed", msg.splitlines()[0])

    # ------------------------------------------------------------ professional-suite tools
    def refresh_object_manager(self) -> None:
        if hasattr(self, "object_manager"):
            self.object_manager.refresh(self.project.name, self.datasets, self.tabs, self.project_context)

    def open_data_connector(self) -> None:
        if self.controller is None:
            return self.show_error("Controller unavailable", "The MVC controller is not attached.")
        dlg = DataConnectorDialog(self.controller, self)
        if dlg.exec():
            self.refresh_object_manager()
            self.populate_axes(self.primary_dataset())
            self.queue_render()

    def refresh_live_connectors(self) -> None:
        if self.controller is None:
            return
        count = 0
        now = time.monotonic()
        for profile in list(self.app_model.connectors.profiles.values()):
            if self.controller.import_connector(profile) is not None:
                count += 1
                self._connector_last_refresh[profile.name] = now
        self.statusBar().showMessage(f"Refreshed {count} data connector(s).", 5000)

    def _refresh_due_connectors(self) -> None:
        """Refresh scheduled live connectors without blocking file telemetry semantics."""
        if not self.live_telemetry_enabled or self.controller is None:
            return
        now = time.monotonic()
        for profile in list(self.app_model.connectors.profiles.values()):
            interval = max(0.0, float(profile.refresh_seconds or 0.0))
            if interval <= 0:
                continue
            if now - self._connector_last_refresh.get(profile.name, 0.0) < interval:
                continue
            self._connector_last_refresh[profile.name] = now
            # Keep UI responsive: connector imports are normally short; failures are
            # caught by the controller and logged persistently.
            self.controller.import_connector(profile)
        self.refresh_object_manager()

    def load_excel_workbook_all_sheets(self) -> None:
        """Persist an Excel workbook and register every worksheet as a dataset."""
        path, _ = QFileDialog.getOpenFileName(self, "Load Excel workbook", self.dataset_dir, "Excel workbooks (*.xlsx *.xls *.xlsm)")
        if not path:
            return
        try:
            project_path = self.project.ingest_file(path, "datasets")
            sheets = load_excel_workbook(project_path)
            for ds in sheets.values():
                ds.meta.setdefault("source_external", os.path.abspath(path))
                if self.controller is not None:
                    self.controller.register_loaded_dataset(ds)
                self.dataset_registered_from_controller(ds)
            self.refresh_object_manager()
            self.populate_axes(self.primary_dataset())
            self.queue_render()
            self.statusBar().showMessage(f"Loaded {len(sheets)} worksheet(s) from {os.path.basename(path)}", 5000)
        except Exception as exc:
            log_line(f"Workbook import failed: {path}: {exc}", "LOAD")
            QMessageBox.critical(self, "Workbook import failed", f"{type(exc).__name__}: {exc}")

    def open_spreadsheet_tools(self) -> None:
        ds = self.primary_dataset()
        if ds is None:
            return QMessageBox.information(self, "Workbook", "Load/select a dataset first.")
        if self.controller is None:
            return self.show_error("Controller unavailable", "The MVC controller is not attached.")
        SpreadsheetDialog(ds, self.controller, self).exec()
        self.populate_axes(ds)
        self.queue_render()

    def open_analysis_hub(self) -> None:
        ds = self.primary_dataset()
        if ds is None:
            return QMessageBox.information(self, "Analysis Hub", "Load/select a dataset first.")
        def add_series(name: str, values: np.ndarray) -> None:
            if self.controller is not None:
                base = name
                candidate = base
                i = 2
                while candidate in ds.df.columns:
                    candidate = f"{base}_{i}"; i += 1
                self.controller.add_derived_series(ds.name, candidate, values)
                self.populate_axes(ds)
                self.queue_render()
        AnalysisHubDialog(ds, add_series, self).exec()

    def open_predictive_advisor(self) -> None:
        ds = self.primary_dataset()
        if ds is None:
            return QMessageBox.information(self, "Predictive Modeling Advisor", "Load/select a dataset first.")
        try:
            advice = self.controller.run_multivariate("predictive_advisor", ds.df) if self.controller is not None else []
            QMessageBox.information(self, "Predictive Modeling Advisor", "\n\n".join(f"{name}: {why}" for name, why in advice) or "No strong recommendation for the current data structure.")
        except Exception as exc:
            QMessageBox.warning(self, "Advisor failed", str(exc))

    def open_batch_processor(self) -> None:
        BatchProcessingDialog(self).exec()

    def open_workflow_builder(self) -> None:
        ds=self.primary_dataset()
        if ds is None:
            return QMessageBox.information(self,"Workflow Builder","Load/select a dataset first.")
        WorkflowBuilderDialog(ds,self.app_model.workflows,self).exec()

    def open_python_console(self) -> None:
        from graphvis.automation.console import PythonConsoleDialog
        namespace = {"app": self, "model": self.app_model, "controller": self.controller,
                     "datasets": self.datasets, "np": np, "pd": pd}
        dlg = PythonConsoleDialog(namespace, self)
        dlg.setAttribute(Qt.WA_DeleteOnClose, True)
        dlg.show()
        self._python_console = dlg

    def open_r_console(self) -> None:
        from graphvis.automation.console import RConsoleDialog
        dlg = RConsoleDialog(self)
        dlg.setAttribute(Qt.WA_DeleteOnClose, True)
        dlg.show()
        self._r_console = dlg

    def pop_out_current_graph(self) -> None:
        canvas = self.current_canvas()
        if canvas is None:
            return
        win = QMainWindow()
        win.setWindowTitle(f"GraphVis — {self.tabs.tabText(self.tabs.currentIndex())}")
        clone = ScientificPlotCanvas(canvas.spec.clone(), title=self.tabs.tabText(self.tabs.currentIndex()))
        win.setCentralWidget(clone)
        win.resize(1100, 780)
        win.show()
        self._floating_windows.append(win)
        win.destroyed.connect(lambda *_: self._floating_windows.remove(win) if win in self._floating_windows else None)

    def clone_active_project(self) -> None:
        name, ok = QInputDialog.getText(self, "Clone project", "New project name:", text=f"{self.project.name}_Copy")
        if not ok or not name.strip():
            return
        from pathlib import Path
        from graphvis.core.workspace import PROJECTS_ROOT, open_project
        target = PROJECTS_ROOT / "".join(c if c.isalnum() or c in "-_ ." else "_" for c in name.strip())
        if target.exists():
            return QMessageBox.warning(self, "Clone project", f"Target already exists:\n{target}")
        shutil.copytree(self.project.root, target, ignore=shutil.ignore_patterns(".cache", "*.log"))
        QMessageBox.information(self, "Project cloned", f"Created:\n{target}\n\nUse File → Project workspace → Open project folder to switch to it.")

    def open_workflow_folder(self) -> None:
        from PySide6.QtCore import QUrl
        from PySide6.QtGui import QDesktopServices
        self.project.workflows_dir.mkdir(parents=True, exist_ok=True)
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.project.workflows_dir)))

    # ------------------------------------------------------------ session cache
    def save_session(self):
        try:
            tabs = []
            for i in range(self.tabs.count()):
                w = self.tabs.widget(i)
                if isinstance(w, ScientificPlotCanvas):
                    tabs.append({"title": self.tabs.tabText(i), "spec": w.spec.to_dict(), "active": self._tab_is_active(i)})
            payload = {"tabs": tabs, "advanced": self.btn_mode_advanced.isChecked(),
                       "preview_active": (self.tabs.indexOf(self.preview_canvas) >= 0 and self._tab_is_active(self.tabs.indexOf(self.preview_canvas))),
                       "sidebar_sizes": self.splitter.sizes(), "state": self.snapshot_ui_state()}
            with open(SESSION_FILE, "w", encoding="utf-8") as fh:
                json.dump(payload, fh, default=str)
        except Exception as exc:
            log_line(f"Session save failed: {exc}", "CACHE")

    def _maybe_restore_session(self):
        """Do not auto-restore graph tabs at startup.

        Restoring rendered tabs was one of the largest avoidable startup costs.
        The session file is still saved and can be restored explicitly from the
        File menu via :meth:`restore_previous_graph_tabs`.
        """
        self._session_restored = True

    def restore_previous_graph_tabs(self):
        if not os.path.exists(SESSION_FILE):
            return QMessageBox.information(self, "Restore graph tabs", "No saved graph-tab session is available.")
        try:
            with open(SESSION_FILE, encoding="utf-8") as fh:
                payload = json.load(fh)
        except Exception as exc:
            log_line(f"Session restore failed: {exc}", "CACHE")
            return QMessageBox.warning(self, "Restore graph tabs", str(exc))
        restored = 0
        for t in payload.get("tabs", []):
            try:
                spec = PlotSpec.from_dict(t["spec"], registry=self.datasets)
                if not spec.datasets and spec.chart_type not in EXPRESSION_CHARTS:
                    continue
                canvas = ScientificPlotCanvas(spec, title=t["title"])
                self._wire_canvas(canvas)
                canvas.set_drawer_visible(self.btn_mode_advanced.isChecked())
                idx = self.tabs.addTab(canvas, t["title"])
                self._set_tab_active_checkbox(idx, bool(t.get("active", True)))
                restored += 1
            except Exception as exc:
                log_line(f"Tab restore skipped: {exc}", "CACHE")
        if restored:
            self.stage_stack.setCurrentWidget(self.right_split)
        self.statusBar().showMessage(f"Restored {restored} saved graph tab(s).", 5000)

    def closeEvent(self, ev):
        self.save_session()
        self.settings.setValue("ui/sidebar_width", self.splitter.currentSidebarWidth())
        self.settings.setValue("ui/sidebar_collapsed", self.splitter.isSidebarCollapsed())
        try:
            detached = [getattr(p, "_title", "") for p in getattr(self, "_detachable_sections", [])
                        if getattr(p, "_float_dock", None) is not None]
            self.settings.setValue("ui/detached_panels", json.dumps([x for x in detached if x]))
            self.settings.setValue("ui/mainwindow_geometry", self.saveGeometry())
            self.settings.setValue("ui/mainwindow_state", self.saveState())
        except Exception as exc:
            log_line(f"Dock layout save skipped: {exc}", "UI")
        # Remove the legacy absolute-size setting so it cannot reintroduce the
        # old tiny/huge sidebar behaviour on another display.
        self.settings.remove("ui/sidebar_sizes")
        self.settings.sync()
        self.watchdog.stop()
        log_line("Application closed.", "INFO")
        super().closeEvent(ev)


