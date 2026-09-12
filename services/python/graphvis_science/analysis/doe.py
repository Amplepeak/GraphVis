"""Design-of-experiments and parameter-sweep engine for GraphVis 18."""
from __future__ import annotations
from dataclasses import dataclass, field
from itertools import product
from typing import Callable, Mapping, Sequence, Any
import numpy as np
import pandas as pd
from scipy.stats import qmc

@dataclass(slots=True)
class DesignResult:
    design: pd.DataFrame
    kind: str
    diagnostics: dict[str, Any] = field(default_factory=dict)


def _bounds_frame(unit: np.ndarray, bounds: Mapping[str, tuple[float,float]]) -> pd.DataFrame:
    names=list(bounds); lo=np.array([bounds[n][0] for n in names],float); hi=np.array([bounds[n][1] for n in names],float)
    if np.any(~np.isfinite(lo)) or np.any(~np.isfinite(hi)) or np.any(hi<=lo):
        raise ValueError("Every DOE bound must be finite with low < high.")
    scaled=qmc.scale(unit,lo,hi)
    return pd.DataFrame(scaled,columns=names)


class DOEEngine:
    @staticmethod
    def latin_hypercube(bounds: Mapping[str,tuple[float,float]], n: int, *, strength: int = 1,
                        optimization: str | None = "random-cd", seed: int | None = None) -> DesignResult:
        sampler=qmc.LatinHypercube(d=len(bounds),strength=int(strength),optimization=optimization,seed=seed)
        unit=sampler.random(int(n)); df=_bounds_frame(unit,bounds)
        return DesignResult(df,"Latin Hypercube",{"discrepancy":float(qmc.discrepancy(unit)),"n":len(df)})

    @staticmethod
    def sobol(bounds: Mapping[str,tuple[float,float]], n: int, *, scramble: bool = True, seed: int | None = None) -> DesignResult:
        sampler=qmc.Sobol(d=len(bounds),scramble=scramble,seed=seed)
        m=int(np.ceil(np.log2(max(2,int(n))))); unit=sampler.random_base2(m)[:int(n)]; df=_bounds_frame(unit,bounds)
        return DesignResult(df,"Sobol",{"discrepancy":float(qmc.discrepancy(unit)),"n":len(df)})

    @staticmethod
    def full_factorial(levels: Mapping[str, Sequence[float] | int]) -> DesignResult:
        """Every combination of the given factor levels.

        Accepts either shape, because both are natural and the field's own hint
        advertised the one this could not read:

            {"temperature": [20, 30, 40], "pH": [6, 7]}   explicit levels
            {"temperature": 3, "pH": 2}                   a count per factor

        A count becomes that many evenly spaced coded levels from -1 to +1,
        which is the DOE convention and the only thing a bare number can mean
        when no units have been given. Before this, a count raised
        "TypeError: 'int' object is not iterable" from inside itertools, naming
        nothing the user had typed - and a count was exactly what the hint told
        them to type, so the documented input was the broken one.
        """
        if not levels:
            raise ValueError("No factor levels supplied.")
        names = list(levels)
        expanded: list[list[float]] = []
        for name in names:
            value = levels[name]
            if isinstance(value, bool):
                raise ValueError(f"'{name}' needs a level count or a list of levels.")
            if isinstance(value, (int, float)):
                count = int(value)
                if count < 2:
                    raise ValueError(
                        f"'{name}' needs at least 2 levels; {count} was given.")
                expanded.append([float(v) for v in np.linspace(-1.0, 1.0, count)])
                continue
            try:
                points = [float(v) for v in value]
            except (TypeError, ValueError) as exc:
                raise ValueError(
                    f"'{name}' needs a level count or a list of levels, "
                    f"not {value!r}."
                ) from exc
            if len(points) < 2:
                raise ValueError(f"'{name}' needs at least 2 levels.")
            expanded.append(points)

        rows = list(product(*expanded))
        if not rows: raise ValueError("No factor levels supplied.")
        return DesignResult(pd.DataFrame(rows,columns=names),"Full factorial",{"runs":len(rows)})

    @staticmethod
    def central_composite(bounds: Mapping[str,tuple[float,float]], *, alpha: str|float="rotatable", center_points: int = 5) -> DesignResult:
        names=list(bounds); k=len(names)
        a=np.sqrt(k) if str(alpha).lower()=="rotatable" else float(alpha)
        coded=list(product([-1.0,1.0],repeat=k))
        for i in range(k):
            p=np.zeros(k); p[i]=a; coded.append(tuple(p)); coded.append(tuple(-p))
        coded.extend([tuple(np.zeros(k))]*max(1,int(center_points)))
        C=np.asarray(coded,float); lo=np.array([bounds[n][0] for n in names]); hi=np.array([bounds[n][1] for n in names]); mid=(lo+hi)/2; half=(hi-lo)/2
        X=mid+C*half
        return DesignResult(pd.DataFrame(X,columns=names),"Central composite",{"alpha":float(a),"center_points":center_points,"runs":len(X)})

    @staticmethod
    def recommend(existing: pd.DataFrame, parameter_columns: Sequence[str], response: str | None = None) -> dict[str,Any]:
        n=len(existing); p=len(parameter_columns); missing=float(existing[list(parameter_columns)].isna().mean().mean()) if p else 0.0
        unique=[existing[c].nunique(dropna=True) for c in parameter_columns]
        if p>=3 and n>=10*p and all(u>max(5,n*0.15) for u in unique):
            kind="Latin Hypercube / Sobol continuation"; reason="Existing parameters behave as continuous multi-dimensional sweeps."
        elif p<=5 and all(u<=5 for u in unique):
            kind="Factorial / response-surface design"; reason="Factors have a small discrete level count, making interaction estimates efficient."
        else:
            kind="Space-filling Latin Hypercube"; reason="Mixed coverage is best extended with a space-filling design."
        return {"recommended_design":kind,"reason":reason,"rows":n,"parameters":p,"missing_fraction":missing,"response":response}
