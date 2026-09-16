"""Chart-to-data and chart-to-code utilities for GraphVis 18."""
from __future__ import annotations
from dataclasses import dataclass
from typing import Iterable
import numpy as np
import pandas as pd
from PIL import Image

@dataclass(slots=True)
class AxisCalibration:
    left:float; top:float; right:float; bottom:float
    x_min:float; x_max:float; y_min:float; y_max:float
    x_scale:str="linear"; y_scale:str="linear"
    def _forward(self,t,lo,hi,scale):
        key=str(scale).lower()
        if key in {"log","log10"}:
            if lo<=0 or hi<=0: raise ValueError("Log calibration requires positive limits")
            return np.power(10,np.log10(lo)+t*(np.log10(hi)-np.log10(lo)))
        if key in {"reciprocal","1/t","arrhenius"}:
            a,b=1/lo,1/hi; inv=a+t*(b-a); return 1/inv
        if key in {"symlog","asinh"}:
            a,b=np.arcsinh(lo),np.arcsinh(hi); return np.sinh(a+t*(b-a))
        return lo+t*(hi-lo)
    def map_pixels(self,xpx,ypx):
        tx=(np.asarray(xpx,float)-self.left)/max(self.right-self.left,1e-12)
        ty=(self.bottom-np.asarray(ypx,float))/max(self.bottom-self.top,1e-12)
        return self._forward(tx,self.x_min,self.x_max,self.x_scale),self._forward(ty,self.y_min,self.y_max,self.y_scale)

class ChartDerenderer:
    def __init__(self,image_path:str):
        self.path=str(image_path); self.rgb=np.asarray(Image.open(image_path).convert("RGB"),dtype=np.float32)
    def trace_colour(self,rgb:Iterable[float],calibration:AxisCalibration,*,tolerance:float=45.0,statistic:str="median") -> pd.DataFrame:
        target=np.asarray(list(rgb),float)[:3]; dist=np.linalg.norm(self.rgb-target[None,None,:],axis=2); mask=dist<=float(tolerance)
        ys,xs=np.nonzero(mask); rows=[]
        if xs.size==0:return pd.DataFrame(columns=["x","y"])
        for x in np.unique(xs):
            ycol=ys[xs==x]; y=float(np.mean(ycol) if statistic.lower().startswith("mean") else np.median(ycol)); rows.append((float(x),y))
        px=np.asarray([r[0] for r in rows]); py=np.asarray([r[1] for r in rows]); x,y=calibration.map_pixels(px,py)
        return pd.DataFrame({"x":x,"y":y,"pixel_x":px,"pixel_y":py})
    def extract_heatmap_matrix(self,bbox:tuple[int,int,int,int],*,width:int=160,height:int=120,grayscale:bool=False)->np.ndarray:
        l,t,r,b=map(int,bbox); crop=np.clip(self.rgb[t:b,l:r],0,255).astype(np.uint8); im=Image.fromarray(crop).resize((int(width),int(height)),Image.Resampling.BILINEAR); a=np.asarray(im,dtype=float)/255.0
        if grayscale:return np.dot(a[...,:3],[0.2126,0.7152,0.0722])
        return a
    @staticmethod
    def calibration_from_observation(obs,image_shape:tuple[int,int],*,fallback_bbox=None)->AxisCalibration:
        h,w=image_shape[:2]; bbox=obs.plot_bbox or fallback_bbox
        if bbox is None: bbox=[.12,.08,.93,.88]
        # VLM bbox is normalized when values are <=1.5.
        l,t,r,b=map(float,bbox)
        if max(abs(l),abs(t),abs(r),abs(b))<=1.5: l,r=l*w,r*w; t,b=t*h,b*h
        if obs.x_range is None or obs.y_range is None: raise ValueError("Automatic chart calibration needs visible numeric x_range and y_range")
        return AxisCalibration(l,t,r,b,*obs.x_range,*obs.y_range,obs.x_scale,obs.y_scale)


def generate_reconstruction_code(frame:pd.DataFrame,*,backend:str="fastplotlib",title:str="Reconstructed literature figure",x:str="x",y:str="y") -> str:
    """Generate a self-contained reconstruction script without embedding source data files."""
    backend=backend.lower(); records=frame[[x,y]].dropna().to_dict(orient="list")
    if backend in {"fastplotlib","wgpu","pygfx"}:
        return f'''import numpy as np\nimport fastplotlib as fpl\nx=np.array({records[x]!r},dtype=np.float32)\ny=np.array({records[y]!r},dtype=np.float32)\nfig=fpl.Figure(size=(900,650))\nfig[0,0].add_line(np.column_stack([x,y]),thickness=2.0)\nfig.show()\nfpl.loop.run()\n'''
    if backend in {"pyvista","vtk"}:
        return f'''import numpy as np\nimport pyvista as pv\nx=np.array({records[x]!r},dtype=float); y=np.array({records[y]!r},dtype=float)\npts=np.column_stack([x,y,np.zeros_like(x)])\nplotter=pv.Plotter(); plotter.add_lines(pts,connected=True,width=2); plotter.show_grid(); plotter.show()\n'''
    return f'''import numpy as np\nimport matplotlib.pyplot as plt\nx=np.array({records[x]!r},dtype=float); y=np.array({records[y]!r},dtype=float)\nfig,ax=plt.subplots(figsize=(9,6.5)); ax.plot(x,y); ax.set_title({title!r}); ax.grid(True,alpha=.25); fig.tight_layout(); plt.show()\n'''
