"""Persistent project intelligence for GraphVis.

Keeps named study groups that link literature/theses, datasets and analysis
scripts.  The store is Qt-free and intentionally compact so projects remain
portable.  Literature extraction summaries are cached per project and reloaded
at startup; the original source documents and datasets remain in their normal
project folders.
"""
from __future__ import annotations

import gzip
import hashlib
import json
import os
import re
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Iterable

import pandas as pd

from graphvis.core.workspace import ProjectWorkspace
from graphvis.literature.extractor import LiteratureExtraction, LitDataset


@dataclass
class ProjectGroup:
    name: str
    literature: list[str] = field(default_factory=list)
    datasets: list[str] = field(default_factory=list)
    scripts: list[str] = field(default_factory=list)
    notes: str = ""
    created: float = field(default_factory=time.time)
    modified: float = field(default_factory=time.time)


_PLOT_CALL_MAP = {
    # Values intentionally use GraphVis' concrete engine names so script hints
    # can be applied directly instead of being silently filtered out.
    "plot": "Line Chart", "line": "Line Chart", "scatter": "4D / 5D Scatter", "scatter3": "3D Scatter",
    "plot3": "3D Line", "surf": "3D Topography / Surface", "surface": "3D Topography / Surface",
    "mesh": "3D Mesh", "contour": "2D Contour", "contourf": "2D Contour", "imagesc": "2D Heatmap",
    "imshow": "2D Heatmap", "heatmap": "2D Heatmap", "bar": "Bar", "barh": "Horizontal Bar",
    "hist": "Histogram", "histogram": "Histogram", "boxplot": "Box Plot", "violinplot": "Violin Plot",
    "errorbar": "Error Bar", "semilogx": "Line Chart", "semilogy": "Line Chart", "loglog": "Line Chart",
    "quiver": "Quiver Field", "streamplot": "Stream Field", "polarplot": "Polar Line", "rose": "Wind Rose",
    "pie": "Pie", "stem": "Stem", "stairs": "Stairs", "waterfall": "Waterfall",
}


def analyse_script_text(text: str, filename: str = "script") -> dict:
    """Extract useful plotting/parameter intent from MATLAB/Python/R-style scripts."""
    suffix = Path(filename).suffix.lower()
    language = {".m": "MATLAB", ".py": "Python", ".r": "R", ".jl": "Julia"}.get(suffix, "Text/Script")
    assignments: dict[str, str] = {}
    for m in re.finditer(r"(?m)^\s*([A-Za-z_]\w*)\s*=\s*([^\n;]{1,160})", text):
        key, value = m.group(1), m.group(2).strip()
        if key not in assignments and len(assignments) < 120:
            assignments[key] = value
    calls = []
    lower = text.lower()
    for call, graph in _PLOT_CALL_MAP.items():
        if re.search(rf"\b{re.escape(call.lower())}\s*\(", lower):
            calls.append({"call": call, "graph": graph})
    axes = {}
    for key, fn in (("x", "xlabel"), ("y", "ylabel"), ("z", "zlabel"), ("title", "title")):
        m = re.search(rf"\b{fn}\s*\(\s*['\"]([^'\"]+)", text, re.I)
        if m:
            axes[key] = m.group(1).strip()
    comments = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith(('%', '#', '//')):
            comments.append(stripped.lstrip('%#/ ').strip())
        if len(comments) >= 40:
            break
    return {
        "filename": Path(filename).name,
        "language": language,
        "assignments": assignments,
        "plot_calls": calls,
        "axis_labels": axes,
        "comments": comments,
        "text_excerpt": text[:24000],
        "updated": time.time(),
    }


class ProjectContextStore:
    def __init__(self, project: ProjectWorkspace):
        self.project = project
        self.project.ensure()
        self.path = self.project.context_dir / "project_context.json"
        self.literature_index_path = self.project.context_dir / "literature_index.json"
        self.script_index_path = self.project.context_dir / "script_index.json"
        self.literature_cache_dir = self.project.context_dir / "literature_cache"
        self.literature_cache_dir.mkdir(parents=True, exist_ok=True)
        self.data = self._load_json(self.path, {"active_group": "", "groups": {}})
        self.literature_index = self._load_json(self.literature_index_path, {})
        self.script_index = self._load_json(self.script_index_path, {})

    @staticmethod
    def _load_json(path: Path, fallback):
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
            return raw if isinstance(raw, type(fallback)) else fallback
        except Exception:
            return fallback.copy() if isinstance(fallback, dict) else fallback

    def _save(self) -> None:
        self.project.context_dir.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.data, indent=2, ensure_ascii=False), encoding="utf-8")
        self.literature_index_path.write_text(json.dumps(self.literature_index, indent=2, ensure_ascii=False), encoding="utf-8")
        self.script_index_path.write_text(json.dumps(self.script_index, indent=2, ensure_ascii=False), encoding="utf-8")

    @property
    def active_group_name(self) -> str:
        name = str(self.data.get("active_group") or "")
        return name if name in self.data.get("groups", {}) else ""

    def groups(self) -> list[ProjectGroup]:
        out = []
        for name, rec in self.data.get("groups", {}).items():
            try:
                out.append(ProjectGroup(name=name, **{k: rec.get(k, getattr(ProjectGroup(name), k)) for k in ("literature","datasets","scripts","notes","created","modified")}))
            except Exception:
                continue
        return sorted(out, key=lambda g: g.name.lower())

    def get_group(self, name: str | None = None) -> ProjectGroup | None:
        name = str(name or self.active_group_name or "")
        rec = self.data.get("groups", {}).get(name)
        if not rec:
            return None
        return ProjectGroup(name=name,
                            literature=list(rec.get("literature", [])), datasets=list(rec.get("datasets", [])),
                            scripts=list(rec.get("scripts", [])), notes=str(rec.get("notes", "")),
                            created=float(rec.get("created", time.time())), modified=float(rec.get("modified", time.time())))

    def save_group(self, group: ProjectGroup, make_active: bool = True) -> None:
        group.name = group.name.strip() or "Project Overview"
        group.modified = time.time()
        old = self.data.setdefault("groups", {}).get(group.name, {})
        if old.get("created"):
            group.created = float(old["created"])
        rec = asdict(group); rec.pop("name", None)
        self.data["groups"][group.name] = rec
        if make_active:
            self.data["active_group"] = group.name
        self._save()

    def delete_group(self, name: str) -> None:
        self.data.setdefault("groups", {}).pop(name, None)
        if self.data.get("active_group") == name:
            self.data["active_group"] = ""
        self._save()

    def ensure_default_group(self) -> ProjectGroup:
        current = self.get_group()
        if current:
            return current
        group = ProjectGroup(name="Project Overview")
        self.save_group(group, True)
        return group

    def set_active(self, name: str) -> None:
        if name in self.data.get("groups", {}):
            self.data["active_group"] = name
            self._save()

    def save_ai_advice(self, result: dict, group_name: str | None = None) -> None:
        group = self.get_group(group_name) or self.ensure_default_group()
        self.data.setdefault("ai_advice", {})[group.name] = {"updated": time.time(), "result": result}
        self._save()

    def load_ai_advice(self, group_name: str | None = None) -> dict:
        group = self.get_group(group_name) or self.ensure_default_group()
        rec = self.data.get("ai_advice", {}).get(group.name, {})
        return dict(rec.get("result", {})) if isinstance(rec, dict) else {}

    def add_to_group(self, *, literature: Iterable[str] = (), datasets: Iterable[str] = (), scripts: Iterable[str] = (),
                     group_name: str | None = None) -> ProjectGroup:
        group = self.get_group(group_name) or self.ensure_default_group()
        for attr, values in (("literature", literature), ("datasets", datasets), ("scripts", scripts)):
            current = getattr(group, attr)
            for value in values:
                value = str(value)
                if value and value not in current:
                    current.append(value)
        self.save_group(group, True)
        return group

    def remove_dataset_references(self, names: Iterable[str]) -> None:
        """Remove dataset membership from every group; files themselves are untouched."""
        remove = {str(n) for n in names}
        if not remove:
            return
        changed = False
        for rec in self.data.setdefault("groups", {}).values():
            before = list(rec.get("datasets", []))
            after = [n for n in before if str(n) not in remove]
            if after != before:
                rec["datasets"] = after; rec["modified"] = time.time(); changed = True
        if changed:
            self._save()

    def cache_literature(self, ext: LiteratureExtraction) -> None:
        key = str(Path(ext.path).resolve())
        text_cache = ""
        if ext.text:
            try:
                digest = hashlib.sha256(key.encode("utf-8", errors="ignore")).hexdigest()[:24]
                cache_path = self.literature_cache_dir / f"{digest}.txt.gz"
                tmp = cache_path.with_suffix(cache_path.suffix + ".tmp")
                with gzip.open(tmp, "wt", encoding="utf-8") as fh:
                    fh.write(ext.text)
                os.replace(tmp, cache_path)
                text_cache = cache_path.name
            except Exception:
                text_cache = ""
        datasets = []
        for d in ext.datasets:
            datasets.append({
                "name": d.name, "source_page": d.source_page, "kind": d.kind, "kind_hint": d.kind_hint,
                "header_note": d.header_note, "yerr_col": d.yerr_col, "saved_path": d.saved_path,
                "columns": [str(c) for c in d.df.columns], "rows": int(len(d.df)),
            })
        previous = self.literature_index.get(key) or {}
        self.literature_index[key] = {
            "title": ext.title, "path": ext.path, "method": ext.method, "text_excerpt": ext.text[:50000],
            "text_cache": text_cache,
            "parameters": ext.parameters, "warnings": ext.warnings, "n_pages": ext.n_pages,
            "primary_document": ext.primary_document, "linked_files": ext.linked_files, "batch_id": ext.batch_id,
            "semantic_context": ext.semantic_context, "datasets": datasets, "updated": time.time(),
            # Literature profiling: user-assigned tags survive re-ingestion.
            "tags": list(previous.get("tags") or []),
        }
        self._save()

    # ------------------------------------------------- literature profiling
    def literature_tags(self, path: str) -> list[str]:
        """User-assigned profile tags for one literature source."""
        try:
            key = str(Path(path).resolve())
        except Exception:
            key = str(path)
        rec = self.literature_index.get(key) or {}
        return [str(t) for t in (rec.get("tags") or [])]

    def set_literature_tags(self, path: str, tags: Iterable[str]) -> None:
        """Assign profile tags (categories) to a literature source.

        The source gains an index record even before full ingestion so papers
        can be organised immediately after being dropped into the project.
        """
        try:
            key = str(Path(path).resolve())
        except Exception:
            key = str(path)
        cleaned = sorted({str(t).strip() for t in tags if str(t).strip()})
        rec = self.literature_index.setdefault(key, {"title": Path(key).stem, "path": key})
        rec["tags"] = cleaned
        rec["updated"] = time.time()
        self._save()

    def all_literature_tags(self) -> list[str]:
        tags: set[str] = set()
        for rec in self.literature_index.values():
            tags.update(str(t) for t in (rec.get("tags") or []))
        return sorted(tags)

    def literature_cross_links(self, path: str) -> dict:
        """Cross-link profile for one paper: the groups containing it, the
        datasets those groups bind it to, and its own extracted tables."""
        try:
            key = str(Path(path).resolve())
        except Exception:
            key = str(path)
        groups, datasets = [], []
        for name, rec in (self.data.get("groups") or {}).items():
            lit = [str(Path(p).resolve()) if os.path.exists(p) else str(p) for p in rec.get("literature", [])]
            if key in lit or str(path) in rec.get("literature", []):
                groups.append(name)
                datasets.extend(rec.get("datasets", []))
        entry = self.literature_index.get(key) or {}
        return {"groups": groups, "datasets": sorted(set(datasets)),
                "tags": list(entry.get("tags") or []),
                "extracted_tables": [d.get("name") for d in entry.get("datasets", [])]}

    def load_literature(self, paths: Iterable[str] | None = None, *, full_text: bool = False) -> list[LiteratureExtraction]:
        wanted = {str(Path(p).resolve()) for p in paths} if paths is not None else None
        out: list[LiteratureExtraction] = []
        for key, rec in self.literature_index.items():
            try:
                resolved = str(Path(key).resolve())
            except Exception:
                resolved = key
            if wanted is not None and resolved not in wanted:
                continue
            text_value = rec.get("text_excerpt", "")
            cached_text = str(rec.get("text_cache", "") or "")
            if full_text and cached_text:
                try:
                    cache_path = Path(cached_text)
                    if not cache_path.is_absolute():
                        cache_path = self.literature_cache_dir / cache_path
                    with gzip.open(cache_path, "rt", encoding="utf-8") as fh:
                        text_value = fh.read()
                except Exception:
                    pass
            ext = LiteratureExtraction(title=rec.get("title", Path(key).stem), path=rec.get("path", key),
                                       method=rec.get("method", "cached"), text=text_value,
                                       parameters=rec.get("parameters", {}), warnings=rec.get("warnings", []),
                                       n_pages=int(rec.get("n_pages", 0)), primary_document=rec.get("primary_document"),
                                       linked_files=rec.get("linked_files", []), batch_id=rec.get("batch_id", ""),
                                       semantic_context=rec.get("semantic_context", {}))
            for drec in rec.get("datasets", []):
                df = pd.DataFrame(columns=drec.get("columns", []))
                sp = drec.get("saved_path")
                if sp and os.path.exists(sp):
                    try:
                        df = pd.read_csv(sp)
                    except Exception:
                        pass
                ext.datasets.append(LitDataset(name=drec.get("name", "dataset"), df=df,
                                               source_page=drec.get("source_page"), kind=drec.get("kind", "table"),
                                               kind_hint=drec.get("kind_hint", "generic"), header_note=drec.get("header_note", ""),
                                               yerr_col=drec.get("yerr_col"), saved_path=sp))
            ext.saved_paths = [d.saved_path for d in ext.datasets if d.saved_path]
            out.append(ext)
        return out

    def cache_script(self, project_path: str, text: str) -> dict:
        rec = analyse_script_text(text, project_path)
        self.script_index[str(Path(project_path).resolve())] = rec
        self._save()
        return rec

    def script_contexts(self, paths: Iterable[str] | None = None) -> list[dict]:
        wanted = {str(Path(p).resolve()) for p in paths} if paths is not None else None
        out = []
        for key, rec in self.script_index.items():
            try:
                rk = str(Path(key).resolve())
            except Exception:
                rk = key
            if wanted is None or rk in wanted:
                out.append(dict(rec))
        return out

    def project_summary(self, group_name: str | None = None, max_chars: int = 24000) -> str:
        group = self.get_group(group_name) or self.ensure_default_group()
        pieces = [f"Project group: {group.name}"]
        if group.notes:
            pieces.append("Notes: " + group.notes)
        for ext in self.load_literature(group.literature, full_text=True):
            pieces.append(f"Literature: {ext.title}")
            sc = ext.semantic_context or {}
            if sc:
                pieces.append("Semantic context: " + json.dumps(sc, ensure_ascii=False)[:4000])
            if ext.parameters:
                pieces.append("Parameters: " + json.dumps(ext.parameters, ensure_ascii=False)[:3000])
            if ext.text:
                pieces.append("Text excerpt: " + ext.text[:5000])
        for rec in self.script_contexts(group.scripts):
            pieces.append("Script context: " + json.dumps({k: rec.get(k) for k in ("filename","language","assignments","plot_calls","axis_labels","comments")}, ensure_ascii=False)[:6000])
        return "\n\n".join(pieces)[:max_chars]

    def script_graph_hints(self, group_name: str | None = None) -> list[tuple[str, str]]:
        group = self.get_group(group_name) or self.ensure_default_group()
        out = []
        for rec in self.script_contexts(group.scripts):
            for call in rec.get("plot_calls", []):
                graph = call.get("graph")
                if graph:
                    out.append((graph, f"{rec.get('language','Script')} file {rec.get('filename','')} uses {call.get('call')}(), suggesting this visual form."))
        seen = set(); clean=[]
        for item in out:
            if item[0] not in seen:
                seen.add(item[0]); clean.append(item)
        return clean

    def keyword_graph_hints(self, group_name: str | None = None) -> list[tuple[str, str]]:
        """Offline graph hints from project text when no external AI is used."""
        text = self.project_summary(group_name, max_chars=30000).lower()
        rules = [
            (("time course", "kinetic", "over time", "temporal", "trajectory"), "Line Chart", "Project context describes time-dependent/kinetic behaviour."),
            (("distribution", "variance", "group comparison", "replicate"), "Box Plot", "Project context discusses distributions or grouped variability."),
            (("correlation", "association", "relationship between"), "4D / 5D Scatter", "Project context asks about relationships/correlations among variables."),
            (("optimiz", "response surface", "operating window", "parameter sweep"), "2D Heatmap", "Project context describes optimization or a multi-parameter operating window."),
            (("contour", "iso-line", "isoline"), "2D Contour", "Project context explicitly refers to contour/iso-response behaviour."),
            (("spectrum", "spectral", "frequency", "fft"), "Power Spectral Density", "Project context contains frequency/spectral analysis language."),
            (("agreement", "method comparison"), "Bland-Altman", "Project context describes agreement or method-comparison analysis."),
            (("classification", "sensitivity", "specificity", "auc"), "ROC Curve", "Project context describes classification performance."),
            (("effect size", "confidence interval", "meta-analysis"), "Forest Plot", "Project context discusses effect estimates and confidence intervals."),
            (("p-value", "fold change", "differential"), "Volcano Plot", "Project context discusses significance together with effect/fold change."),
            (("process control", "quality control", "control limit"), "Control Chart", "Project context describes statistical process control."),
            (("composition", "ternary", "three component"), "Ternary Scatter", "Project context describes a three-component composition."),
        ]
        out = []
        for tokens, graph, why in rules:
            if any(t in text for t in tokens):
                out.append((graph, why))
        return out


def request_ai_graph_advice(endpoint: str, model: str, api_key: str, project_summary: str,
                            dataset_schema: dict, timeout: float = 40.0) -> dict:
    """Call a user-configured OpenAI-compatible chat-completions endpoint.

    This function is provider-neutral. It sends only the compact project summary
    and dataset schema supplied by the caller, never raw datasets. The caller is
    responsible for obtaining explicit user consent before invoking it.
    """
    import urllib.request
    prompt = (
        "You are a scientific visualization advisor. Use the project context and dataset schema to recommend "
        "up to 8 graph types. Return JSON only with keys recommendations (list of objects with graph and reason), "
        "x_variable, y_variable, z_variable, and project_interpretation. Prefer graphs that reveal the project's "
        "scientific questions; do not invent variables.\n\nPROJECT CONTEXT:\n" + project_summary +
        "\n\nDATASET SCHEMA:\n" + json.dumps(dataset_schema, ensure_ascii=False)[:12000]
    )
    payload = json.dumps({
        "model": model,
        "messages": [
            {"role": "system", "content": "Return concise valid JSON only. Do not fabricate measurements."},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.15,
    }).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"
    req = urllib.request.Request(endpoint, data=payload, headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=float(timeout)) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
    doc = json.loads(raw)
    content = doc.get("choices", [{}])[0].get("message", {}).get("content", "")
    if not content:
        raise ValueError("AI endpoint returned no message content")
    # tolerate fenced JSON from compatible local servers
    content = re.sub(r"^\s*```(?:json)?\s*|\s*```\s*$", "", content.strip(), flags=re.I | re.S)
    try:
        parsed = json.loads(content)
    except Exception:
        parsed = {"project_interpretation": content, "recommendations": []}
    parsed["raw_response"] = content
    return parsed
