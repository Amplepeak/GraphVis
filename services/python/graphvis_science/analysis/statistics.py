"""Typed statistical analysis backend for GraphVis.

The module is intentionally Qt-free.  Views invoke it through the controller so
statistical work can be run in worker threads and recorded in project history.
SciPy provides the always-available core; statsmodels/lifelines are used when
installed for richer ANOVA, post-hoc, power and survival analyses.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Iterable, Sequence

import numpy as np
import pandas as pd
from scipy import stats

from graphvis_science.runtime import get_logger

LOG = get_logger("statistics")


@dataclass(slots=True)
class StatisticalResult:
    name: str
    statistic: float | None = None
    p_value: float | None = None
    effect_size: float | None = None
    table: pd.DataFrame | None = None
    details: dict[str, Any] = field(default_factory=dict)
    warnings: list[str] = field(default_factory=list)

    def summary(self) -> str:
        parts = [self.name]
        if self.statistic is not None and np.isfinite(self.statistic):
            parts.append(f"statistic={self.statistic:.6g}")
        if self.p_value is not None and np.isfinite(self.p_value):
            parts.append(f"p={self.p_value:.6g}")
        if self.effect_size is not None and np.isfinite(self.effect_size):
            parts.append(f"effect={self.effect_size:.6g}")
        return " | ".join(parts)


def _clean(values: Iterable[float]) -> np.ndarray:
    arr = np.asarray(list(values) if not isinstance(values, np.ndarray) else values, dtype=float).ravel()
    return arr[np.isfinite(arr)]


def _paired_clean(a: Iterable[float], b: Iterable[float]) -> tuple[np.ndarray, np.ndarray]:
    x, y = np.asarray(a, float).ravel(), np.asarray(b, float).ravel()
    n = min(x.size, y.size)
    x, y = x[:n], y[:n]
    ok = np.isfinite(x) & np.isfinite(y)
    return x[ok], y[ok]


def cohen_d(a: Iterable[float], b: Iterable[float], paired: bool = False) -> float:
    x, y = _paired_clean(a, b) if paired else (_clean(a), _clean(b))
    if paired:
        d = x - y
        return float(np.mean(d) / np.std(d, ddof=1)) if d.size > 1 and np.std(d, ddof=1) > 0 else np.nan
    if x.size < 2 or y.size < 2:
        return np.nan
    pooled = np.sqrt(((x.size - 1) * np.var(x, ddof=1) + (y.size - 1) * np.var(y, ddof=1)) / max(x.size + y.size - 2, 1))
    return float((np.mean(x) - np.mean(y)) / pooled) if pooled > 0 else np.nan


class StatisticalEngine:
    """Collection of scientific statistics used by the Analysis Hub."""

    @staticmethod
    def descriptive(values: Iterable[float], confidence: float = 0.95) -> StatisticalResult:
        x = _clean(values)
        if x.size == 0:
            raise ValueError("No finite observations.")
        alpha = 1.0 - float(confidence)
        mean = float(np.mean(x)); sd = float(np.std(x, ddof=1)) if x.size > 1 else 0.0
        se = sd / np.sqrt(x.size) if x.size else np.nan
        tcrit = float(stats.t.ppf(1 - alpha / 2, x.size - 1)) if x.size > 1 else np.nan
        ci = (mean - tcrit * se, mean + tcrit * se) if x.size > 1 else (mean, mean)
        table = pd.DataFrame({
            "metric": ["n", "mean", "median", "variance", "std", "std_error", "minimum", "maximum", "q25", "q75", f"CI{confidence:.0%}_low", f"CI{confidence:.0%}_high"],
            "value": [x.size, mean, np.median(x), np.var(x, ddof=1) if x.size > 1 else 0.0, sd, se, np.min(x), np.max(x), np.percentile(x,25), np.percentile(x,75), ci[0], ci[1]],
        })
        return StatisticalResult("Descriptive statistics", table=table, details={"confidence": confidence})

    @staticmethod
    def normality(values: Iterable[float]) -> list[StatisticalResult]:
        x = _clean(values)
        if x.size < 3:
            raise ValueError("Normality testing needs at least three observations.")
        out: list[StatisticalResult] = []
        if x.size <= 5000:
            s, p = stats.shapiro(x)
            out.append(StatisticalResult("Shapiro-Wilk", float(s), float(p)))
        if x.size >= 8:
            s, p = stats.normaltest(x)
            out.append(StatisticalResult("D'Agostino K²", float(s), float(p)))
        s, p = stats.kstest((x - np.mean(x)) / (np.std(x, ddof=1) or 1.0), "norm")
        out.append(StatisticalResult("Kolmogorov-Smirnov vs normal", float(s), float(p)))
        return out

    @staticmethod
    def t_test(a: Iterable[float], b: Iterable[float] | None = None, *, paired: bool = False, equal_var: bool = True, mu: float = 0.0) -> StatisticalResult:
        if b is None:
            x = _clean(a); s, p = stats.ttest_1samp(x, popmean=mu, nan_policy="omit")
            d = float((np.mean(x)-mu)/(np.std(x,ddof=1) or np.nan))
            return StatisticalResult("One-sample t-test", float(s), float(p), d, details={"mu": mu, "n": len(x)})
        if paired:
            x, y = _paired_clean(a, b); s, p = stats.ttest_rel(x, y, nan_policy="omit")
            return StatisticalResult("Paired t-test", float(s), float(p), cohen_d(x,y,True), details={"n": len(x)})
        x, y = _clean(a), _clean(b); s, p = stats.ttest_ind(x, y, equal_var=equal_var, nan_policy="omit")
        return StatisticalResult("Unpaired t-test" if equal_var else "Welch t-test", float(s), float(p), cohen_d(x,y), details={"n1":len(x),"n2":len(y)})

    @staticmethod
    def one_way_anova(groups: Sequence[Iterable[float]], labels: Sequence[str] | None = None, *, welch: bool = False) -> StatisticalResult:
        clean = [_clean(g) for g in groups]
        clean = [g for g in clean if g.size]
        if len(clean) < 2:
            raise ValueError("ANOVA requires at least two non-empty groups.")
        if welch:
            try:
                from statsmodels.stats.oneway import anova_oneway
                res = anova_oneway(clean, use_var="unequal", welch_correction=True)
                return StatisticalResult("Welch ANOVA", float(res.statistic), float(res.pvalue), details={"df": tuple(map(float,res.df))})
            except Exception as exc:
                LOG.exception("Welch ANOVA failed")
                raise RuntimeError("Welch ANOVA requires statsmodels.") from exc
        s, p = stats.f_oneway(*clean)
        all_vals = np.concatenate(clean); grand = float(np.mean(all_vals))
        ss_between = sum(len(g)*(float(np.mean(g))-grand)**2 for g in clean)
        ss_total = float(np.sum((all_vals-grand)**2))
        eta2 = ss_between/ss_total if ss_total > 0 else np.nan
        return StatisticalResult("One-way ANOVA", float(s), float(p), float(eta2), details={"groups": list(labels or [f"G{i+1}" for i in range(len(clean))])})

    @staticmethod
    def factorial_anova(df: pd.DataFrame, response: str, factors: Sequence[str], repeated_subject: str | None = None) -> StatisticalResult:
        if response not in df or any(f not in df for f in factors):
            raise KeyError("Response/factor columns not found.")
        work = df[[response, *factors] + ([repeated_subject] if repeated_subject else [])].dropna().copy()
        try:
            import statsmodels.api as sm
            from statsmodels.formula.api import ols
            from statsmodels.stats.anova import AnovaRM
        except Exception as exc:
            raise RuntimeError("Factorial/repeated-measures ANOVA requires statsmodels.") from exc
        if repeated_subject:
            if len(factors) > 2:
                raise ValueError("Repeated-measures helper supports one or two within-subject factors.")
            fit = AnovaRM(work, depvar=response, subject=repeated_subject, within=list(factors)).fit()
            table = fit.anova_table.reset_index().rename(columns={"index":"term"})
            return StatisticalResult("Repeated-measures ANOVA", table=table)
        formula = f"Q('{response}') ~ " + " * ".join(f"C(Q('{f}'))" for f in factors)
        model = ols(formula, data=work).fit()
        table = sm.stats.anova_lm(model, typ=2).reset_index().rename(columns={"index":"term"})
        return StatisticalResult(f"{len(factors)}-way ANOVA", table=table, details={"formula": formula})

    @staticmethod
    def brown_forsythe(groups: Sequence[Iterable[float]]) -> StatisticalResult:
        clean = [_clean(g) for g in groups if _clean(g).size]
        s, p = stats.levene(*clean, center="median")
        return StatisticalResult("Brown-Forsythe variance test", float(s), float(p))

    @staticmethod
    def nonparametric(name: str, a: Iterable[float], b: Iterable[float] | None = None, groups: Sequence[Iterable[float]] | None = None) -> StatisticalResult:
        key = name.lower().replace("_","-")
        if key in {"mann-whitney","mannwhitney","mann-whitney u"}:
            s,p=stats.mannwhitneyu(_clean(a),_clean([] if b is None else b),alternative="two-sided"); return StatisticalResult("Mann-Whitney U",float(s),float(p))
        if key in {"wilcoxon","wilcoxon signed-rank"}:
            x,y=_paired_clean(a, [] if b is None else b); s,p=stats.wilcoxon(x,y); return StatisticalResult("Wilcoxon signed-rank",float(s),float(p))
        if key in {"ks","kolmogorov-smirnov"}:
            s,p=stats.ks_2samp(_clean(a),_clean([] if b is None else b)); return StatisticalResult("Two-sample Kolmogorov-Smirnov",float(s),float(p))
        if key in {"kruskal","kruskal-wallis"}:
            gs=[_clean(g) for g in (groups or []) if _clean(g).size]; s,p=stats.kruskal(*gs); return StatisticalResult("Kruskal-Wallis",float(s),float(p))
        if key in {"friedman","friedman-anova"}:
            gs=[_clean(g) for g in (groups or []) if _clean(g).size]; n=min(map(len,gs)); s,p=stats.friedmanchisquare(*[g[:n] for g in gs]); return StatisticalResult("Friedman ANOVA",float(s),float(p))
        raise ValueError(f"Unsupported non-parametric test: {name}")

    @staticmethod
    def adjust_pvalues(pvalues: Sequence[float], method: str = "fdr_bh") -> np.ndarray:
        p = np.asarray(pvalues, float)
        try:
            from statsmodels.stats.multitest import multipletests
            aliases = {"fdr":"fdr_bh","bonferroni":"bonferroni","holm":"holm","holm-sidak":"holm-sidak","sidak":"sidak"}
            return np.asarray(multipletests(p, method=aliases.get(method.lower(), method))[1], float)
        except Exception:
            if method.lower() == "bonferroni":
                return np.minimum(p * len(p), 1.0)
            # Benjamini-Hochberg fallback.
            order=np.argsort(p); ranked=p[order]*len(p)/np.arange(1,len(p)+1); ranked=np.minimum.accumulate(ranked[::-1])[::-1]
            out=np.empty_like(ranked); out[order]=np.minimum(ranked,1.0); return out

    @staticmethod
    def tukey(groups: Sequence[Iterable[float]], labels: Sequence[str] | None = None, alpha: float = 0.05) -> StatisticalResult:
        clean=[_clean(g) for g in groups]; labs=list(labels or [f"G{i+1}" for i in range(len(clean))])
        vals=np.concatenate(clean); group_names=np.concatenate([[lab]*len(g) for lab,g in zip(labs,clean)])
        try:
            from statsmodels.stats.multicomp import pairwise_tukeyhsd
            res=pairwise_tukeyhsd(vals,group_names,alpha=alpha)
            table=pd.DataFrame(res._results_table.data[1:],columns=res._results_table.data[0])
            return StatisticalResult("Tukey HSD", table=table)
        except Exception as exc:
            raise RuntimeError("Tukey HSD requires statsmodels.") from exc

    @staticmethod
    def pairwise_comparisons(groups: Sequence[Iterable[float]], labels: Sequence[str] | None = None, method: str = "bonferroni", alpha: float = 0.05) -> StatisticalResult:
        """Pairwise post-hoc comparisons with common scientific corrections.

        Supported methods: Bonferroni, FDR (Benjamini-Hochberg), Holm, Fisher
        LSD and Scheffe. Fisher LSD uses the pooled one-way ANOVA error term;
        Scheffe evaluates each pair as a contrast against the omnibus MSE.
        """
        clean=[_clean(g) for g in groups]
        labs=list(labels or [f"G{i+1}" for i in range(len(clean))])
        keep=[i for i,g in enumerate(clean) if len(g)]
        clean=[clean[i] for i in keep]; labs=[labs[i] for i in keep]
        if len(clean)<2: raise ValueError("Post-hoc comparisons require at least two non-empty groups.")
        key=method.lower().replace("_","-")
        all_vals=np.concatenate(clean); n_total=len(all_vals); k=len(clean)
        grand=float(np.mean(all_vals)); sse=sum(float(np.sum((g-np.mean(g))**2)) for g in clean); df_error=max(n_total-k,1); mse=sse/df_error
        rows=[]; raw=[]
        for i in range(k):
            for j in range(i+1,k):
                gi,gj=clean[i],clean[j]; diff=float(np.mean(gi)-np.mean(gj))
                if key in {"scheffe","scheffé"}:
                    denom=mse*(1/len(gi)+1/len(gj)); f=(diff*diff/max(denom,1e-300))/max(k-1,1); p=float(stats.f.sf(f,max(k-1,1),df_error)); stat=float(f)
                else:
                    t,p=stats.ttest_ind(gi,gj,equal_var=True); stat=float(t); p=float(p)
                raw.append(p); rows.append({"group1":labs[i],"group2":labs[j],"mean_difference":diff,"statistic":stat,"p_raw":p})
        p_raw=np.asarray(raw,float)
        if key in {"fisher-lsd","fisher lsd","lsd","scheffe","scheffé"}: adjusted=p_raw
        elif key in {"fdr","fdr-bh","benjamini-hochberg"}: adjusted=StatisticalEngine.adjust_pvalues(p_raw,"fdr_bh")
        elif key in {"holm","holm-bonferroni"}: adjusted=StatisticalEngine.adjust_pvalues(p_raw,"holm")
        else: adjusted=StatisticalEngine.adjust_pvalues(p_raw,"bonferroni")
        for row,padj in zip(rows,adjusted): row["p_adjusted"]=float(padj); row["significant"]=bool(padj<alpha)
        return StatisticalResult(f"Pairwise comparisons ({method})", table=pd.DataFrame(rows), details={"alpha":alpha,"method":method,"mse":mse,"df_error":df_error,"grand_mean":grand})

    @staticmethod
    def power_ttest(effect_size: float, alpha: float = 0.05, power: float = 0.8, ratio: float = 1.0, alternative: str = "two-sided") -> StatisticalResult:
        try:
            from statsmodels.stats.power import TTestIndPower
            n = TTestIndPower().solve_power(effect_size=abs(effect_size), alpha=alpha, power=power, ratio=ratio, alternative=alternative)
        except Exception as exc:
            raise RuntimeError("Power analysis requires statsmodels.") from exc
        return StatisticalResult("Two-sample t-test power", details={"n_group1":float(n),"n_group2":float(n*ratio),"effect_size":effect_size,"alpha":alpha,"power":power})

    @staticmethod
    def kaplan_meier(time: Iterable[float], event: Iterable[int], group: Iterable[Any] | None = None) -> StatisticalResult:
        try:
            from lifelines import KaplanMeierFitter
        except Exception as exc:
            raise RuntimeError("Kaplan-Meier analysis requires lifelines.") from exc
        t=np.asarray(time,float); e=np.asarray(event,int); ok=np.isfinite(t)&np.isfinite(e); t,e=t[ok],e[ok]
        rows=[]
        if group is None:
            km=KaplanMeierFitter().fit(t,event_observed=e,label="All")
            sf=km.survival_function_.reset_index(); sf.columns=["time","survival"]; rows.append(sf.assign(group="All"))
        else:
            g=np.asarray(group,object)[ok]
            for label in pd.unique(g):
                m=g==label; km=KaplanMeierFitter().fit(t[m],event_observed=e[m],label=str(label)); sf=km.survival_function_.reset_index(); sf.columns=["time","survival"]; rows.append(sf.assign(group=str(label)))
        return StatisticalResult("Kaplan-Meier", table=pd.concat(rows,ignore_index=True) if rows else pd.DataFrame())

    @staticmethod
    def logrank(time: Iterable[float], event: Iterable[int], group: Iterable[Any]) -> StatisticalResult:
        try:
            from lifelines.statistics import multivariate_logrank_test
        except Exception as exc:
            raise RuntimeError("Log-rank testing requires lifelines.") from exc
        res=multivariate_logrank_test(np.asarray(time,float),np.asarray(group),np.asarray(event,int))
        return StatisticalResult("Log-rank test",float(res.test_statistic),float(res.p_value))

    @staticmethod
    def cox(df: pd.DataFrame, duration_col: str, event_col: str, covariates: Sequence[str]) -> StatisticalResult:
        try:
            from lifelines import CoxPHFitter
        except Exception as exc:
            raise RuntimeError("Cox regression requires lifelines.") from exc
        cols=[duration_col,event_col,*covariates]; work=df[cols].dropna(); model=CoxPHFitter().fit(work,duration_col=duration_col,event_col=event_col)
        return StatisticalResult("Cox proportional-hazards", table=model.summary.reset_index(), details={"concordance":float(model.concordance_index_)})
