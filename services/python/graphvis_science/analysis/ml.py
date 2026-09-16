"""Multivariate statistics and machine-learning backend for GraphVis."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Sequence

import numpy as np
import pandas as pd

from graphvis_science.runtime import get_logger

LOG = get_logger("ml")


@dataclass(slots=True)
class MLResult:
    name: str
    table: pd.DataFrame | None = None
    scores: pd.DataFrame | None = None
    loadings: pd.DataFrame | None = None
    predictions: np.ndarray | None = None
    metrics: dict[str, float] = field(default_factory=dict)
    model: Any = None
    details: dict[str, Any] = field(default_factory=dict)


def _numeric_frame(df: pd.DataFrame, columns: Sequence[str] | None = None) -> pd.DataFrame:
    cols = list(columns) if columns else list(df.select_dtypes(include=[np.number]).columns)
    out = df[cols].replace([np.inf, -np.inf], np.nan).dropna()
    if out.empty:
        raise ValueError("No complete numeric rows are available.")
    return out


class MultivariateEngine:
    @staticmethod
    def pca(df: pd.DataFrame, columns: Sequence[str] | None = None, n_components: int | None = None, scale: bool = True) -> MLResult:
        try:
            from sklearn.decomposition import PCA
            from sklearn.preprocessing import StandardScaler
        except Exception as exc:
            raise RuntimeError("PCA requires scikit-learn.") from exc
        frame = _numeric_frame(df, columns)
        X = frame.to_numpy(float)
        if scale:
            scaler = StandardScaler(); X = scaler.fit_transform(X)
        n = min(n_components or min(X.shape), X.shape[0], X.shape[1])
        model = PCA(n_components=n).fit(X)
        sc = model.transform(X)
        names = [f"PC{i+1}" for i in range(n)]
        scores = pd.DataFrame(sc, index=frame.index, columns=names)
        loadings = pd.DataFrame(model.components_.T, index=frame.columns, columns=names).reset_index().rename(columns={"index":"variable"})
        table = pd.DataFrame({"component":names,"eigenvalue":model.explained_variance_,"explained_variance_ratio":model.explained_variance_ratio_,"cumulative_variance":np.cumsum(model.explained_variance_ratio_)})
        return MLResult("PCA", table=table, scores=scores, loadings=loadings, model=model, metrics={"explained":float(np.sum(model.explained_variance_ratio_))})

    @staticmethod
    def kmeans(df: pd.DataFrame, columns: Sequence[str] | None = None, n_clusters: int = 3, scale: bool = True, random_state: int = 0) -> MLResult:
        try:
            from sklearn.cluster import KMeans
            from sklearn.preprocessing import StandardScaler
            from sklearn.metrics import silhouette_score
        except Exception as exc:
            raise RuntimeError("K-means requires scikit-learn.") from exc
        frame = _numeric_frame(df, columns); X=frame.to_numpy(float)
        if scale: X=StandardScaler().fit_transform(X)
        n_clusters=max(2,min(int(n_clusters),max(2,len(frame)-1)))
        model=KMeans(n_clusters=n_clusters,n_init="auto",random_state=random_state).fit(X)
        sil=float(silhouette_score(X,model.labels_)) if len(frame)>n_clusters else np.nan
        table=pd.DataFrame({"row_index":frame.index,"cluster":model.labels_})
        return MLResult("K-means clustering",table=table,model=model,metrics={"inertia":float(model.inertia_),"silhouette":sil})

    @staticmethod
    def hierarchical(df: pd.DataFrame, columns: Sequence[str] | None = None, method: str = "ward", metric: str = "euclidean") -> MLResult:
        from scipy.cluster.hierarchy import linkage
        frame=_numeric_frame(df,columns); X=frame.to_numpy(float)
        Z=linkage(X,method=method,metric=metric)
        table=pd.DataFrame(Z,columns=["left","right","distance","count"])
        return MLResult("Hierarchical clustering",table=table,details={"linkage":Z})

    @staticmethod
    def discriminant(df: pd.DataFrame, features: Sequence[str], target: str) -> MLResult:
        try:
            from sklearn.discriminant_analysis import LinearDiscriminantAnalysis
            from sklearn.metrics import accuracy_score, confusion_matrix
        except Exception as exc:
            raise RuntimeError("Discriminant analysis requires scikit-learn.") from exc
        work=df[[*features,target]].dropna(); X=work[list(features)].to_numpy(float); y=work[target].to_numpy()
        model=LinearDiscriminantAnalysis().fit(X,y); pred=model.predict(X)
        cm=confusion_matrix(y,pred,labels=model.classes_)
        table=pd.DataFrame(cm,index=[f"true:{v}" for v in model.classes_],columns=[f"pred:{v}" for v in model.classes_])
        return MLResult("Linear discriminant analysis",table=table,predictions=pred,model=model,metrics={"accuracy":float(accuracy_score(y,pred))})

    @staticmethod
    def pls(df: pd.DataFrame, features: Sequence[str], target: str, n_components: int = 2) -> MLResult:
        try:
            from sklearn.cross_decomposition import PLSRegression
            from sklearn.metrics import r2_score, mean_squared_error
        except Exception as exc:
            raise RuntimeError("PLS requires scikit-learn.") from exc
        work=df[[*features,target]].dropna(); X=work[list(features)].to_numpy(float); y=work[target].to_numpy(float)
        n=max(1,min(int(n_components),len(features),len(work)-1)); model=PLSRegression(n_components=n).fit(X,y); pred=model.predict(X).ravel()
        coef=np.asarray(model.coef_).reshape(-1)
        table=pd.DataFrame({"feature":list(features),"coefficient":coef[:len(features)]})
        return MLResult("Partial least squares",table=table,predictions=pred,model=model,metrics={"r2":float(r2_score(y,pred)),"rmse":float(np.sqrt(mean_squared_error(y,pred)))})

    @staticmethod
    def decision_tree(df: pd.DataFrame, features: Sequence[str], target: str, task: str = "auto", max_depth: int | None = None, random_state: int = 0) -> MLResult:
        try:
            from sklearn.tree import DecisionTreeClassifier, DecisionTreeRegressor
            from sklearn.metrics import accuracy_score, r2_score
        except Exception as exc:
            raise RuntimeError("Decision trees require scikit-learn.") from exc
        work=df[[*features,target]].dropna(); X=work[list(features)].to_numpy(float); y=work[target].to_numpy()
        if task == "auto":
            task = "classification" if (not pd.api.types.is_numeric_dtype(work[target]) or work[target].nunique() <= max(12,int(np.sqrt(len(work))))) else "regression"
        if task == "classification":
            model=DecisionTreeClassifier(max_depth=max_depth,random_state=random_state).fit(X,y); pred=model.predict(X); metric={"accuracy":float(accuracy_score(y,pred))}
        else:
            y=np.asarray(y,float); model=DecisionTreeRegressor(max_depth=max_depth,random_state=random_state).fit(X,y); pred=model.predict(X); metric={"r2":float(r2_score(y,pred))}
        table=pd.DataFrame({"feature":list(features),"importance":model.feature_importances_}).sort_values("importance",ascending=False)
        return MLResult(f"Decision tree ({task})",table=table,predictions=pred,model=model,metrics=metric)

    @staticmethod
    def predictive_advisor(df: pd.DataFrame, target: str | None = None) -> list[tuple[str,str]]:
        numeric=list(df.select_dtypes(include=[np.number]).columns); categorical=[c for c in df.columns if c not in numeric]
        recommendations: list[tuple[str,str]]=[]
        if len(numeric)>=3:
            recommendations.append(("PCA","Several numeric variables are available; PCA can reveal dominant latent directions and collinearity."))
            recommendations.append(("K-means / hierarchical clustering","Multiple numeric dimensions support unsupervised grouping."))
        if target and target in df:
            if pd.api.types.is_numeric_dtype(df[target]) and df[target].nunique()>12:
                recommendations.extend([("PLS regression","Useful when predictors are correlated."),("Decision-tree regression","Captures nonlinear interactions without specifying an equation.")])
            else:
                recommendations.extend([("Linear discriminant analysis","Interpretable classification when groups are approximately Gaussian."),("Decision-tree classification","Handles nonlinear decision boundaries.")])
        elif categorical and numeric:
            recommendations.append(("Discriminant analysis",f"Categorical field '{categorical[0]}' can serve as a group label."))
        return recommendations
