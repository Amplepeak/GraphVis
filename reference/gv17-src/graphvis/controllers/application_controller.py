"""Controller layer coordinating GraphVis models and Qt views."""
from __future__ import annotations

from pathlib import Path
from typing import Protocol

from graphvis.core.logging import get_logger
from graphvis.data.loader import Dataset
from graphvis.models.application_model import GraphVisApplicationModel
from graphvis.core.workspace import ProjectWorkspace
from graphvis.core.publication import PublicationProfile
from graphvis.data.connectors import ConnectorProfile

LOG = get_logger("controller")


class GraphVisViewProtocol(Protocol):
    def show_status(self, message: str, timeout_ms: int = 5000) -> None: ...
    def show_error(self, title: str, message: str) -> None: ...
    def dataset_registered_from_controller(self, dataset: Dataset) -> None: ...
    def dataset_removed_from_controller(self, name: str) -> None: ...


class GraphVisApplicationController:
    """Thin controller: validates user intents, mutates the model, updates the view."""

    def __init__(self, model: GraphVisApplicationModel, view: GraphVisViewProtocol) -> None:
        self.model = model
        self.view = view

    def import_dataset(self, path: str) -> Dataset | None:
        try:
            ds = self.model.import_dataset(path, persist=True)
            self.view.dataset_registered_from_controller(ds)
            self.view.show_status(f"Loaded {ds.name}: {len(ds.df):,} rows")
            return ds
        except Exception as exc:
            LOG.exception("Dataset import failed: %s", path)
            self.view.show_error("Dataset import failed", f"{type(exc).__name__}: {exc}")
            return None

    def refresh_datasets(self) -> None:
        loaded, errors = self.model.rescan_project()
        for ds in loaded:
            self.view.dataset_registered_from_controller(ds)
        if errors:
            self.view.show_error("Refresh completed with errors", "\n".join(errors[:20]))
        self.view.show_status(f"Folder rescan complete: {len(loaded)} dataset(s) refreshed")

    def detach_dataset(self, name: str, *, delete_local: bool = False) -> None:
        try:
            _old, detached = self.model.detach_dataset(name, delete_local=delete_local)
            self.view.dataset_removed_from_controller(name)
            if delete_local:
                self.view.show_status(f"Removed {name} and deleted its project copy")
            else:
                self.view.show_status(f"Removed {name} from auto-scan; moved project copy to detached_datasets")
        except Exception as exc:
            LOG.exception("Dataset detach failed: %s", name)
            self.view.show_error("Remove dataset failed", f"{type(exc).__name__}: {exc}")

    def hard_delete(self, name: str, delete_external_source: bool = False) -> None:
        try:
            self.model.delete_dataset(name, delete_external_source=delete_external_source)
            self.view.dataset_removed_from_controller(name)
            self.view.show_status(f"Permanently deleted {name}")
        except Exception as exc:
            LOG.exception("Hard delete failed: %s", name)
            self.view.show_error("Delete failed", f"{type(exc).__name__}: {exc}")

    def register_dataframe(self, name: str, frame, *, kind: str = "derived", meta: dict | None = None) -> Dataset:
        ds=self.model.register_dataframe(name,frame,kind=kind,meta=meta)
        self.view.dataset_registered_from_controller(ds)
        self.view.show_status(f"Created derived dataset: {name}")
        return ds

    def register_loaded_dataset(self, dataset: Dataset) -> None:
        """Commit a dataset parsed by a background service into Model state."""
        self.model.register_dataset(dataset)

    def unregister_dataset(self, name: str) -> Dataset | None:
        """Remove a dataset from the in-memory registry without deleting its file."""
        return self.model.unregister_dataset(name)

    def set_dataset_assumption(self, name: str, assumed: bool, note: str | None = None) -> None:
        self.model.set_dataset_assumption(name, assumed, note)

    def set_dataset_assumption_note(self, name: str, note: str) -> None:
        self.model.set_dataset_assumption_note(name, note)

    def add_derived_series(self, dataset_name: str, column: str, values, unit: str | None = None) -> None:
        self.model.add_derived_series(dataset_name, column, values, unit)

    def remove_series(self, dataset_name: str, column: str) -> None:
        self.model.remove_series(dataset_name, column)

    def save_snapshot(self, name: str, payload: dict) -> str:
        return self.model.save_snapshot(name, payload)

    def list_snapshots(self) -> list[dict]:
        return self.model.list_snapshots()

    def load_snapshot(self, path_or_name: str) -> dict:
        return self.model.load_snapshot(path_or_name)

    def save_template(self, name: str, payload: dict) -> str:
        return self.model.save_template(name, payload)

    def list_templates(self) -> list[dict]:
        return self.model.list_templates()

    def load_template(self, path_or_name: str) -> dict:
        return self.model.load_template(path_or_name)

    def switch_project(self, workspace: ProjectWorkspace) -> None:
        self.model.switch_project(workspace)
        self.view.show_status(f"Project switched: {workspace.name}")

    def set_nicknames(self, mapping: dict[str, str]) -> None:
        self.model.set_nicknames(mapping)
        self.view.show_status(f"Saved {len(self.model.nicknames)} parameter nickname(s)")

    def set_live_telemetry(self, enabled: bool) -> None:
        self.model.live_telemetry_enabled = bool(enabled)

    def dataset_paths(self) -> list[str]:
        return self.model.project.dataset_files()

    def publication_profiles(self) -> list[PublicationProfile]:
        return self.model.publication_profiles.list()

    def save_publication_profile(self, profile: PublicationProfile) -> str:
        path = self.model.publication_profiles.save(profile)
        self.view.show_status(f"Saved publication profile: {profile.name}")
        return str(path)

    def delete_publication_profile(self, name: str) -> bool:
        deleted = self.model.publication_profiles.delete(name)
        if deleted:
            self.view.show_status(f"Deleted publication profile: {name}")
        return deleted

    def import_connector(self, profile: ConnectorProfile) -> Dataset | None:
        try:
            ds = self.model.import_connector_frame(profile)
            self.view.dataset_registered_from_controller(ds)
            self.view.show_status(f"Connector refreshed: {profile.name} ({len(ds.df):,} rows)")
            return ds
        except Exception as exc:
            LOG.exception("Connector import failed: %s", profile.name)
            self.view.show_error("Connector failed", f"{type(exc).__name__}: {exc}")
            return None

    def update_dataset_metadata(self, dataset_name: str, metadata: dict) -> bool:
        try:
            self.model.update_dataset_metadata(dataset_name,metadata)
            self.view.show_status(f"Updated metadata for {dataset_name}")
            return True
        except Exception as exc:
            LOG.exception("Metadata update failed: %s",dataset_name)
            self.view.show_error("Metadata update failed",f"{type(exc).__name__}: {exc}")
            return False

    def assign_formula(self, dataset_name: str, target: str, expression: str) -> bool:
        try:
            self.model.assign_formula(dataset_name, target, expression)
            self.view.show_status(f"Formula column created: {target}")
            return True
        except Exception as exc:
            LOG.exception("Formula failed")
            self.view.show_error("Formula failed", f"{type(exc).__name__}: {exc}")
            return False

    def apply_conditional_mask(self, dataset_name: str, expression: str) -> int:
        try:
            n = self.model.apply_conditional_mask(dataset_name, expression)
            self.view.show_status(f"Conditional mask retained {n:,} rows")
            return n
        except Exception as exc:
            LOG.exception("Conditional mask failed")
            self.view.show_error("Mask failed", f"{type(exc).__name__}: {exc}")
            return 0
    def run_statistics(self, method: str, *args, **kwargs):
        """Execute a whitelisted statistics backend operation outside the View layer."""
        from graphvis.analysis.statistics import StatisticalEngine
        fn = getattr(StatisticalEngine, method, None)
        if fn is None or method.startswith("_"):
            raise ValueError(f"Unknown statistics operation: {method}")
        return fn(*args, **kwargs)

    def run_multivariate(self, method: str, *args, **kwargs):
        """Execute a multivariate/ML backend operation through the Controller."""
        from graphvis.analysis.ml import MultivariateEngine
        fn = getattr(MultivariateEngine, method, None)
        if fn is None or method.startswith("_"):
            raise ValueError(f"Unknown multivariate operation: {method}")
        return fn(*args, **kwargs)

    def run_signal(self, method: str, *args, **kwargs):
        """Execute a signal-processing backend operation through the Controller."""
        from graphvis.analysis.signal import SignalEngine
        fn = getattr(SignalEngine, method, None)
        if fn is None or method.startswith("_"):
            raise ValueError(f"Unknown signal operation: {method}")
        return fn(*args, **kwargs)

    def run_calculus(self, method: str, *args, **kwargs):
        """Execute a numerical-calculus backend operation through the Controller."""
        from graphvis.analysis.signal import CalculusEngine
        fn = getattr(CalculusEngine, method, None)
        if fn is None or method.startswith("_"):
            raise ValueError(f"Unknown calculus operation: {method}")
        return fn(*args, **kwargs)

    def run_curve_fit(self, method: str, *args, **kwargs):
        """Execute a curve-fitting backend operation through the Controller."""
        from graphvis.analysis.fitting import CurveFittingEngine
        fn = getattr(CurveFittingEngine, method, None)
        if fn is None or method.startswith("_"):
            raise ValueError(f"Unknown curve-fit operation: {method}")
        return fn(*args, **kwargs)

    def run_peak(self, method: str, *args, **kwargs):
        """Execute a peak-analysis backend operation through the Controller."""
        from graphvis.analysis.fitting import PeakEngine
        fn = getattr(PeakEngine, method, None)
        if fn is None or method.startswith("_"):
            raise ValueError(f"Unknown peak operation: {method}")
        return fn(*args, **kwargs)

