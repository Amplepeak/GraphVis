"""Curve, peak and surface fitting services for GraphVis."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Iterable, Sequence

import numpy as np
# NumPy 2.0 removed ``trapz``; ``trapezoid`` is its exact replacement.
_np_trapz = getattr(np, "trapezoid", getattr(np, "trapz", None))

import pandas as pd
from scipy import optimize, signal, special

from graphvis.core.logging import get_logger

LOG = get_logger("fitting")


@dataclass(slots=True)
class FitResult:
    name: str
    parameters: dict[str, float]
    x: np.ndarray
    y: np.ndarray
    predicted: np.ndarray
    residuals: np.ndarray
    r2: float
    rmse: float
    covariance: np.ndarray | None = None
    details: dict[str, Any] = field(default_factory=dict)


def _clean_xy(x: Iterable[float], y: Iterable[float]) -> tuple[np.ndarray,np.ndarray]:
    xx=np.asarray(x,float).ravel(); yy=np.asarray(y,float).ravel(); n=min(len(xx),len(yy)); xx,yy=xx[:n],yy[:n]; ok=np.isfinite(xx)&np.isfinite(yy); return xx[ok],yy[ok]


def _metrics(y: np.ndarray, pred: np.ndarray) -> tuple[float,float]:
    res=y-pred; ss=float(np.sum(res**2)); total=float(np.sum((y-np.mean(y))**2)); return 1-ss/max(total,1e-15), float(np.sqrt(np.mean(res**2)))


def _logistic4(x,bottom,top,ec50,hill):
    return bottom+(top-bottom)/(1+np.power(np.maximum(ec50,1e-15)/np.maximum(x,1e-15),hill))


def _exp1(x,a,k,c): return a*np.exp(k*x)+c

def _gaussian(x,amp,center,sigma,offset): return offset+amp*np.exp(-0.5*((x-center)/max(abs(sigma),1e-15))**2)

def _lorentzian(x,amp,center,gamma,offset): return offset+amp*(gamma**2/((x-center)**2+gamma**2))

def _pvoigt(x,amp,center,width,eta,offset):
    sigma=max(abs(width),1e-15)/2.354820045; gamma=max(abs(width),1e-15)/2
    g=np.exp(-0.5*((x-center)/sigma)**2); l=gamma**2/((x-center)**2+gamma**2)
    return offset+amp*(np.clip(eta,0,1)*l+(1-np.clip(eta,0,1))*g)


class CurveFittingEngine:
    @staticmethod
    def fit(x: Iterable[float], y: Iterable[float], model: str = "linear", degree: int = 2, initial: Sequence[float] | None = None) -> FitResult:
        xx,yy=_clean_xy(x,y)
        if len(xx)<3: raise ValueError("At least three finite points are required.")
        key=model.lower().strip()
        if key == "linear":
            coef=np.polyfit(xx,yy,1); pred=np.polyval(coef,xx); params={"slope":float(coef[0]),"intercept":float(coef[1])}; cov=None
        elif key.startswith("poly"):
            deg=max(1,int(degree)); coef=np.polyfit(xx,yy,deg); pred=np.polyval(coef,xx); params={f"c{deg-i}":float(v) for i,v in enumerate(coef)}; cov=None
        elif key in {"exponential","exp"}:
            p0=initial or [float(np.ptp(yy) or 1), -1/max(np.ptp(xx),1e-9), float(np.min(yy))]; popt,pcov=optimize.curve_fit(_exp1,xx,yy,p0=p0,maxfev=20000); pred=_exp1(xx,*popt); params=dict(zip(["a","k","c"],map(float,popt))); cov=pcov
        elif key in {"logistic","dose-response","dose response","4pl"}:
            positive=xx[xx>0]; ec=float(np.median(positive)) if positive.size else 1.0; p0=initial or [float(np.min(yy)),float(np.max(yy)),ec,1.0]
            popt,pcov=optimize.curve_fit(_logistic4,xx,yy,p0=p0,maxfev=30000); pred=_logistic4(xx,*popt); params=dict(zip(["bottom","top","ec50","hill"],map(float,popt))); cov=pcov
        else:
            raise ValueError(f"Unsupported built-in model: {model}")
        r2,rmse=_metrics(yy,pred); return FitResult(model,params,xx,yy,pred,yy-pred,float(r2),rmse,cov)

    @staticmethod
    def custom(x: Iterable[float], y: Iterable[float], function: Callable[..., np.ndarray], parameter_names: Sequence[str], initial: Sequence[float], bounds=(-np.inf,np.inf)) -> FitResult:
        xx,yy=_clean_xy(x,y); popt,pcov=optimize.curve_fit(function,xx,yy,p0=list(initial),bounds=bounds,maxfev=50000); pred=np.asarray(function(xx,*popt),float); r2,rmse=_metrics(yy,pred)
        return FitResult(getattr(function,"__name__","custom"),dict(zip(parameter_names,map(float,popt))),xx,yy,pred,yy-pred,r2,rmse,pcov)

    @staticmethod
    def global_linear(datasets: Sequence[tuple[Iterable[float],Iterable[float]]], shared_slope: bool = True) -> FitResult:
        clean=[_clean_xy(x,y) for x,y in datasets]; clean=[v for v in clean if len(v[0])>=2]
        if not clean: raise ValueError("No valid datasets.")
        if shared_slope:
            # y_ij = slope*x_ij + intercept_j
            rows=[]; targets=[]
            for j,(x,y) in enumerate(clean):
                for xv,yv in zip(x,y):
                    row=[float(xv)]+[0.0]*len(clean); row[1+j]=1.0; rows.append(row); targets.append(float(yv))
            beta,*_=np.linalg.lstsq(np.asarray(rows),np.asarray(targets),rcond=None); slope=float(beta[0]); params={"shared_slope":slope,**{f"intercept_{j+1}":float(beta[j+1]) for j in range(len(clean))}}
            xx=np.concatenate([c[0] for c in clean]); yy=np.concatenate([c[1] for c in clean]); pred=np.concatenate([slope*x+beta[j+1] for j,(x,_) in enumerate(clean)])
        else:
            xx=np.concatenate([c[0] for c in clean]); yy=np.concatenate([c[1] for c in clean]); coef=np.polyfit(xx,yy,1); pred=np.polyval(coef,xx); params={"slope":float(coef[0]),"intercept":float(coef[1])}
        r2,rmse=_metrics(yy,pred); return FitResult("Global linear fit",params,xx,yy,pred,yy-pred,r2,rmse)


@dataclass(slots=True)
class Peak:
    index: int
    x: float
    height: float
    prominence: float
    width: float
    fwhm: float
    area: float


class PeakEngine:
    @staticmethod
    def detect(x: Iterable[float], y: Iterable[float], prominence: float | None = None, distance: int | None = None) -> pd.DataFrame:
        xx,yy=_clean_xy(x,y); prom=float(prominence) if prominence is not None else max(float(np.nanstd(yy))*0.5, float(np.ptp(yy))*0.02)
        idx,props=signal.find_peaks(yy,prominence=prom,distance=distance); widths=signal.peak_widths(yy,idx,rel_height=0.5)
        rows=[]
        for j,k in enumerate(idx):
            left=max(0,int(np.floor(widths[2][j]))); right=min(len(yy)-1,int(np.ceil(widths[3][j]))); area=float(_np_trapz(np.maximum(yy[left:right+1],0),xx[left:right+1])) if right>left else 0.0
            dx=float(np.median(np.diff(xx))) if len(xx)>1 else 1.0; fwhm=float(widths[0][j]*abs(dx)); rows.append({"index":int(k),"x":float(xx[k]),"height":float(yy[k]),"prominence":float(props["prominences"][j]),"width_samples":float(widths[0][j]),"FWHM":fwhm,"area":area})
        return pd.DataFrame(rows)

    @staticmethod
    def deconvolve(x: Iterable[float], y: Iterable[float], n_peaks: int | None = None, profile: str = "gaussian", prominence: float | None = None) -> FitResult:
        xx,yy=_clean_xy(x,y); peaks=PeakEngine.detect(xx,yy,prominence=prominence)
        if peaks.empty: raise ValueError("No peaks detected.")
        if n_peaks is not None: peaks=peaks.nlargest(max(1,int(n_peaks)),"prominence").sort_values("x")
        base=float(np.percentile(yy,5)); span=max(np.ptp(xx),1e-9); model_key=profile.lower()
        if model_key.startswith("lor"):
            one=lambda x,a,c,w: a*(w**2/((x-c)**2+w**2)); names=("amp","center","gamma")
        elif model_key.startswith("pseudo") or model_key.startswith("voigt"):
            one=lambda x,a,c,w,e: a*(np.clip(e,0,1)*(w/2)**2/((x-c)**2+(w/2)**2)+(1-np.clip(e,0,1))*np.exp(-0.5*((x-c)/max(w/2.3548,1e-15))**2)); names=("amp","center","width","eta")
        else:
            one=lambda x,a,c,w: a*np.exp(-0.5*((x-c)/max(abs(w),1e-15))**2); names=("amp","center","sigma")
        stride=len(names)
        p0=[]; lo=[]; hi=[]
        for _,p in peaks.iterrows():
            width=max(float(p.get("FWHM",span/30))/(2.3548 if "sigma" in names else 2),span/1000)
            vals=[max(float(p["height"])-base,1e-9),float(p["x"]),width]+([0.5] if "eta" in names else [])
            p0.extend(vals); lo.extend([0,float(xx.min()),span/10000]+([0] if "eta" in names else [])); hi.extend([np.inf,float(xx.max()),span]+([1] if "eta" in names else []))
        p0.append(base); lo.append(-np.inf); hi.append(np.inf)
        def combo(x,*p):
            z=np.zeros_like(x,dtype=float)+p[-1]
            for i in range(len(peaks)): z+=one(x,*p[i*stride:(i+1)*stride])
            return z
        popt,pcov=optimize.curve_fit(combo,xx,yy,p0=p0,bounds=(lo,hi),maxfev=100000); pred=combo(xx,*popt); r2,rmse=_metrics(yy,pred); params={"offset":float(popt[-1])}
        peak_table=[]
        for i in range(len(peaks)):
            vals=popt[i*stride:(i+1)*stride]; d={f"peak_{i+1}_{k}":float(v) for k,v in zip(names,vals)}; params.update(d); curve=one(xx,*vals); area=float(_np_trapz(curve,xx)); peak_table.append({**{k:float(v) for k,v in zip(names,vals)},"area":area})
        return FitResult(f"{profile} peak deconvolution",params,xx,yy,pred,yy-pred,r2,rmse,pcov,details={"peaks":pd.DataFrame(peak_table)})


class SurfaceFittingEngine:
    @staticmethod
    def polynomial(x: Iterable[float], y: Iterable[float], z: Iterable[float], degree: int = 2) -> dict[str, Any]:
        x=np.asarray(x,float).ravel(); y=np.asarray(y,float).ravel(); z=np.asarray(z,float).ravel(); n=min(len(x),len(y),len(z)); x,y,z=x[:n],y[:n],z[:n]; ok=np.isfinite(x)&np.isfinite(y)&np.isfinite(z); x,y,z=x[ok],y[ok],z[ok]
        terms=[]; names=[]
        for i in range(degree+1):
            for j in range(degree+1-i): terms.append((x**i)*(y**j)); names.append(f"x^{i} y^{j}")
        A=np.column_stack(terms); coef,*_=np.linalg.lstsq(A,z,rcond=None); pred=A@coef; r2,rmse=_metrics(z,pred)
        return {"coefficients":dict(zip(names,map(float,coef))),"predicted":pred,"residuals":z-pred,"r2":float(r2),"rmse":float(rmse)}
