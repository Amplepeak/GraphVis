"""Model layer for the GraphVis MVC application.

No Qt widgets live here. The model owns project state, datasets, nicknames,
publication profiles and file operations. Controllers may call these methods
from worker tasks; Views only display the resulting state.
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from graphvis.core.logging import get_logger
from graphvis.data.loader import Dataset, load_dataset
from graphvis.core.workspace import ProjectWorkspace, active_project
from graphvis.core.publication import PublicationProfileStore
from graphvis.data.connectors import ConnectorManager, ConnectorProfile, ConnectorStore
from graphvis.core.workflow import AnalysisWorkflow, WorkflowStore
from graphvis.data.organization import WorksheetModel, FormulaEngine, DataOperations

LOG = get_logger("model")


@dataclass(slots=True)
class DatasetRecord:
    name: str
    project_path: str
    source_path: str | None = None
    loaded: bool = False


class GraphVisApplicationModel:
    """Authoritative application state for the MVC layer."""

    def __init__(self, workspace: ProjectWorkspace | None = None) -> None:
        self.project: ProjectWorkspace = workspace or active_project()
        self.datasets: dict[str, Dataset] = {}
        self.nicknames: dict[str, str] = self.project.load_nicknames()
        self.publication_profiles = PublicationProfileStore(self.project.profiles_dir)
        self.live_telemetry_enabled: bool = False
        self.connectors = ConnectorManager(ConnectorStore(self.project.connectors_dir / "connectors.json"))
        self.workflows = WorkflowStore(self.project.workflows_dir)

    def switch_project(self, workspace: ProjectWorkspace) -> None:
        self.project = workspace.ensure()
        self.datasets.clear()
        self.nicknames = self.project.load_nicknames()
        self.publication_profiles = PublicationProfileStore(self.project.profiles_dir)
        self.connectors = ConnectorManager(ConnectorStore(self.project.connectors_dir / "connectors.json"))
        self.workflows = WorkflowStore(self.project.workflows_dir)

    def import_dataset(self, source: str, *, persist: bool = True) -> Dataset:
        source_path = str(Path(source).expanduser().resolve())
        project_path = self.project.ingest_file(source_path, "datasets") if persist else source_path
        ds = load_dataset(project_path)
        ds.meta.setdefault("source_external", source_path)
        ds.meta.setdefault("project_path", project_path)
        self.register_dataset(ds)
        return ds

    def load_project_dataset(self, path: str) -> Dataset:
        ds = load_dataset(path)
        ds.meta.setdefault("project_path", str(Path(path).resolve()))
        source = self.project.source_for(path)
        if source:
            ds.meta.setdefault("source_external", source)
        self.register_dataset(ds)
        return ds

    def register_dataframe(self, name: str, frame, *, kind: str = "derived", meta: dict | None = None) -> Dataset:
        ds=Dataset(str(name),"",frame.copy(),meta={"kind":kind,**dict(meta or {})})
        self.register_dataset(ds)
        return ds

    def register_dataset(self, ds: Dataset) -> None:
        self.datasets[ds.name] = ds
        LOG.info("Dataset registered: %s (%s rows)", ds.name, len(ds.df))

    def unregister_dataset(self, name: str) -> Dataset | None:
        return self.datasets.pop(name, None)

    def rescan_project(self) -> tuple[list[Dataset], list[str]]:
        """Load new/changed project datasets and remove vanished registry entries."""
        loaded: list[Dataset] = []
        errors: list[str] = []
        on_disk = {str(Path(p).resolve()): p for p in self.project.dataset_files()}
        registered_paths = {str(Path(ds.path).resolve()): name for name, ds in self.datasets.items() if ds.path}
        for resolved, path in on_disk.items():
            existing_name = registered_paths.get(resolved)
            try:
                # Always reparse during an explicit rescan; loader-level caches remain mtime-aware.
                ds = load_dataset(path)
                if existing_name and existing_name != ds.name:
                    self.datasets.pop(existing_name, None)
                self.register_dataset(ds)
                loaded.append(ds)
            except Exception as exc:
                errors.append(f"{os.path.basename(path)}: {type(exc).__name__}: {exc}")
                LOG.exception("Rescan failed for %s", path)
        vanished = [name for p, name in registered_paths.items() if p not in on_disk]
        for name in vanished:
            self.datasets.pop(name, None)
        return loaded, errors

    def detach_dataset(self, name: str, *, delete_local: bool = False) -> tuple[str | None, str | None]:
        ds = self.datasets.get(name)
        if ds is None:
            raise KeyError(name)
        project_path = str(Path(ds.path).expanduser().resolve()) if ds.path else None
        self.datasets.pop(name, None)
        detached = self.project.detach_dataset(project_path, delete_local=delete_local) if project_path else None
        LOG.info("Dataset removed from auto-scan: %s | detached=%s | permanent=%s", project_path, detached, delete_local)
        return project_path, detached

    def delete_dataset(self, name: str, *, delete_external_source: bool = False) -> tuple[str | None, str | None]:
        ds = self.datasets.get(name)
        if ds is None:
            raise KeyError(name)
        project_path = str(Path(ds.path).expanduser().resolve()) if ds.path else None
        source_path = self.project.source_for(project_path) if project_path else None
        self.datasets.pop(name, None)
        if project_path:
            self.project.delete_dataset(project_path, delete_source=delete_external_source)
        LOG.warning("Dataset permanently deleted: %s | source=%s", project_path, source_path)
        return project_path, source_path

    def set_nickname(self, raw_name: str, nickname: str) -> None:
        raw_name, nickname = str(raw_name).strip(), str(nickname).strip()
        if not raw_name:
            return
        if nickname:
            self.nicknames[raw_name] = nickname
        else:
            self.nicknames.pop(raw_name, None)
        self.project.save_nicknames(self.nicknames)

    def set_nicknames(self, mapping: dict[str, str]) -> None:
        self.nicknames = {str(k): str(v).strip() for k, v in mapping.items() if str(v).strip()}
        self.project.save_nicknames(self.nicknames)

    def display_name(self, raw_name: str) -> str:
        nick = self.nicknames.get(str(raw_name), "").strip()
        return f"{nick} ({raw_name})" if nick else str(raw_name)


    def set_dataset_assumption(self, name: str, assumed: bool, note: str | None = None) -> None:
        ds = self.datasets.get(name)
        if ds is None:
            raise KeyError(name)
        ds.assumed = bool(assumed)
        if note is not None:
            ds.meta["assumption_note"] = str(note)

    def set_dataset_assumption_note(self, name: str, note: str) -> None:
        ds = self.datasets.get(name)
        if ds is None:
            raise KeyError(name)
        ds.meta["assumption_note"] = str(note)

    def add_derived_series(self, dataset_name: str, column: str, values, unit: str | None = None) -> None:
        ds = self.datasets.get(dataset_name)
        if ds is None:
            raise KeyError(dataset_name)
        ds.df[str(column)] = values
        if unit:
            ds.units[str(column)] = str(unit)
        ds.touch()

    def remove_series(self, dataset_name: str, column: str) -> None:
        ds = self.datasets.get(dataset_name)
        if ds is None:
            raise KeyError(dataset_name)
        ds.df.drop(columns=[str(column)], inplace=True, errors="ignore")
        ds.units.pop(str(column), None)
        ds.touch()

    def save_snapshot(self, name: str, payload: dict) -> str:
        return self.project.save_snapshot(name, payload)

    def list_snapshots(self) -> list[dict]:
        return self.project.list_snapshots()

    def load_snapshot(self, path_or_name: str) -> dict:
        return self.project.load_snapshot(path_or_name)

    def save_template(self, name: str, payload: dict) -> str:
        return self.project.save_template(name, payload)

    def list_templates(self) -> list[dict]:
        return self.project.list_templates()

    def load_template(self, path_or_name: str) -> dict:
        return self.project.load_template(path_or_name)

    def telemetry_paths(self, active_names: Iterable[str] | None = None) -> list[str]:
        names = set(active_names or self.datasets.keys())
        out: list[str] = []
        for name in names:
            ds = self.datasets.get(name)
            if ds is None or not ds.path:
                continue
            out.append(str(Path(ds.path).resolve()))
            source = self.project.source_for(ds.path)
            if source:
                out.append(str(Path(source).resolve()))
        return list(dict.fromkeys(out))

    def import_connector_frame(self, profile: ConnectorProfile, *, dataset_name: str | None = None) -> Dataset:
        """Refresh a connector and register its tabular result as a live Dataset."""
        self.connectors.upsert(profile)
        frame = self.connectors.refresh(profile.name)
        name = dataset_name or profile.name
        ds = Dataset(name, f"connector://{profile.name}", frame, meta={"kind": "connector", "connector": profile.name, "source": profile.url})
        self.register_dataset(ds)
        return ds

    def refresh_connector(self, name: str) -> Dataset:
        profile = self.connectors.profiles[name]
        return self.import_connector_frame(profile, dataset_name=name)

    def update_dataset_metadata(self, dataset_name: str, metadata: dict) -> None:
        ds=self.datasets[dataset_name]
        ds.meta.update(dict(metadata))
        LOG.info("Dataset metadata updated: %s (%s keys)",dataset_name,len(metadata))

    def assign_formula(self, dataset_name: str, target: str, expression: str) -> None:
        ds = self.datasets[dataset_name]
        sheet = WorksheetModel(ds.name, ds.df)
        FormulaEngine.assign(sheet, target, expression)
        ds.df = sheet.frame
        ds.meta.setdefault("formulas", {})[target] = expression
        ds.touch()

    def apply_conditional_mask(self, dataset_name: str, expression: str, name: str = "conditional") -> int:
        ds = self.datasets[dataset_name]
        sheet = WorksheetModel(ds.name, ds.df)
        mask = DataOperations.conditional_mask(sheet, name, expression)
        ds.meta.setdefault("masks", {})[name] = expression
        ds.df = ds.df.loc[mask].copy()
        ds.touch()
        return int(mask.sum())

    def save_workflow(self, workflow: AnalysisWorkflow) -> str:
        return str(self.workflows.save(workflow))
