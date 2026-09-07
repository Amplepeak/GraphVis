//! Domain-neutral GraphVis application model.
//!
//! This crate owns application state, dataset metadata, plot specifications,
//! command history, and renderer-neutral scientific mappings.  It contains no
//! thesis/project-specific variable aliases and does not depend on Python.

use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use std::{collections::HashMap, sync::Arc};
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum AxisScale {
    Linear,
    Log10,
    Ln,
    Log2,
    SymLog { linear_threshold: f64 },
    Asinh { scale: f64 },
    Reciprocal,
    Power { exponent: f64 },
    Logit,
    Probit,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct AxisRange {
    pub min: f64,
    pub max: f64,
    pub auto_expand: bool,
    pub padding_fraction: f64,
}

// AxisRange::sanitized() lived here and had no caller. The axis limits the user
// sets are the C++ canvas's own - PlotCanvas applies them when it paints - so
// PlotSpec::ranges is only ever carried through the state and written to the
// saved document; the field stays for that, the padding logic that nothing
// reached does not.

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct VariableMapping {
    pub x: Option<String>,
    pub y: Option<String>,
    pub z: Option<String>,
    pub color: Option<String>,
    pub fifth: Option<String>,
    pub fifth_role: Option<String>,
    pub invert_opacity: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PlotSpec {
    pub id: Uuid,
    pub kind: String,
    pub title: String,
    pub dataset_id: Uuid,
    pub mapping: VariableMapping,
    pub axis_scales: [AxisScale; 3],
    pub ranges: [Option<AxisRange>; 3],
    pub colormap: String,
    pub point_size: f32,
    pub opacity: f32,
    pub aggregation: String,
    pub pbr: PbrSettings,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PbrSettings {
    pub roughness: f32,
    pub metallic: f32,
    pub specular: f32,
    pub normal_strength: f32,
}

impl Default for PbrSettings {
    fn default() -> Self {
        Self { roughness: 0.35, metallic: 0.0, specular: 0.45, normal_strength: 0.0 }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DatasetMeta {
    pub id: Uuid,
    pub name: String,
    pub source: String,
    pub arrow_ipc_path: String,
    pub rows: usize,
    pub columns: Vec<String>,
    pub numeric_columns: Vec<String>,
    pub units: HashMap<String, String>,
}

// clippy::large_enum_variant fires here: AddDataset and SetPlot carry whole
// structs, so every Command is as large as the biggest of them. Boxing them
// would shrink the enum and is the usual fix - but it is the wrong one here.
// A Command is never stored: it is built, passed to execute(), applied to a
// snapshot and dropped. The history keeps AppSnapshot, not Command. Boxing
// would trade one stack copy of a short-lived value for a heap allocation and
// a free on every single user action, which is slower, not faster.
#[allow(clippy::large_enum_variant)]
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Command {
    AddDataset(DatasetMeta),
    SetActiveDataset(Option<Uuid>),
    SetPlot(PlotSpec),
    RemovePlot(Uuid),
    SetWorkspaceName(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppSnapshot {
    pub workspace_name: String,
    pub datasets: Vec<DatasetMeta>,
    pub active_dataset: Option<Uuid>,
    pub plots: Vec<PlotSpec>,
}

impl Default for AppSnapshot {
    fn default() -> Self {
        Self { workspace_name: "Untitled".into(), datasets: vec![], active_dataset: None, plots: vec![] }
    }
}

#[derive(Default)]
struct History {
    undo: Vec<AppSnapshot>,
    redo: Vec<AppSnapshot>,
}

#[derive(Clone, Default)]
pub struct AppState {
    inner: Arc<RwLock<AppSnapshot>>,
    history: Arc<RwLock<History>>,
}

impl AppState {
    pub fn snapshot(&self) -> AppSnapshot { self.inner.read().clone() }

    fn replace(&self, next: AppSnapshot, record_history: bool) {
        if record_history {
            let current = self.snapshot();
            let mut h = self.history.write();
            h.undo.push(current);
            h.redo.clear();
        }
        *self.inner.write() = next;
    }

    pub fn execute(&self, command: Command) {
        let mut next = self.snapshot();
        match command {
            Command::AddDataset(dataset) => {
                if next.datasets.iter().all(|d| d.id != dataset.id) { next.datasets.push(dataset.clone()); }
                next.active_dataset = Some(dataset.id);
            }
            Command::SetActiveDataset(id) => next.active_dataset = id,
            Command::SetPlot(plot) => match next.plots.iter().position(|p| p.id == plot.id) {
                Some(i) => next.plots[i] = plot,
                None => next.plots.push(plot),
            },
            Command::RemovePlot(id) => next.plots.retain(|p| p.id != id),
            Command::SetWorkspaceName(name) => next.workspace_name = name,
        }
        self.replace(next, true);
    }

    pub fn add_dataset(&self, dataset: DatasetMeta) { self.execute(Command::AddDataset(dataset)); }
    pub fn set_plot(&self, plot: PlotSpec) { self.execute(Command::SetPlot(plot)); }

    pub fn set_mapping(&self, dataset_id: Uuid, mapping: VariableMapping, point_size: f32, opacity: f32, aggregation: String) -> Uuid {
        let current = self.snapshot();
        let existing = current.plots.iter().find(|p| p.dataset_id == dataset_id && p.kind == "point-cloud").cloned();
        let id = existing.as_ref().map(|p| p.id).unwrap_or_else(Uuid::new_v4);
        let plot = PlotSpec {
            id,
            kind: "point-cloud".into(),
            title: existing.as_ref().map(|p| p.title.clone()).unwrap_or_else(|| "Scientific point cloud".into()),
            dataset_id,
            mapping,
            axis_scales: existing.as_ref().map(|p| p.axis_scales.clone()).unwrap_or([AxisScale::Linear, AxisScale::Linear, AxisScale::Linear]),
            ranges: existing.as_ref().map(|p| p.ranges.clone()).unwrap_or([None, None, None]),
            colormap: existing.as_ref().map(|p| p.colormap.clone()).unwrap_or_else(|| "Viridis".into()),
            point_size,
            opacity,
            aggregation,
            pbr: existing.map(|p| p.pbr).unwrap_or_default(),
        };
        self.set_plot(plot);
        id
    }

    pub fn undo(&self) -> bool {
        let mut h = self.history.write();
        let Some(previous) = h.undo.pop() else { return false; };
        h.redo.push(self.snapshot());
        *self.inner.write() = previous;
        true
    }

    pub fn redo(&self) -> bool {
        let mut h = self.history.write();
        let Some(next) = h.redo.pop() else { return false; };
        h.undo.push(self.snapshot());
        *self.inner.write() = next;
        true
    }
}
