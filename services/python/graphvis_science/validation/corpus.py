"""Stress-corpus runner for GraphVis literature/chart reconstruction."""
from __future__ import annotations
from dataclasses import dataclass,field,asdict
from pathlib import Path
from typing import Callable,Any
import json,time
from graphvis_science.validation.visual_parity import compare_images

@dataclass(slots=True)
class CorpusCase:
    name:str
    source:str
    kind:str
    ground_truth:str|None=None

@dataclass(slots=True)
class CorpusReport:
    cases:int=0; successes:int=0; failures:int=0; rows:list[dict[str,Any]]=field(default_factory=list)
    @property
    def failure_rate(self): return self.failures/max(self.cases,1)
    def save(self,path): Path(path).write_text(json.dumps({**asdict(self),"failure_rate":self.failure_rate},indent=2),encoding="utf-8")

class LiteratureCorpusRunner:
    def __init__(self,processor:Callable[[CorpusCase],str|None]): self.processor=processor
    def run(self,cases:list[CorpusCase])->CorpusReport:
        report=CorpusReport(cases=len(cases))
        for case in cases:
            t=time.perf_counter(); row={"name":case.name,"kind":case.kind}
            try:
                output=self.processor(case); row["output"]=output; row["seconds"]=time.perf_counter()-t
                if case.ground_truth and output: row["visual_metrics"]=compare_images(case.ground_truth,output).as_dict()
                report.successes+=1; row["status"]="ok"
            except Exception as exc:
                report.failures+=1; row.update(status="failed",error=f"{type(exc).__name__}: {exc}",seconds=time.perf_counter()-t)
            report.rows.append(row)
        return report
