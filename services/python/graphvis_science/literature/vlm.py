"""Pluggable multimodal/VLM providers for GraphVis literature intelligence.

No provider is mandatory. GraphVis can run fully offline with deterministic PDF
parsing and chart extraction, while users may opt into a local Transformers VLM
or an OpenAI-compatible HTTPS endpoint. API keys are read from the environment
or supplied by the caller and are never persisted by this module.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Protocol, Any
from pathlib import Path
import base64, json, os, mimetypes, urllib.request

@dataclass(slots=True)
class VLMObservation:
    plot_type:str="unknown"
    confidence:float=0.0
    x_label:str=""; y_label:str=""; z_label:str=""; color_label:str=""
    x_scale:str="linear"; y_scale:str="linear"; z_scale:str="linear"
    x_range:tuple[float,float]|None=None; y_range:tuple[float,float]|None=None
    legend:list[str]=field(default_factory=list)
    methodology_links:list[str]=field(default_factory=list)
    conditions:dict[str,Any]=field(default_factory=dict)
    plot_bbox:list[float]|None=None
    raw:dict[str,Any]=field(default_factory=dict)

class VLMProvider(Protocol):
    name:str
    def analyze_figure(self,image_path:str,prompt:str)->VLMObservation: ...

_SYSTEM_PROMPT="""You are reading a scientific figure for GraphVis. Return JSON only. Identify plot_type, axis labels, axis scales (linear/log10/symlog/reciprocal), numeric axis ranges when visible, legend series, plot bounding box as normalized [left,top,right,bottom], experimental conditions visible in the figure/caption, and concise links between the figure and methodology. Never invent unreadable values; use null/empty values when uncertain."""

def _coerce(payload:dict)->VLMObservation:
    def pair(v):
        try:
            if v is not None and len(v)==2:return (float(v[0]),float(v[1]))
        except Exception:pass
        return None
    return VLMObservation(
        plot_type=str(payload.get("plot_type","unknown")), confidence=float(payload.get("confidence",0.0) or 0.0),
        x_label=str(payload.get("x_label","") or ""), y_label=str(payload.get("y_label","") or ""), z_label=str(payload.get("z_label","") or ""), color_label=str(payload.get("color_label","") or ""),
        x_scale=str(payload.get("x_scale","linear") or "linear"), y_scale=str(payload.get("y_scale","linear") or "linear"), z_scale=str(payload.get("z_scale","linear") or "linear"),
        x_range=pair(payload.get("x_range")), y_range=pair(payload.get("y_range")), legend=list(payload.get("legend") or []),
        methodology_links=list(payload.get("methodology_links") or []), conditions=dict(payload.get("conditions") or {}),
        plot_bbox=list(payload.get("plot_bbox")) if payload.get("plot_bbox") else None, raw=payload)

class OpenAICompatibleVLM:
    name="OpenAI-compatible VLM"
    def __init__(self, endpoint:str, model:str, api_key:str|None=None, *, timeout:float=90.0):
        self.endpoint=endpoint.rstrip("/"); self.model=model; self.api_key=api_key or os.getenv("GRAPHVIS_VLM_API_KEY",""); self.timeout=float(timeout)
    def analyze_figure(self,image_path:str,prompt:str="")->VLMObservation:
        data=Path(image_path).read_bytes(); mime=mimetypes.guess_type(image_path)[0] or "image/png"; b64=base64.b64encode(data).decode("ascii")
        body={"model":self.model,"messages":[{"role":"system","content":_SYSTEM_PROMPT},{"role":"user","content":[{"type":"text","text":prompt or "Analyze this scientific figure."},{"type":"image_url","image_url":{"url":f"data:{mime};base64,{b64}"}}]}],"temperature":0,"response_format":{"type":"json_object"}}
        req=urllib.request.Request(self.endpoint,data=json.dumps(body).encode(),headers={"Content-Type":"application/json",**({"Authorization":f"Bearer {self.api_key}"} if self.api_key else {})},method="POST")
        with urllib.request.urlopen(req,timeout=self.timeout) as r: payload=json.loads(r.read().decode("utf-8"))
        content=payload.get("choices",[{}])[0].get("message",{}).get("content","{}")
        if isinstance(content,list): content="".join(str(p.get("text",p)) for p in content)
        return _coerce(json.loads(content))

class LocalTransformersVLM:
    name="Local Transformers VLM"
    def __init__(self, model_id:str, *, device_map:str="auto", trust_remote_code:bool=False):
        self.model_id=model_id; self.device_map=device_map; self.trust_remote_code=trust_remote_code; self._model=None; self._processor=None
    def _load(self):
        if self._model is not None:return
        from transformers import AutoProcessor, AutoModelForImageTextToText
        self._processor=AutoProcessor.from_pretrained(self.model_id,trust_remote_code=self.trust_remote_code)
        self._model=AutoModelForImageTextToText.from_pretrained(self.model_id,device_map=self.device_map,trust_remote_code=self.trust_remote_code)
    def analyze_figure(self,image_path:str,prompt:str="")->VLMObservation:
        self._load(); from PIL import Image
        image=Image.open(image_path).convert("RGB"); text=_SYSTEM_PROMPT+"\n"+(prompt or "Analyze this scientific figure.")
        messages=[{"role":"user","content":[{"type":"image","image":image},{"type":"text","text":text}]}]
        rendered=self._processor.apply_chat_template(messages,add_generation_prompt=True,tokenize=False)
        inputs=self._processor(text=[rendered],images=[image],return_tensors="pt").to(self._model.device)
        out=self._model.generate(**inputs,max_new_tokens=1200,do_sample=False)
        decoded=self._processor.batch_decode(out,skip_special_tokens=True)[0]
        start=decoded.find("{"); end=decoded.rfind("}")
        if start<0 or end<start: raise ValueError("Local VLM did not return a JSON object")
        return _coerce(json.loads(decoded[start:end+1]))
