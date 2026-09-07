"""End-to-end literature intelligence orchestration for GraphVis 18."""
from __future__ import annotations
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any
import json, re
import numpy as np
import pandas as pd
from graphvis_science.literature.extractor import extract_literature, LiteratureExtraction
from graphvis_science.literature.vlm import VLMProvider, VLMObservation
from graphvis_science.literature.derender import ChartDerenderer, generate_reconstruction_code

@dataclass(slots=True)
class FigureAsset:
    page:int
    path:str
    caption:str=""
    observation:VLMObservation|None=None
    reconstructed_data:str|None=None
    reconstructed_code:str|None=None
    diagnostics:dict[str,Any]=field(default_factory=dict)

@dataclass(slots=True)
class LiteratureIntelligenceResult:
    extraction:LiteratureExtraction
    figures:list[FigureAsset]=field(default_factory=list)
    semantic_links:list[dict[str,Any]]=field(default_factory=list)


def extract_pdf_figure_images(pdf_path:str,output_dir:str,*,min_pixels:int=20_000)->list[FigureAsset]:
    """Extract embedded raster figures directly with PyMuPDF before considering OCR."""
    try: import pymupdf as fitz
    except ImportError: import fitz
    out=[]; root=Path(output_dir); root.mkdir(parents=True,exist_ok=True); doc=fitz.open(pdf_path); seen=set()
    for page_i,page in enumerate(doc,1):
        text=page.get_text("text") or ""
        captions=[ln.strip() for ln in text.splitlines() if re.match(r"^(fig(?:ure)?\.?\s*\d+)",ln.strip(),re.I)]
        for j,img in enumerate(page.get_images(full=True)):
            xref=img[0]
            if xref in seen: continue
            seen.add(xref); pix=fitz.Pixmap(doc,xref)
            if pix.width*pix.height<min_pixels: continue
            path=root/f"page_{page_i:03d}_figure_{j+1:02d}.png"
            if pix.alpha: pix=fitz.Pixmap(fitz.csRGB,pix)
            pix.save(str(path)); out.append(FigureAsset(page_i,str(path),captions[min(j,len(captions)-1)] if captions else ""))
    doc.close(); return out

class LiteratureIntelligencePipeline:
    def __init__(self,vlm:VLMProvider|None=None): self.vlm=vlm
    def analyze(self,path:str,output_dir:str,*,use_vlm:bool=True,extract_figures:bool=True,progress=None)->LiteratureIntelligenceResult:
        root=Path(output_dir); root.mkdir(parents=True,exist_ok=True)
        ext=extract_literature(path,extract_dataset=True,ocr=True,progress=progress,output_dir=str(root/"literature_dataset"))
        result=LiteratureIntelligenceResult(ext)
        if extract_figures and str(path).lower().endswith(".pdf"):
            result.figures=extract_pdf_figure_images(path,str(root/"figures"))
        paper_context=(ext.text[:18_000] if ext.text else "")
        for fig in result.figures:
            if use_vlm and self.vlm is not None:
                prompt=f"Caption: {fig.caption}\nPaper context excerpt:\n{paper_context}\nRelate this figure to the methods and operating conditions."
                try: fig.observation=self.vlm.analyze_figure(fig.path,prompt)
                except Exception as exc: fig.diagnostics["vlm_error"]=f"{type(exc).__name__}: {exc}"
            if fig.observation:
                obs=fig.observation
                result.semantic_links.append({"page":fig.page,"figure":fig.path,"plot_type":obs.plot_type,"methodology_links":obs.methodology_links,"conditions":obs.conditions})
        side=root/"literature_intelligence.json"
        side.write_text(json.dumps({"source":path,"figures":[{"page":f.page,"path":f.path,"caption":f.caption,"observation":asdict(f.observation) if f.observation else None,"diagnostics":f.diagnostics} for f in result.figures],"semantic_links":result.semantic_links},indent=2,default=str),encoding="utf-8")
        return result
    def derender_coloured_series(self,figure:FigureAsset,rgb:tuple[int,int,int],*,tolerance:float=45.0,backend:str="fastplotlib")->pd.DataFrame:
        if not figure.observation: raise ValueError("Figure needs a VLM/manual calibration observation before de-rendering")
        der=ChartDerenderer(figure.path); cal=der.calibration_from_observation(figure.observation,der.rgb.shape); frame=der.trace_colour(rgb,cal,tolerance=tolerance)
        out=Path(figure.path).with_name(Path(figure.path).stem+"_reconstructed.csv"); frame.to_csv(out,index=False); figure.reconstructed_data=str(out)
        code=generate_reconstruction_code(frame,backend=backend,title=figure.caption or "Reconstructed literature figure"); code_path=out.with_suffix(".py"); code_path.write_text(code,encoding="utf-8"); figure.reconstructed_code=str(code_path); return frame
