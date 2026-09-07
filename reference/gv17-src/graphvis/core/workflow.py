"""Reusable linked analysis workflows for GraphVis projects."""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Callable, Mapping

import pandas as pd

from graphvis.core.logging import get_logger

LOG = get_logger("workflow")


@dataclass(slots=True)
class WorkflowStep:
    operation: str
    inputs: dict[str, Any] = field(default_factory=dict)
    output: str = ""
    enabled: bool = True


@dataclass(slots=True)
class AnalysisWorkflow:
    name: str
    steps: list[WorkflowStep] = field(default_factory=list)
    metadata: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {"name":self.name,"steps":[asdict(s) for s in self.steps],"metadata":self.metadata}

    @classmethod
    def from_dict(cls,data: Mapping[str,Any]) -> "AnalysisWorkflow":
        return cls(str(data.get("name","Workflow")),[WorkflowStep(**s) for s in data.get("steps",[])],dict(data.get("metadata",{})))


class WorkflowRegistry:
    def __init__(self) -> None:
        self.operations: dict[str,Callable[...,Any]]={}

    def register(self,name: str,fn: Callable[...,Any]) -> None: self.operations[str(name)]=fn
    def get(self,name: str) -> Callable[...,Any]:
        if name not in self.operations: raise KeyError(f"Unknown workflow operation: {name}")
        return self.operations[name]


class WorkflowEngine:
    def __init__(self,registry: WorkflowRegistry | None = None) -> None:
        self.registry=registry or WorkflowRegistry()

    def execute(self,workflow: AnalysisWorkflow,context: dict[str,Any] | None = None) -> dict[str,Any]:
        ctx=dict(context or {})
        for i,step in enumerate(workflow.steps):
            if not step.enabled: continue
            fn=self.registry.get(step.operation); kwargs={}
            for key,value in step.inputs.items():
                if isinstance(value,str) and value.startswith("$"):
                    kwargs[key]=ctx[value[1:]]
                else: kwargs[key]=value
            LOG.info("Workflow %s step %s: %s",workflow.name,i+1,step.operation)
            result=fn(**kwargs)
            ctx[step.output or f"step_{i+1}"]=result
        return ctx


class WorkflowStore:
    def __init__(self,directory: str | Path) -> None:
        self.directory=Path(directory); self.directory.mkdir(parents=True,exist_ok=True)
    def save(self,workflow: AnalysisWorkflow) -> Path:
        safe="".join(c if c.isalnum() or c in "-_" else "_" for c in workflow.name).strip("_") or "workflow"
        path=self.directory/f"{safe}.json"; path.write_text(json.dumps(workflow.to_dict(),indent=2,ensure_ascii=False),encoding="utf-8"); return path
    def load(self,path: str | Path) -> AnalysisWorkflow: return AnalysisWorkflow.from_dict(json.loads(Path(path).read_text(encoding="utf-8")))
    def list(self) -> list[Path]: return sorted(self.directory.glob("*.json"))


def default_workflow_registry() -> WorkflowRegistry:
    """Return the built-in GraphVis analysis-chain operation registry.

    Workflow step inputs may reference prior outputs as ``$name``. The initial
    context supplied by the UI contains ``dataset`` and ``frame``.
    """
    registry = WorkflowRegistry()

    def select_column(frame: pd.DataFrame, column: str):
        return frame[str(column)].to_numpy()

    def descriptive(values):
        from graphvis.analysis.statistics import StatisticalEngine
        return StatisticalEngine.descriptive(values)

    def savgol(values, x=None, window: int = 11, polyorder: int = 3):
        from graphvis.analysis.signal import SignalEngine
        return SignalEngine.savgol(values, x, window=window, polyorder=polyorder).y

    def linear_fit(x, y):
        from graphvis.analysis.fitting import CurveFittingEngine
        return CurveFittingEngine.fit(x, y, model="linear")

    def pca(frame: pd.DataFrame, columns: list[str] | None = None, components: int = 2):
        from graphvis.analysis.ml import MultivariateEngine
        return MultivariateEngine.pca(frame, columns, n_components=components)

    registry.register("select_column", select_column)
    registry.register("descriptive", descriptive)
    registry.register("savgol", savgol)
    registry.register("linear_fit", linear_fit)
    registry.register("pca", pca)
    return registry
