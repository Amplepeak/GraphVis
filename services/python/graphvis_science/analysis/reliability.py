"""Reliability, uncertainty and Monte-Carlo analysis for GraphVis 18."""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Callable, Mapping, Any, Iterable
import numpy as np
import pandas as pd
from scipy import stats

@dataclass(slots=True)
class ReliabilityResult:
    name: str
    table: pd.DataFrame
    details: dict[str,Any]=field(default_factory=dict)

class ReliabilityEngine:
    @staticmethod
    def weibull(values: Iterable[float], *, confidence: float=.95) -> ReliabilityResult:
        x=np.asarray(values,float).ravel(); x=x[np.isfinite(x)&(x>0)]
        if len(x)<3: raise ValueError("Weibull fitting requires at least 3 positive finite observations.")
        shape,loc,scale=stats.weibull_min.fit(x,floc=0)
        q=np.linspace(.01,.99,99); t=stats.weibull_min.ppf(q,shape,loc=0,scale=scale); reliability=1-q
        table=pd.DataFrame({"time":t,"reliability":reliability,"cdf":q})
        return ReliabilityResult("Weibull reliability",table,{"shape":float(shape),"scale":float(scale),"confidence":confidence})

    # What `statistic` may name. The request arrives as JSON, so a caller can
    # only ever send a string; taking a bare Callable meant that anything the
    # application actually sent raised "TypeError: 'str' object is not
    # callable", and the only working call was the one that omitted the
    # argument entirely and got the mean.
    BOOTSTRAP_STATISTICS = {
        "mean": np.mean,
        "median": np.median,
        "std": lambda a: np.std(a, ddof=1),
        "var": lambda a: np.var(a, ddof=1),
        "min": np.min,
        "max": np.max,
    }

    @staticmethod
    def bootstrap_ci(values: Iterable[float],
                     statistic: Callable[[np.ndarray],float] | str = "mean",
                     *, confidence: float=.95, resamples: int=2000, seed:int=0) -> dict[str,float]:
        x=np.asarray(values,float).ravel(); x=x[np.isfinite(x)]
        if len(x)<2: raise ValueError("Bootstrap requires at least 2 finite observations.")
        if statistic is None:
            statistic = "mean"
        if isinstance(statistic, str):
            key = statistic.strip().lower()
            table = ReliabilityEngine.BOOTSTRAP_STATISTICS
            if key not in table:
                raise ValueError(
                    f"Unknown bootstrap statistic {statistic!r}. "
                    f"Choose one of: {', '.join(sorted(table))}."
                )
            statistic = table[key]
        elif not callable(statistic):
            raise ValueError(
                f"'statistic' must name a statistic, not {statistic!r}. "
                f"Choose one of: "
                f"{', '.join(sorted(ReliabilityEngine.BOOTSTRAP_STATISTICS))}."
            )
        rng=np.random.default_rng(seed); stats_out=np.empty(int(resamples),float)
        for i in range(int(resamples)): stats_out[i]=statistic(x[rng.integers(0,len(x),len(x))])
        a=(1-confidence)/2
        return {"estimate":float(statistic(x)),"low":float(np.quantile(stats_out,a)),"high":float(np.quantile(stats_out,1-a)),"confidence":float(confidence)}

    @staticmethod
    def monte_carlo(model: Callable[...,Any], distributions: Mapping[str,Any], *, n:int=10000, seed:int=0) -> ReliabilityResult:
        """Propagate input uncertainty through an arbitrary vectorized model.

        Distribution values may be scipy frozen distributions or callables
        accepting `(rng, n)` and returning a 1-D array.
        """
        rng=np.random.default_rng(seed); samples={}
        for name,dist in distributions.items():
            if hasattr(dist,"rvs"):
                try: vals=dist.rvs(size=int(n),random_state=rng)
                except TypeError: vals=dist.rvs(size=int(n))
            elif callable(dist): vals=dist(rng,int(n))
            else: raise TypeError(f"Unsupported distribution for {name}")
            samples[name]=np.asarray(vals,float)
        out=np.asarray(model(**samples),float).ravel(); out=out[np.isfinite(out)]
        table=pd.DataFrame({"output":out})
        details={"n":len(out),"mean":float(np.mean(out)),"std":float(np.std(out,ddof=1)),"q025":float(np.quantile(out,.025)),"q975":float(np.quantile(out,.975))}
        return ReliabilityResult("Monte Carlo uncertainty propagation",table,details)

    @staticmethod
    def failure_probability(values: Iterable[float], threshold: float, *, higher_is_failure: bool=True) -> dict[str,float]:
        x=np.asarray(values,float).ravel(); x=x[np.isfinite(x)]
        fail=x>=threshold if higher_is_failure else x<=threshold
        p=float(np.mean(fail)) if len(x) else np.nan
        se=float(np.sqrt(p*(1-p)/len(x))) if len(x) and np.isfinite(p) else np.nan
        return {"probability":p,"standard_error":se,"n":int(len(x)),"threshold":float(threshold)}
