"""Visual/numerical parity metrics for literature reconstruction validation."""
from __future__ import annotations
from dataclasses import dataclass, asdict
from pathlib import Path
import numpy as np
from PIL import Image

@dataclass(slots=True)
class VisualMetrics:
    ssim:float
    psnr:float
    delta_e_mean:float
    delta_e_p95:float
    pixels:int
    def as_dict(self): return asdict(self)

def _rgb(path,shape=None):
    im=Image.open(path).convert("RGB")
    if shape is not None: im=im.resize((shape[1],shape[0]),Image.Resampling.LANCZOS)
    return np.asarray(im,dtype=np.uint8)

def compare_images(reference:str|Path,candidate:str|Path)->VisualMetrics:
    ref=_rgb(reference); cand=_rgb(candidate,ref.shape[:2])
    try:
        from skimage.metrics import structural_similarity, peak_signal_noise_ratio
        from skimage.color import rgb2lab, deltaE_ciede2000
        ssim=float(structural_similarity(ref,cand,channel_axis=2,data_range=255))
        psnr=float(peak_signal_noise_ratio(ref,cand,data_range=255))
        de=deltaE_ciede2000(rgb2lab(ref/255.0),rgb2lab(cand/255.0))
    except Exception:
        # Deterministic minimal fallback when scikit-image is not installed.
        d=(ref.astype(float)-cand.astype(float)); mse=float(np.mean(d*d)); psnr=float("inf") if mse==0 else 20*np.log10(255/np.sqrt(mse));
        # Global luminance/contrast proxy, explicitly not full SSIM.
        x=ref.mean(axis=2).ravel(); y=cand.mean(axis=2).ravel(); c1=6.5025;c2=58.5225; ssim=float(((2*x.mean()*y.mean()+c1)*(2*np.cov(x,y)[0,1]+c2))/((x.mean()**2+y.mean()**2+c1)*(x.var()+y.var()+c2)))
        de=np.linalg.norm(d/2.55,axis=2)
    return VisualMetrics(ssim,psnr,float(np.mean(de)),float(np.percentile(de,95)),int(ref.shape[0]*ref.shape[1]))

def numerical_parity(reference,candidate,*,relative_tolerance:float=.01,absolute_tolerance:float=1e-12)->dict:
    a=np.asarray(reference,float); b=np.asarray(candidate,float)
    if a.shape!=b.shape:return {"passed":False,"reason":f"shape mismatch {a.shape} != {b.shape}","within_fraction":0.0}
    finite=np.isfinite(a)&np.isfinite(b)
    if not finite.any():return {"passed":False,"reason":"no mutually finite values","within_fraction":0.0}
    err=np.abs(a[finite]-b[finite]); allowed=np.maximum(np.abs(a[finite])*relative_tolerance,absolute_tolerance); within=err<=allowed
    return {"passed":bool(np.all(within)),"within_fraction":float(np.mean(within)),"max_abs_error":float(err.max()),"max_relative_error":float(np.max(err/np.maximum(np.abs(a[finite]),absolute_tolerance))),"relative_tolerance":float(relative_tolerance),"n":int(finite.sum())}
