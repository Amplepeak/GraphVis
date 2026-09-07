"""Workbook/worksheet data organisation and spreadsheet-style operations."""
from __future__ import annotations

import ast
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Mapping, Sequence

import numpy as np
import pandas as pd


@dataclass(slots=True)
class WorksheetModel:
    name: str
    frame: pd.DataFrame
    metadata: dict[str, Any] = field(default_factory=dict)
    row_metadata: pd.DataFrame | None = None
    column_metadata: dict[str, dict[str, Any]] = field(default_factory=dict)
    masks: dict[str, pd.Series] = field(default_factory=dict)
    formulas: dict[str, str] = field(default_factory=dict)

    def active_frame(self) -> pd.DataFrame:
        if not self.masks: return self.frame.copy()
        combined=np.ones(len(self.frame),dtype=bool)
        for mask in self.masks.values(): combined &= np.asarray(mask.reindex(self.frame.index,fill_value=True),bool)
        return self.frame.loc[combined].copy()


@dataclass(slots=True)
class WorkbookModel:
    name: str
    sheets: dict[str, WorksheetModel] = field(default_factory=dict)
    metadata: dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_excel(cls, path: str) -> "WorkbookModel":
        sheets=pd.read_excel(path,sheet_name=None)
        return cls(Path(path).stem,{str(name):WorksheetModel(str(name),df) for name,df in sheets.items()},{"source":str(Path(path).resolve())})

    def add_sheet(self, name: str, frame: pd.DataFrame) -> WorksheetModel:
        base=str(name or "Sheet"); candidate=base; i=2
        while candidate in self.sheets: candidate=f"{base}_{i}"; i+=1
        sheet=WorksheetModel(candidate,frame.copy()); self.sheets[candidate]=sheet; return sheet


class FormulaEngine:
    """Spreadsheet-like formula evaluator using a constrained Python AST."""
    ALLOWED_FUNCS={
        "abs":np.abs,"sqrt":np.sqrt,"log":np.log,"log10":np.log10,"exp":np.exp,
        "sin":np.sin,"cos":np.cos,"tan":np.tan,"mean":np.mean,"median":np.median,
        "minimum":np.minimum,"maximum":np.maximum,"where":np.where,"clip":np.clip,
    }

    @classmethod
    def evaluate(cls, frame: pd.DataFrame, expression: str) -> Any:
        expr=str(expression).strip(); expr=expr[1:] if expr.startswith("=") else expr
        tree=ast.parse(expr,mode="eval")
        allowed_nodes=(ast.Expression,ast.BinOp,ast.UnaryOp,ast.BoolOp,ast.Compare,ast.Call,ast.Name,ast.Load,ast.Constant,ast.Subscript,ast.List,ast.Tuple,ast.Add,ast.Sub,ast.Mult,ast.Div,ast.Pow,ast.Mod,ast.USub,ast.UAdd,ast.And,ast.Or,ast.Eq,ast.NotEq,ast.Lt,ast.LtE,ast.Gt,ast.GtE,ast.BitAnd,ast.BitOr)
        for node in ast.walk(tree):
            if not isinstance(node,allowed_nodes): raise ValueError(f"Formula operation not allowed: {type(node).__name__}")
            if isinstance(node,ast.Call) and (not isinstance(node.func,ast.Name) or node.func.id not in cls.ALLOWED_FUNCS): raise ValueError("Only approved numerical functions are permitted in formulas.")
            if isinstance(node,ast.Name) and node.id not in frame.columns and node.id not in cls.ALLOWED_FUNCS: raise KeyError(f"Unknown column/function: {node.id}")
        env={**cls.ALLOWED_FUNCS,**{str(c):frame[c] for c in frame.columns}}
        return eval(compile(tree,"<graphvis-formula>","eval"),{"__builtins__":{}},env)

    @classmethod
    def assign(cls, sheet: WorksheetModel, target: str, expression: str) -> pd.Series:
        result=cls.evaluate(sheet.frame,expression)
        if np.isscalar(result): result=np.full(len(sheet.frame),result)
        series=pd.Series(result,index=sheet.frame.index,name=target); sheet.frame[target]=series; sheet.formulas[target]=expression; return series


class DataOperations:
    @staticmethod
    def conditional_mask(sheet: WorksheetModel, name: str, expression: str) -> pd.Series:
        result=FormulaEngine.evaluate(sheet.frame,expression); mask=pd.Series(np.asarray(result,bool),index=sheet.frame.index); sheet.masks[name]=mask; return mask

    @staticmethod
    def outlier_mask(sheet: WorksheetModel, columns: Sequence[str], method: str = "iqr", threshold: float = 1.5, name: str = "outliers") -> pd.Series:
        frame=sheet.frame[list(columns)].apply(pd.to_numeric,errors="coerce"); keep=np.ones(len(frame),dtype=bool)
        if method.lower()=="zscore":
            z=(frame-frame.mean())/frame.std(ddof=0).replace(0,np.nan); keep &= (np.abs(z)<=float(threshold)).all(axis=1).fillna(False).to_numpy()
        else:
            q1=frame.quantile(.25); q3=frame.quantile(.75); iqr=q3-q1; keep &= ((frame>=q1-threshold*iqr)&(frame<=q3+threshold*iqr)).all(axis=1).fillna(False).to_numpy()
        mask=pd.Series(keep,index=sheet.frame.index); sheet.masks[name]=mask; return mask

    @staticmethod
    def pivot(sheet: WorksheetModel, index: str | Sequence[str], columns: str | Sequence[str] | None, values: str | Sequence[str], aggfunc: str = "mean") -> pd.DataFrame:
        return pd.pivot_table(sheet.active_frame(),index=index,columns=columns,values=values,aggfunc=aggfunc)

    @staticmethod
    def stack_columns(sheet: WorksheetModel, columns: Sequence[str], var_name: str = "variable", value_name: str = "value") -> pd.DataFrame:
        return sheet.active_frame().melt(value_vars=list(columns),var_name=var_name,value_name=value_name)

    @staticmethod
    def transpose(sheet: WorksheetModel) -> pd.DataFrame:
        return sheet.active_frame().T
