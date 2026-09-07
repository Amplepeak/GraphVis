# =========================================================================
# project_workspace.py - persistent GraphVis project workspaces.
#
# A project is a self-contained folder with copied source files, generated
# digitised datasets, snapshots and graph templates.  The module is deliberately
# Qt-free so project state can also be inspected from scripts/tests.
# =========================================================================
from __future__ import annotations

import json
import os
import re
import shutil
import time
from dataclasses import dataclass, field
from pathlib import Path

from graphvis.core.paths import PROJECTS_DIR, ACTIVE_PROJECT_FILE, ensure_layout
ensure_layout()
APP_DIR = Path(__file__).resolve().parent
PROJECTS_ROOT = PROJECTS_DIR
PROJECTS_ROOT.mkdir(parents=True, exist_ok=True)
ACTIVE_PROJECT_FILE.parent.mkdir(parents=True, exist_ok=True)


def _safe_name(text: str, fallback: str = "Project") -> str:
    text = re.sub(r"[^A-Za-z0-9._ -]+", "_", str(text or "")).strip(" ._")
    return text or fallback


def _unique_path(path: Path) -> Path:
    if not path.exists():
        return path
    stem, suffix = path.stem, path.suffix
    i = 2
    while True:
        candidate = path.with_name(f"{stem}_{i}{suffix}")
        if not candidate.exists():
            return candidate
        i += 1


@dataclass
class ProjectWorkspace:
    root: Path
    manifest: dict = field(default_factory=dict)

    @property
    def name(self) -> str:
        return self.root.name

    @property
    def datasets_dir(self) -> Path:
        return self.root / "datasets"

    @property
    def documents_dir(self) -> Path:
        return self.root / "documents"

    @property
    def literature_datasets_dir(self) -> Path:
        return self.datasets_dir / "literature_extracted"

    @property
    def detached_datasets_dir(self) -> Path:
        """Project-local holding area excluded from automatic dataset scans."""
        return self.root / "detached_datasets"

    @property
    def literature_dir(self) -> Path:
        return self.documents_dir / "literature"

    @property
    def scripts_dir(self) -> Path:
        return self.documents_dir / "scripts"

    @property
    def context_dir(self) -> Path:
        return self.root / "context"

    @property
    def digitized_dir(self) -> Path:
        return self.root / "digitized"

    @property
    def snapshots_dir(self) -> Path:
        return self.root / "snapshots"

    @property
    def templates_dir(self) -> Path:
        return self.root / "templates"

    @property
    def exports_dir(self) -> Path:
        return self.root / "exports"

    @property
    def figures_dir(self) -> Path:
        return self.root / "figures"

    @property
    def profiles_dir(self) -> Path:
        return self.root / "publication_profiles"

    @property
    def settings_dir(self) -> Path:
        return self.root / "settings"

    @property
    def workflows_dir(self) -> Path:
        return self.root / "workflows"

    @property
    def reports_dir(self) -> Path:
        return self.root / "reports"

    @property
    def connectors_dir(self) -> Path:
        return self.root / "connectors"

    @property
    def nicknames_path(self) -> Path:
        return self.settings_dir / "parameter_nicknames.json"

    @property
    def manifest_path(self) -> Path:
        return self.root / "project.json"

    def ensure(self):
        self.root.mkdir(parents=True, exist_ok=True)
        for d in (self.datasets_dir, self.literature_datasets_dir, self.documents_dir, self.literature_dir, self.scripts_dir, self.context_dir, self.digitized_dir,
                  self.snapshots_dir, self.templates_dir, self.exports_dir, self.figures_dir,
                  self.profiles_dir, self.settings_dir, self.workflows_dir, self.reports_dir, self.connectors_dir):
            d.mkdir(parents=True, exist_ok=True)
        if not self.manifest:
            self.manifest = self._read_manifest()
        self.manifest.setdefault("name", self.name)
        self.manifest.setdefault("created", time.time())
        self.manifest.setdefault("files", [])
        self.manifest.setdefault("snapshots", [])
        self.manifest.setdefault("templates", [])
        self.save_manifest()
        return self

    def _read_manifest(self) -> dict:
        try:
            return json.loads(self.manifest_path.read_text(encoding="utf-8"))
        except Exception:
            return {}

    def save_manifest(self):
        self.manifest["modified"] = time.time()
        self.manifest_path.write_text(json.dumps(self.manifest, indent=2, ensure_ascii=False), encoding="utf-8")

    def ingest_file(self, source: str, category: str = "datasets", copy: bool = True) -> str:
        """Copy/link a source file into the project and record provenance.

        category is normally datasets, documents or digitized. Existing project
        files are reused; outside files receive a collision-safe copied name.
        """
        src = Path(source).expanduser().resolve()
        if not src.exists():
            raise FileNotFoundError(src)
        target_dir = {
            "datasets": self.datasets_dir,
            "documents": self.documents_dir,
            "literature": self.literature_dir,
            "scripts": self.scripts_dir,
            "digitized": self.digitized_dir,
        }.get(category, self.root / _safe_name(category, "files"))
        target_dir.mkdir(parents=True, exist_ok=True)
        # Re-importing the same external source refreshes its existing project
        # copy instead of creating an endless _2/_3 chain. This also preserves
        # a stable project path for file-watch/live-link refreshes.
        for rec in self.manifest.setdefault("files", []):
            try:
                same_source = Path(rec.get("source_path", "")).expanduser().resolve() == src
            except Exception:
                same_source = False
            if same_source and rec.get("category") == category:
                dst = Path(rec.get("project_path", "")).expanduser()
                if dst:
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    if copy and src != dst:
                        shutil.copy2(src, dst)
                    rec["synced"] = time.time()
                    self.save_manifest()
                    return str(dst)
        try:
            src.relative_to(self.root)
            dst = src
        except ValueError:
            dst = target_dir / src.name
            if dst.exists() and src != dst:
                same = False
                try:
                    same = src.stat().st_size == dst.stat().st_size and int(src.stat().st_mtime) == int(dst.stat().st_mtime)
                except OSError:
                    pass
                if not same:
                    dst = _unique_path(dst)
            if src != dst and not dst.exists():
                if copy:
                    shutil.copy2(src, dst)
                else:
                    dst = src
        rec = {
            "category": category,
            "project_path": str(dst),
            "source_path": str(src),
            "added": time.time(),
        }
        existing = self.manifest.setdefault("files", [])
        if not any(r.get("project_path") == str(dst) for r in existing):
            existing.append(rec)
            self.save_manifest()
        return str(dst)


    def ingest_file_progress(self, source: str, category: str = "datasets", *, progress=None, cancelled=None,
                             copy: bool = True, chunk_size: int = 8 * 1024 * 1024) -> str:
        """Copy a file into the project with byte-level progress and cancellation.

        This is used for potentially large literature/data imports so the UI does not
        appear frozen at 2-3%% while ``shutil.copy2`` performs one opaque operation.
        ``progress`` receives ``(message, percent)`` and ``cancelled`` is a callable.
        The destination is only committed after the temporary ``.part`` copy finishes.
        """
        src = Path(source).expanduser().resolve()
        if not src.exists():
            raise FileNotFoundError(src)
        target_dir = {
            "datasets": self.datasets_dir,
            "documents": self.documents_dir,
            "literature": self.literature_dir,
            "scripts": self.scripts_dir,
            "digitized": self.digitized_dir,
        }.get(category, self.root / _safe_name(category, "files"))
        target_dir.mkdir(parents=True, exist_ok=True)

        # Reuse the existing project destination for the same original source.
        dst = None
        for rec in self.manifest.setdefault("files", []):
            try:
                same_source = Path(rec.get("source_path", "")).expanduser().resolve() == src
            except Exception:
                same_source = False
            if same_source and rec.get("category") == category:
                dst = Path(rec.get("project_path", "")).expanduser()
                break
        if dst is None:
            try:
                src.relative_to(self.root)
                dst = src
            except ValueError:
                dst = target_dir / src.name
                if dst.exists() and src != dst:
                    same = False
                    try:
                        same = src.stat().st_size == dst.stat().st_size and int(src.stat().st_mtime) == int(dst.stat().st_mtime)
                    except OSError:
                        pass
                    if not same:
                        dst = _unique_path(dst)
        if src == dst or not copy:
            final = src if not copy else dst
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            total = max(int(src.stat().st_size), 1)
            tmp = dst.with_name(dst.name + ".graphvis.part")
            copied = 0
            try:
                with src.open("rb") as rf, tmp.open("wb") as wf:
                    while True:
                        if cancelled and cancelled():
                            raise RuntimeError("Cancelled")
                        block = rf.read(max(int(chunk_size), 1024 * 1024))
                        if not block:
                            break
                        wf.write(block)
                        copied += len(block)
                        if progress:
                            pct = min(99, int(copied * 100 / total))
                            progress(f"Copying {src.name}: {copied/1048576:.1f}/{total/1048576:.1f} MB", pct)
                shutil.copystat(src, tmp)
                os.replace(tmp, dst)
            except Exception:
                try:
                    if tmp.exists():
                        tmp.unlink()
                except Exception:
                    pass
                raise
            final = dst

        rec = {"category": category, "project_path": str(final), "source_path": str(src), "added": time.time(), "synced": time.time()}
        existing = self.manifest.setdefault("files", [])
        replaced = False
        for i, old in enumerate(existing):
            try:
                same_source = Path(old.get("source_path", "")).expanduser().resolve() == src and old.get("category") == category
            except Exception:
                same_source = False
            if same_source:
                existing[i] = rec; replaced = True; break
        if not replaced and not any(r.get("project_path") == str(final) for r in existing):
            existing.append(rec)
        self.save_manifest()
        if progress:
            progress(f"Copied {src.name}", 100)
        return str(final)

    def source_for(self, project_path: str) -> str | None:
        """Return the original external source recorded for a project copy."""
        try:
            target = Path(project_path).expanduser().resolve()
        except Exception:
            return None
        for rec in reversed(self.manifest.get("files", [])):
            try:
                if Path(rec.get("project_path", "")).expanduser().resolve() == target:
                    src = Path(rec.get("source_path", "")).expanduser().resolve()
                    return str(src) if src.exists() and src != target else None
            except Exception:
                continue
        return None

    def sync_from_source(self, project_path: str) -> str:
        """Refresh a project-local file from its recorded original source."""
        dst = Path(project_path).expanduser().resolve()
        src_text = self.source_for(str(dst))
        if not src_text:
            return str(dst)
        src = Path(src_text)
        if not src.exists():
            raise FileNotFoundError(src)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        for rec in self.manifest.get("files", []):
            try:
                if Path(rec.get("project_path", "")).expanduser().resolve() == dst:
                    rec["synced"] = time.time()
            except Exception:
                continue
        self.save_manifest()
        return str(dst)

    def detached_dataset_files(self) -> list[str]:
        if not self.detached_datasets_dir.exists():
            return []
        return [str(p) for p in sorted(self.detached_datasets_dir.iterdir()) if p.is_file()]

    def restore_detached_dataset(self, detached_path: str) -> str:
        src = Path(detached_path).expanduser().resolve()
        try:
            src.relative_to(self.detached_datasets_dir.resolve())
        except ValueError as exc:
            raise ValueError("Restore is limited to this project's detached_datasets folder") from exc
        if not src.exists() or not src.is_file():
            raise FileNotFoundError(src)
        self.datasets_dir.mkdir(parents=True, exist_ok=True)
        dst = _unique_path(self.datasets_dir / src.name)
        shutil.move(str(src), str(dst))
        source = ""
        remaining = []
        for rec in self.manifest.get("detached_files", []):
            try:
                if Path(rec.get("project_path", "")).expanduser().resolve() == src:
                    source = str(rec.get("source_path", "") or "")
                    continue
            except Exception:
                pass
            remaining.append(rec)
        self.manifest["detached_files"] = remaining
        self.manifest.setdefault("files", []).append({"source_path": source or str(dst), "project_path": str(dst), "kind": "datasets", "ingested": time.time()})
        self.save_manifest()
        return str(dst)

    def dataset_files(self) -> list[str]:
        if not self.datasets_dir.exists():
            return []
        allowed = {".mat", ".csv", ".tsv", ".txt", ".asc", ".dat", ".json", ".xlsx", ".xls", ".xlsm",
                   ".h5", ".hdf", ".hdf5", ".nc", ".netcdf", ".tdm", ".tdms",
                   ".html", ".htm", ".xml", ".wav", ".flac", ".mzml", ".mzxml", ".mgf", ".fcs"}
        return [str(p) for p in sorted(self.datasets_dir.rglob("*")) if p.is_file() and p.suffix.lower() in allowed
                and not p.name.endswith(".graphvis.part") and not p.name.endswith("_extraction.json")]


    def save_nicknames(self, mapping: dict[str, str]) -> None:
        self.settings_dir.mkdir(parents=True, exist_ok=True)
        clean = {str(k): str(v).strip() for k, v in mapping.items() if str(v).strip()}
        self.nicknames_path.write_text(json.dumps(clean, indent=2, ensure_ascii=False), encoding="utf-8")
        self.manifest["nicknames_file"] = str(self.nicknames_path)
        self.save_manifest()

    def load_nicknames(self) -> dict[str, str]:
        try:
            raw = json.loads(self.nicknames_path.read_text(encoding="utf-8"))
            return {str(k): str(v) for k, v in raw.items()} if isinstance(raw, dict) else {}
        except Exception:
            return {}

    def detach_dataset(self, project_path: str, *, delete_local: bool = False) -> str | None:
        """Remove a dataset from auto-scan without touching its external source.

        By default the project copy is moved to ``detached_datasets`` so the
        action is reversible.  Permanent deletion applies only to the copied
        project file; GraphVis never deletes the original import source here.
        """
        target = Path(project_path).expanduser().resolve()
        source = self.source_for(str(target))
        self.manifest["files"] = [r for r in self.manifest.get("files", [])
                                  if Path(r.get("project_path", "")).expanduser().resolve() != target]
        result = None
        if target.exists() and target.is_file():
            if delete_local:
                target.unlink()
            else:
                self.detached_datasets_dir.mkdir(parents=True, exist_ok=True)
                dst = _unique_path(self.detached_datasets_dir / target.name)
                shutil.move(str(target), str(dst)); result = str(dst)
                self.manifest.setdefault("detached_files", []).append({
                    "name": target.name, "project_path": str(dst), "source_path": source or "", "detached": time.time()
                })
        self.save_manifest()
        return result

    def delete_dataset(self, project_path: str, delete_source: bool = False) -> None:
        """Permanently remove a project dataset and, optionally, its external source."""
        target = Path(project_path).expanduser().resolve()
        source = self.source_for(str(target))
        self.manifest["files"] = [r for r in self.manifest.get("files", [])
                                  if Path(r.get("project_path", "")).expanduser().resolve() != target]
        if target.exists() and target.is_file():
            target.unlink()
        if delete_source and source:
            src = Path(source).expanduser().resolve()
            try:
                src.relative_to(self.root)
                # Already deleted as the project copy.
            except ValueError:
                if src.exists() and src.is_file():
                    src.unlink()
        self.save_manifest()

    def save_snapshot(self, name: str, payload: dict) -> str:
        fname = _safe_name(name, "snapshot") + ".json"
        path = _unique_path(self.snapshots_dir / fname)
        doc = {"name": name or path.stem, "created": time.time(), "payload": payload}
        path.write_text(json.dumps(doc, indent=2, ensure_ascii=False, default=str), encoding="utf-8")
        self.manifest.setdefault("snapshots", []).append({"name": doc["name"], "path": str(path), "created": doc["created"]})
        self.save_manifest()
        return str(path)

    def list_snapshots(self) -> list[dict]:
        out = []
        for p in sorted(self.snapshots_dir.glob("*.json"), key=lambda x: x.stat().st_mtime, reverse=True):
            try:
                doc = json.loads(p.read_text(encoding="utf-8"))
                out.append({"name": doc.get("name", p.stem), "path": str(p), "created": doc.get("created")})
            except Exception:
                continue
        return out

    def load_snapshot(self, path_or_name: str) -> dict:
        p = Path(path_or_name)
        if not p.exists():
            candidates = [s for s in self.list_snapshots() if s["name"] == path_or_name]
            if not candidates:
                raise FileNotFoundError(path_or_name)
            p = Path(candidates[0]["path"])
        return json.loads(p.read_text(encoding="utf-8")).get("payload", {})

    def save_template(self, name: str, payload: dict) -> str:
        path = self.templates_dir / (_safe_name(name, "template") + ".json")
        path.write_text(json.dumps({"name": name or path.stem, "payload": payload}, indent=2,
                                   ensure_ascii=False, default=str), encoding="utf-8")
        if not any(t.get("path") == str(path) for t in self.manifest.setdefault("templates", [])):
            self.manifest["templates"].append({"name": name or path.stem, "path": str(path)})
            self.save_manifest()
        return str(path)

    def list_templates(self) -> list[dict]:
        out = []
        for p in sorted(self.templates_dir.glob("*.json")):
            try:
                doc = json.loads(p.read_text(encoding="utf-8"))
                out.append({"name": doc.get("name", p.stem), "path": str(p)})
            except Exception:
                continue
        return out

    def load_template(self, path_or_name: str) -> dict:
        p = Path(path_or_name)
        if not p.exists():
            candidates = [t for t in self.list_templates() if t["name"] == path_or_name]
            if not candidates:
                raise FileNotFoundError(path_or_name)
            p = Path(candidates[0]["path"])
        return json.loads(p.read_text(encoding="utf-8")).get("payload", {})


def create_project(name: str) -> ProjectWorkspace:
    ws = ProjectWorkspace(PROJECTS_ROOT / _safe_name(name, "Project")).ensure()
    set_active_project(ws)
    return ws


def open_project(path: str | Path) -> ProjectWorkspace:
    ws = ProjectWorkspace(Path(path).expanduser().resolve()).ensure()
    set_active_project(ws)
    return ws


def set_active_project(ws: ProjectWorkspace):
    ACTIVE_PROJECT_FILE.write_text(json.dumps({"path": str(ws.root)}, indent=2), encoding="utf-8")


def active_project() -> ProjectWorkspace:
    try:
        path = json.loads(ACTIVE_PROJECT_FILE.read_text(encoding="utf-8")).get("path")
        if path:
            return ProjectWorkspace(Path(path)).ensure()
    except Exception:
        pass
    return create_project("Default")
