"""Signal processing and numerical-calculus backend for GraphVis."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Iterable

import numpy as np
# NumPy 2.0 removed ``trapz``; ``trapezoid`` is its exact replacement.
_np_trapz = getattr(np, "trapezoid", getattr(np, "trapz", None))

import pandas as pd
from scipy import integrate, signal


@dataclass(slots=True)
class SignalResult:
    name: str
    x: np.ndarray
    y: np.ndarray
    table: pd.DataFrame | None = None
    details: dict[str, Any] = field(default_factory=dict)


def _xy(y: Iterable[float], x: Iterable[float] | None = None) -> tuple[np.ndarray,np.ndarray]:
    yy=np.asarray(y,float).ravel(); xx=np.arange(len(yy),dtype=float) if x is None else np.asarray(x,float).ravel()[:len(yy)]
    n=min(len(xx),len(yy)); xx,yy=xx[:n],yy[:n]; ok=np.isfinite(xx)&np.isfinite(yy); return xx[ok],yy[ok]


class SignalEngine:
    @staticmethod
    def fft(y: Iterable[float], x: Iterable[float] | None = None) -> SignalResult:
        xx,yy=_xy(y,x); dt=float(np.median(np.diff(xx))) if len(xx)>1 else 1.0; spec=np.fft.rfft(yy); freq=np.fft.rfftfreq(len(yy),d=abs(dt) or 1.0)
        return SignalResult("FFT",freq,np.abs(spec),details={"complex_spectrum":spec,"phase":np.angle(spec),"sample_spacing":dt})

    @staticmethod
    def ifft(spectrum: Iterable[complex], sample_spacing: float = 1.0) -> SignalResult:
        sp=np.asarray(spectrum,complex).ravel(); y=np.fft.irfft(sp); x=np.arange(len(y))*float(sample_spacing)
        return SignalResult("IFFT",x,np.asarray(y,float))

    @staticmethod
    def stft(y: Iterable[float], x: Iterable[float] | None = None, nperseg: int = 256) -> SignalResult:
        xx,yy=_xy(y,x); dt=float(np.median(np.diff(xx))) if len(xx)>1 else 1.0; fs=1.0/(abs(dt) or 1.0); f,t,z=signal.stft(yy,fs=fs,nperseg=min(max(8,nperseg),len(yy)))
        table=pd.DataFrame(np.abs(z),index=f,columns=t)
        return SignalResult("STFT",t,np.mean(np.abs(z),axis=0),table=table,details={"frequency":f,"time":t,"complex_stft":z,"fs":fs})

    @staticmethod
    def hilbert(y: Iterable[float], x: Iterable[float] | None = None) -> SignalResult:
        xx,yy=_xy(y,x); analytic=signal.hilbert(yy); return SignalResult("Hilbert transform envelope",xx,np.abs(analytic),details={"analytic":analytic,"phase":np.unwrap(np.angle(analytic))})

    @staticmethod
    def iir_filter(y: Iterable[float], x: Iterable[float] | None = None, cutoff: float | tuple[float,float] = 0.1, order: int = 4, kind: str = "lowpass", fs: float | None = None) -> SignalResult:
        xx,yy=_xy(y,x)
        if fs is None:
            dt=float(np.median(np.diff(xx))) if len(xx)>1 else 1.0; fs=1.0/(abs(dt) or 1.0)
        wn=np.asarray(cutoff,float); b,a=signal.butter(int(order),wn,btype=kind,fs=float(fs)); filt=signal.filtfilt(b,a,yy) if len(yy)>3*max(len(a),len(b)) else signal.lfilter(b,a,yy)
        return SignalResult(f"IIR {kind}",xx,filt,details={"b":b,"a":a,"fs":fs})

    @staticmethod
    def savgol(y: Iterable[float], x: Iterable[float] | None = None, window: int = 11, polyorder: int = 3) -> SignalResult:
        xx,yy=_xy(y,x); window=max(polyorder+2,int(window)); window += 1-window%2; window=min(window,len(yy) if len(yy)%2 else len(yy)-1); out=signal.savgol_filter(yy,window,polyorder) if window>polyorder else yy.copy()
        return SignalResult("Savitzky-Golay",xx,out,details={"window":window,"polyorder":polyorder})

    @staticmethod
    def lowess(y: Iterable[float], x: Iterable[float] | None = None, frac: float = 0.15) -> SignalResult:
        xx,yy=_xy(y,x)
        try:
            from statsmodels.nonparametric.smoothers_lowess import lowess
        except Exception as exc:
            raise RuntimeError("LOWESS/LOESS requires statsmodels.") from exc
        out=lowess(yy,xx,frac=float(frac),return_sorted=True); return SignalResult("LOWESS",out[:,0],out[:,1],details={"frac":frac})

    @staticmethod
    def baseline_subtract(y: Iterable[float], x: Iterable[float] | None = None, method: str = "polynomial", degree: int = 2, lam: float = 1e5, p: float = 0.01) -> SignalResult:
        xx,yy=_xy(y,x)
        if method.lower().startswith("poly"):
            coef=np.polyfit(xx,yy,int(degree)); baseline=np.polyval(coef,xx)
        else:
            # asymmetric least squares baseline
            from scipy import sparse
            from scipy.sparse.linalg import spsolve
            L=len(yy); D=sparse.diags([1,-2,1],[0,-1,-2],shape=(L,L-2)); w=np.ones(L)
            for _ in range(10):
                W=sparse.spdiags(w,0,L,L); z=spsolve(W+lam*D@D.T,w*yy); w=p*(yy>z)+(1-p)*(yy<z)
            baseline=np.asarray(z)
        return SignalResult("Baseline subtraction",xx,yy-baseline,details={"baseline":baseline,"method":method})


class CalculusEngine:
    @staticmethod
    def derivative(y: Iterable[float], x: Iterable[float] | None = None, order: int = 1) -> SignalResult:
        xx,yy=_xy(y,x); out=yy.copy()
        for _ in range(max(1,int(order))): out=np.gradient(out,xx)
        return SignalResult(f"Derivative order {order}",xx,out)

    @staticmethod
    def integral(y: Iterable[float], x: Iterable[float] | None = None) -> SignalResult:
        xx,yy=_xy(y,x); cum=np.r_[0.0,integrate.cumulative_trapezoid(yy,xx)]; return SignalResult("Cumulative integral",xx,cum,details={"total":float(integrate.trapezoid(yy,xx))})

    @staticmethod
    def polygon_area(x: Iterable[float], y: Iterable[float]) -> float:
        xx,yy=_xy(y,x); return float(0.5*abs(np.dot(xx,np.roll(yy,1))-np.dot(yy,np.roll(xx,1))))

    @staticmethod
    def surface_area(z: np.ndarray, x: np.ndarray | None = None, y: np.ndarray | None = None) -> float:
        Z=np.asarray(z,float); gy,gx=np.gradient(Z); density=np.sqrt(1+gx**2+gy**2); return float(np.nansum(density))

    @staticmethod
    def volume_2d(z: np.ndarray, x: np.ndarray | None = None, y: np.ndarray | None = None) -> float:
        Z=np.asarray(z,float); X=np.arange(Z.shape[1]) if x is None else np.asarray(x,float); Y=np.arange(Z.shape[0]) if y is None else np.asarray(y,float)
        return float(_np_trapz(_np_trapz(Z,X,axis=1),Y,axis=0))
