"""Regression and generalized-model services used by the GraphVis Analysis Studio."""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Sequence, Any
import numpy as np
import pandas as pd

@dataclass(slots=True)
class RegressionResult:
    name: str
    coefficients: pd.DataFrame
    predictions: np.ndarray
    residuals: np.ndarray
    metrics: dict[str,float]=field(default_factory=dict)
    model: Any=None

class RegressionEngine:
    @staticmethod
    def linear(df:pd.DataFrame,response:str,predictors:Sequence[str],*,robust:bool=False,add_intercept:bool=True)->RegressionResult:
        work=df[[response,*predictors]].apply(pd.to_numeric,errors="coerce").dropna()
        if len(work)<=len(predictors): raise ValueError("Insufficient finite rows for regression.")
        y=work[response].to_numpy(float); X=work[list(predictors)].to_numpy(float)
        try:
            import statsmodels.api as sm
            Xm=sm.add_constant(X,has_constant="add") if add_intercept else X
            model=(sm.RLM(y,Xm,M=sm.robust.norms.HuberT()).fit() if robust else sm.OLS(y,Xm).fit())
            names=(["Intercept"] if add_intercept else [])+list(predictors); pred=np.asarray(model.predict(Xm),float)
            table=pd.DataFrame({"term":names,"coefficient":model.params,"std_error":getattr(model,"bse",np.full(len(names),np.nan)),"p_value":getattr(model,"pvalues",np.full(len(names),np.nan))})
            metrics={"r2":float(getattr(model,"rsquared",np.nan)),"adj_r2":float(getattr(model,"rsquared_adj",np.nan)),"aic":float(getattr(model,"aic",np.nan)),"bic":float(getattr(model,"bic",np.nan))}
        except Exception:
            Xm=np.column_stack([np.ones(len(X)),X]) if add_intercept else X; beta,*_=np.linalg.lstsq(Xm,y,rcond=None); pred=Xm@beta; names=(["Intercept"] if add_intercept else [])+list(predictors); table=pd.DataFrame({"term":names,"coefficient":beta}); ss=np.sum((y-pred)**2); st=np.sum((y-y.mean())**2); metrics={"r2":float(1-ss/max(st,1e-15))}; model=None
        return RegressionResult("Robust linear regression" if robust else "Linear regression",table,pred,y-pred,metrics,model)

    @staticmethod
    def logistic(df:pd.DataFrame,response:str,predictors:Sequence[str])->RegressionResult:
        work=df[[response,*predictors]].apply(pd.to_numeric,errors="coerce").dropna(); y=work[response].to_numpy(float); X=work[list(predictors)].to_numpy(float)
        import statsmodels.api as sm
        Xm=sm.add_constant(X,has_constant="add"); model=sm.Logit(y,Xm).fit(disp=False); pred=np.asarray(model.predict(Xm),float)
        names=["Intercept",*predictors]; table=pd.DataFrame({"term":names,"coefficient":model.params,"std_error":model.bse,"p_value":model.pvalues,"odds_ratio":np.exp(model.params)})
        return RegressionResult("Logistic regression",table,pred,y-pred,{"aic":float(model.aic),"bic":float(model.bic),"pseudo_r2":float(model.prsquared)},model)
