"""Professional-suite dialogs for connectors, analysis, workbooks and batching."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Callable

import numpy as np
import pandas as pd
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFormLayout,
    QHBoxLayout, QLabel, QLineEdit, QListWidget, QMessageBox, QPushButton,
    QPlainTextEdit, QSpinBox, QTabWidget, QTableWidget, QTableWidgetItem,
    QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget, QDockWidget
)

from graphvis.data.connectors import ConnectorProfile
from graphvis.data.organization import DataOperations, FormulaEngine, WorksheetModel
from graphvis.analysis.fitting import CurveFittingEngine, PeakEngine
from graphvis.analysis.ml import MultivariateEngine
from graphvis.analysis.signal import CalculusEngine, SignalEngine
from graphvis.analysis.statistics import StatisticalEngine, StatisticalResult
from graphvis.automation.batch import BatchProcessor
from graphvis.core.workflow import AnalysisWorkflow, WorkflowEngine, default_workflow_registry


def _format_payload(value: Any) -> str:
    if isinstance(value, pd.DataFrame):
        return value.to_string(index=False, max_rows=80, max_cols=24)
    if hasattr(value, "summary") and callable(value.summary):
        try: return str(value.summary())
        except Exception: pass
    if hasattr(value, "__dict__"):
        return json.dumps(value.__dict__, indent=2, default=str)
    return str(value)


class DataConnectorDialog(QDialog):
    def __init__(self, controller, parent=None) -> None:
        super().__init__(parent); self.controller=controller; self.setWindowTitle("GraphVis Data Connector"); self.resize(720,420)
        root=QVBoxLayout(self); form=QFormLayout(); self.name=QLineEdit("Live Connector"); self.kind=QComboBox(); self.kind.addItems(["SQL / ODBC","Windows ADO / OLEDB","Web / Cloud"]); self.url=QLineEdit(); self.query=QPlainTextEdit("SELECT * FROM table_name"); self.query.setMaximumHeight(100); self.format=QComboBox(); self.format.addItems(["auto","csv","tsv","xlsx","json","xml","html","parquet"]); self.refresh=QSpinBox(); self.refresh.setRange(0,86400); self.refresh.setSuffix(" s"); self.refresh.setSpecialValueText("Manual"); self.refresh.setValue(0); self.persist=QCheckBox("Persist credentials in project profile (off = passwords are redacted)")
        form.addRow("Name:",self.name); form.addRow("Connector:",self.kind); form.addRow("URL / DSN / ADO string:",self.url); form.addRow("SQL query:",self.query); form.addRow("Remote format:",self.format); form.addRow("Auto-refresh:",self.refresh); form.addRow(self.persist); root.addLayout(form)
        root.addWidget(QLabel("Examples: sqlite:///C:/data/run.db · mysql+pymysql://user:pass@host/db · mssql+pyodbc://... · Provider=Microsoft.ACE.OLEDB.12.0;Data Source=C:/data.accdb · https://server/data.csv · s3://bucket/data.csv"))
        buttons=QDialogButtonBox(QDialogButtonBox.Ok|QDialogButtonBox.Cancel); buttons.button(QDialogButtonBox.Ok).setText("Connect + Import"); buttons.accepted.connect(self._connect); buttons.rejected.connect(self.reject); root.addWidget(buttons)
        self.kind.currentTextChanged.connect(lambda _: self.query.setVisible(self.kind.currentIndex() in (0,1)))
    def _connect(self) -> None:
        name=self.name.text().strip() or "Connector"; url=self.url.text().strip()
        if not url: QMessageBox.warning(self,"Connector","Enter a database or web/cloud URL."); return
        kind=("sql" if self.kind.currentIndex()==0 else "ado" if self.kind.currentIndex()==1 else "web"); options={} if self.format.currentText()=="auto" else {"format":self.format.currentText()}
        profile=ConnectorProfile(name,kind,url,self.query.toPlainText().strip() if kind in {"sql","ado"} else "",options,float(self.refresh.value()),persist_secret=self.persist.isChecked())
        ds=self.controller.import_connector(profile)
        if ds is not None: self.accept()


class SpreadsheetDialog(QDialog):
    def __init__(self, dataset, controller, parent=None) -> None:
        super().__init__(parent); self.dataset=dataset; self.controller=controller; self.setWindowTitle(f"Workbook / Data Operations — {dataset.name}"); self.resize(1120,720)
        root=QVBoxLayout(self); self.table=QTableWidget(); root.addWidget(self.table,1); self._populate()
        controls=QHBoxLayout(); self.target=QLineEdit("derived"); self.formula=QLineEdit(); self.formula.setPlaceholderText("Formula, e.g. voltage*current or log10(concentration)"); add=QPushButton("Create Formula Column"); mask=QPushButton("Apply Conditional Mask"); pivot=QPushButton("Preview Pivot"); meta=QPushButton("Edit Metadata…"); controls.addWidget(QLabel("Target:")); controls.addWidget(self.target); controls.addWidget(self.formula,1); controls.addWidget(add); controls.addWidget(mask); controls.addWidget(pivot); controls.addWidget(meta); root.addLayout(controls)
        add.clicked.connect(self._formula); mask.clicked.connect(self._mask); pivot.clicked.connect(self._pivot); meta.clicked.connect(self._metadata)
        close=QDialogButtonBox(QDialogButtonBox.Close); close.rejected.connect(self.reject); root.addWidget(close)
    def _populate(self) -> None:
        df=self.dataset.df.head(500); self.table.setRowCount(len(df)); self.table.setColumnCount(len(df.columns)); self.table.setHorizontalHeaderLabels([str(c) for c in df.columns])
        for r,(_,row) in enumerate(df.iterrows()):
            for c,val in enumerate(row): self.table.setItem(r,c,QTableWidgetItem(str(val)))
    def _push_frame_undo(self, before: pd.DataFrame, after: pd.DataFrame, label: str) -> None:
        owner=self.parent()
        history=getattr(owner,"history",None)
        if history is None: return
        def restore(frame: pd.DataFrame) -> None:
            self.dataset.df=frame.copy(deep=True)
            if hasattr(self.dataset, "touch"): self.dataset.touch()
            self._populate()
            if hasattr(owner,"populate_axes"): owner.populate_axes(self.dataset)
            if hasattr(owner,"queue_render"): owner.queue_render()
        history.push_applied(lambda: restore(after),lambda: restore(before),label)
    def _formula(self) -> None:
        target=self.target.text().strip(); expr=self.formula.text().strip()
        if not (target and expr): return
        before=self.dataset.df.copy(deep=True)
        if self.controller.assign_formula(self.dataset.name,target,expr):
            after=self.dataset.df.copy(deep=True); self._push_frame_undo(before,after,f"Formula: {target}"); self._populate()
    def _mask(self) -> None:
        expr=self.formula.text().strip()
        if not expr: return
        before=self.dataset.df.copy(deep=True); n=self.controller.apply_conditional_mask(self.dataset.name,expr)
        if n:
            after=self.dataset.df.copy(deep=True); self._push_frame_undo(before,after,"Conditional data mask"); self._populate()
    def _pivot(self) -> None:
        df=self.dataset.df; nums=list(df.select_dtypes(include=[np.number]).columns); cats=[c for c in df.columns if c not in nums]
        if not nums or not cats: QMessageBox.information(self,"Pivot","A pivot preview needs at least one categorical and one numeric column."); return
        table=pd.pivot_table(df,index=cats[0],values=nums[0],aggfunc="mean").reset_index(); QMessageBox.information(self,"Pivot preview",table.head(40).to_string(index=False))
        if QMessageBox.question(self,"Pivot dataset","Register this pivot table as a new GraphVis dataset?")==QMessageBox.Yes:
            self.controller.register_dataframe(f"{self.dataset.name} :: Pivot",table,kind="pivot",meta={"source_dataset":self.dataset.name,"index":cats[0],"value":nums[0],"aggfunc":"mean"})
    def _metadata(self) -> None:
        dlg=QDialog(self); dlg.setWindowTitle(f"Metadata — {self.dataset.name}"); dlg.resize(700,500); lay=QVBoxLayout(dlg); edit=QPlainTextEdit(json.dumps(self.dataset.meta,indent=2,ensure_ascii=False,default=str)); lay.addWidget(edit,1); buttons=QDialogButtonBox(QDialogButtonBox.Save|QDialogButtonBox.Cancel); buttons.accepted.connect(dlg.accept); buttons.rejected.connect(dlg.reject); lay.addWidget(buttons)
        if dlg.exec():
            try:
                payload=json.loads(edit.toPlainText())
                if not isinstance(payload,dict): raise ValueError("Metadata must be a JSON object.")
                self.controller.update_dataset_metadata(self.dataset.name,payload)
            except Exception as exc: QMessageBox.warning(self,"Metadata",f"Invalid metadata: {exc}")


class AnalysisHubDialog(QDialog):
    """Unified Statistics / ML / Signal / Fitting UI over typed headless engines."""
    def __init__(self, dataset, on_series: Callable[[str,np.ndarray],None] | None = None, parent=None) -> None:
        super().__init__(parent); self.ds=dataset; self.on_series=on_series; self.controller=getattr(parent,"controller",None); self.setWindowTitle(f"Analysis Hub — {dataset.name}"); self.resize(980,700)
        root=QVBoxLayout(self); top=QHBoxLayout(); self.x=QComboBox(); self.y=QComboBox(); self.group=QComboBox(); cols=[str(c) for c in dataset.df.columns]; nums=[str(c) for c in dataset.numeric_columns]; self.x.addItems(nums); self.y.addItems(nums); self.group.addItem("(none)"); self.group.addItems(cols); top.addWidget(QLabel("X:")); top.addWidget(self.x); top.addWidget(QLabel("Y:")); top.addWidget(self.y); top.addWidget(QLabel("Group/target:")); top.addWidget(self.group); root.addLayout(top)
        self.tabs=QTabWidget(); root.addWidget(self.tabs,1); self.output=QPlainTextEdit(); self.output.setReadOnly(True); self.output.setMaximumHeight(240); root.addWidget(self.output)
        self._stats_tab(); self._ml_tab(); self._signal_tab(); self._fit_tab()
        close=QDialogButtonBox(QDialogButtonBox.Close); close.rejected.connect(self.reject); root.addWidget(close)
    def _stats_call(self, method: str, *args, **kwargs):
        return self.controller.run_statistics(method,*args,**kwargs) if self.controller is not None else getattr(StatisticalEngine,method)(*args,**kwargs)
    def _ml_call(self, method: str, *args, **kwargs):
        return self.controller.run_multivariate(method,*args,**kwargs) if self.controller is not None else getattr(MultivariateEngine,method)(*args,**kwargs)
    def _sig_call(self, method: str, *args, **kwargs):
        return self.controller.run_signal(method,*args,**kwargs) if self.controller is not None else getattr(SignalEngine,method)(*args,**kwargs)
    def _calc_call(self, method: str, *args, **kwargs):
        return self.controller.run_calculus(method,*args,**kwargs) if self.controller is not None else getattr(CalculusEngine,method)(*args,**kwargs)
    def _fit_call(self, method: str, *args, **kwargs):
        return self.controller.run_curve_fit(method,*args,**kwargs) if self.controller is not None else getattr(CurveFittingEngine,method)(*args,**kwargs)
    def _peak_call(self, method: str, *args, **kwargs):
        return self.controller.run_peak(method,*args,**kwargs) if self.controller is not None else getattr(PeakEngine,method)(*args,**kwargs)
    def _stats_tab(self):
        w=QWidget(); l=QVBoxLayout(w); self.stats_op=QComboBox(); self.stats_op.addItems(["Descriptive","Normality","Unpaired t-test","Paired t-test","Welch t-test","Mann-Whitney","Wilcoxon","Kolmogorov-Smirnov","One-way ANOVA","Welch ANOVA","Two-way ANOVA","Three-way ANOVA","Kruskal-Wallis","Brown-Forsythe","Tukey HSD","Bonferroni post-hoc","FDR post-hoc","Fisher LSD","Scheffe post-hoc","Repeated-measures ANOVA","Kaplan-Meier","Log-rank","Cox proportional-hazards","Power / sample size"]); b=QPushButton("Run statistical analysis"); l.addWidget(self.stats_op); l.addWidget(b); l.addStretch(1); b.clicked.connect(self._run_stats); self.tabs.addTab(w,"Statistics")
    def _ml_tab(self):
        w=QWidget(); l=QVBoxLayout(w); self.ml_op=QComboBox(); self.ml_op.addItems(["Predictive Modeling Advisor","PCA","K-means","Hierarchical clustering","PLS","Decision tree","Discriminant analysis"]); self.k=QSpinBox(); self.k.setRange(2,20); self.k.setValue(3); b=QPushButton("Run ML / multivariate analysis"); l.addWidget(self.ml_op); l.addWidget(QLabel("Clusters/components:")); l.addWidget(self.k); l.addWidget(b); l.addStretch(1); b.clicked.connect(self._run_ml); self.tabs.addTab(w,"Multivariate / ML")
    def _signal_tab(self):
        w=QWidget(); l=QVBoxLayout(w); self.sig_op=QComboBox(); self.sig_op.addItems(["FFT","IFFT","STFT","Hilbert envelope","IIR low-pass","Savitzky-Golay","LOWESS","Baseline subtraction","Derivative","Integral"]); b=QPushButton("Run signal/math operation"); l.addWidget(self.sig_op); l.addWidget(b); l.addStretch(1); b.clicked.connect(self._run_signal); self.tabs.addTab(w,"Signal / Math")
    def _fit_tab(self):
        w=QWidget(); l=QVBoxLayout(w); self.fit_op=QComboBox(); self.fit_op.addItems(["Linear","Polynomial","Exponential","Logistic / dose-response","Peak detection","Gaussian peak deconvolution","Lorentzian peak deconvolution"]); b=QPushButton("Run fit / peak analysis"); l.addWidget(self.fit_op); l.addWidget(b); l.addStretch(1); b.clicked.connect(self._run_fit); self.tabs.addTab(w,"Fitting / Peaks")
    def _xy(self):
        x=self.ds.df[self.x.currentText()].to_numpy(float); y=self.ds.df[self.y.currentText()].to_numpy(float); return x,y
    def _show(self,obj):
        if isinstance(obj,list): text="\n\n".join(_format_payload(v) for v in obj)
        elif hasattr(obj,"table") and getattr(obj,"table") is not None: text=_format_payload(obj)+"\n\n"+_format_payload(obj.table)
        else: text=_format_payload(obj)
        self.output.setPlainText(text)
    def _groups(self):
        g=self.group.currentText(); y=self.y.currentText();
        if g=="(none)": return None,None
        frame=self.ds.df[[g,y]].dropna(); labels=list(pd.unique(frame[g])); return [frame.loc[frame[g]==v,y].to_numpy(float) for v in labels],labels
    def _run_stats(self):
        try:
            op=self.stats_op.currentText(); x,y=self._xy(); groups,labels=self._groups()
            if op=="Descriptive": r=self._stats_call("descriptive",y)
            elif op=="Normality": r=self._stats_call("normality",y)
            elif op=="Unpaired t-test": r=self._stats_call("t_test",x,y)
            elif op=="Paired t-test": r=self._stats_call("t_test",x,y,paired=True)
            elif op=="Welch t-test": r=self._stats_call("t_test",x,y,equal_var=False)
            elif op=="Mann-Whitney": r=self._stats_call("nonparametric","mann-whitney",x,y)
            elif op=="Wilcoxon": r=self._stats_call("nonparametric","wilcoxon",x,y)
            elif op=="Kolmogorov-Smirnov": r=self._stats_call("nonparametric","ks",x,y)
            elif op=="Brown-Forsythe": r=self._stats_call("brown_forsythe",groups or [x,y])
            elif op=="Kruskal-Wallis": r=self._stats_call("nonparametric","kruskal",x,groups=groups or [x,y])
            elif op=="Tukey HSD": r=self._stats_call("tukey",groups or [x,y], labels)
            elif op in {"Bonferroni post-hoc","FDR post-hoc","Fisher LSD","Scheffe post-hoc"}:
                method={"Bonferroni post-hoc":"bonferroni","FDR post-hoc":"fdr","Fisher LSD":"fisher-lsd","Scheffe post-hoc":"scheffe"}[op]; r=self._stats_call("pairwise_comparisons",groups or [x,y],labels,method)
            elif op=="Repeated-measures ANOVA":
                cats=[c for c in self.ds.df.columns if c not in self.ds.numeric_columns]
                if len(cats)<2: raise ValueError("Repeated-measures ANOVA needs a subject column and at least one categorical within-subject factor.")
                r=self._stats_call("factorial_anova",self.ds.df,self.y.currentText(),[cats[1]],repeated_subject=cats[0])
            elif op in ("Two-way ANOVA", "Three-way ANOVA"):
                cats=[c for c in self.ds.df.columns if c not in self.ds.numeric_columns]
                nfac=2 if op.startswith("Two") else 3
                if len(cats)<nfac: raise ValueError(f"{op} needs at least {nfac} categorical factor columns.")
                r=self._stats_call("factorial_anova",self.ds.df,self.y.currentText(),cats[:nfac])
            elif op=="Kaplan-Meier":
                g=None if self.group.currentText()=="(none)" else self.ds.df[self.group.currentText()]
                r=self._stats_call("kaplan_meier",x,y,g)
            elif op=="Log-rank":
                if self.group.currentText()=="(none)": raise ValueError("Select a grouping column for the log-rank test.")
                r=self._stats_call("logrank",x,y,self.ds.df[self.group.currentText()])
            elif op=="Cox proportional-hazards":
                cov=[c for c in self.ds.numeric_columns if c not in {self.x.currentText(),self.y.currentText()}]
                if not cov: raise ValueError("Cox regression needs at least one additional numeric covariate.")
                r=self._stats_call("cox",self.ds.df,self.x.currentText(),self.y.currentText(),cov[:8])
            elif op=="Power / sample size":
                from graphvis.analysis.statistics import cohen_d
                effect=cohen_d(x,y); r=self._stats_call("power_ttest",effect if np.isfinite(effect) and effect!=0 else 0.5)
            else: r=self._stats_call("one_way_anova",groups or [x,y],labels,welch=op=="Welch ANOVA")
            self._show(r)
        except Exception as exc: QMessageBox.warning(self,"Analysis failed",str(exc))
    def _run_ml(self):
        try:
            op=self.ml_op.currentText(); nums=list(self.ds.numeric_columns); target=self.group.currentText()
            if op=="Predictive Modeling Advisor": r=self._ml_call("predictive_advisor",self.ds.df,None if target=="(none)" else target)
            elif op=="PCA": r=self._ml_call("pca",self.ds.df,nums,n_components=self.k.value())
            elif op=="K-means": r=self._ml_call("kmeans",self.ds.df,nums,n_clusters=self.k.value())
            elif op=="Hierarchical clustering": r=self._ml_call("hierarchical",self.ds.df,nums)
            else:
                if target=="(none)": raise ValueError("Select a target/group column.")
                features=[c for c in nums if c!=target]
                if op=="PLS": r=self._ml_call("pls",self.ds.df,features,target,self.k.value())
                elif op=="Decision tree": r=self._ml_call("decision_tree",self.ds.df,features,target)
                else: r=self._ml_call("discriminant",self.ds.df,features,target)
            self._show(r)
        except Exception as exc: QMessageBox.warning(self,"ML analysis failed",str(exc))
    def _run_signal(self):
        try:
            op=self.sig_op.currentText(); x,y=self._xy()
            if op=="FFT": r=self._sig_call("fft",y,x)
            elif op=="IFFT":
                spectrum=np.fft.rfft(y); r=self._sig_call("ifft",spectrum,float(np.median(np.diff(x))) if len(x)>1 else 1.0)
            elif op=="STFT": r=self._sig_call("stft",y,x)
            elif op=="Hilbert envelope": r=self._sig_call("hilbert",y,x)
            elif op=="IIR low-pass":
                fs=1.0/(abs(float(np.median(np.diff(x)))) or 1.0) if len(x)>1 else 1.0; r=self._sig_call("iir_filter",y,x,cutoff=0.1*fs,kind="lowpass",fs=fs)
            elif op=="Savitzky-Golay": r=self._sig_call("savgol",y,x)
            elif op=="LOWESS": r=self._sig_call("lowess",y,x)
            elif op=="Baseline subtraction": r=self._sig_call("baseline_subtract",y,x)
            elif op=="Derivative": r=self._calc_call("derivative",y,x)
            else: r=self._calc_call("integral",y,x)
            self._show({"name":r.name,"details":r.details,"points":len(r.y)})
            if self.on_series and len(r.y)==len(self.ds.df): self.on_series(r.name.replace(" ","_"),r.y)
        except Exception as exc: QMessageBox.warning(self,"Signal analysis failed",str(exc))
    def _run_fit(self):
        try:
            op=self.fit_op.currentText(); x,y=self._xy()
            if op=="Peak detection": r=self._peak_call("detect",x,y)
            elif "deconvolution" in op.lower(): r=self._peak_call("deconvolve",x,y,profile="lorentzian" if op.startswith("Lorentzian") else "gaussian")
            else: r=self._fit_call("fit",x,y,model="logistic" if op.startswith("Logistic") else op.lower(),degree=2)
            self._show(r)
        except Exception as exc: QMessageBox.warning(self,"Fit failed",str(exc))


class BatchProcessingDialog(QDialog):
    def __init__(self,parent=None) -> None:
        super().__init__(parent); self.setWindowTitle("GraphVis Batch Processor"); self.resize(820,520); root=QVBoxLayout(self); row=QHBoxLayout(); self.folder=QLineEdit(); browse=QPushButton("Folder…"); row.addWidget(self.folder,1); row.addWidget(browse); root.addLayout(row); self.recursive=QCheckBox("Include subfolders"); root.addWidget(self.recursive); self.output=QPlainTextEdit(); self.output.setReadOnly(True); root.addWidget(self.output,1); buttons=QHBoxLayout(); run=QPushButton("Run Batch Import Audit"); htmlb=QPushButton("Save HTML Report…"); wordb=QPushButton("Save Word Report…"); pdfb=QPushButton("Save PDF Report…"); buttons.addWidget(run); buttons.addWidget(htmlb); buttons.addWidget(wordb); buttons.addWidget(pdfb); root.addLayout(buttons); self.result=None; self.processor=BatchProcessor(); browse.clicked.connect(self._browse); run.clicked.connect(self._run); htmlb.clicked.connect(lambda:self._report("html")); wordb.clicked.connect(lambda:self._report("docx")); pdfb.clicked.connect(lambda:self._report("pdf"))
    def _browse(self):
        p=QFileDialog.getExistingDirectory(self,"Batch folder");
        if p: self.folder.setText(p)
    def _run(self):
        paths=self.processor.scan(self.folder.text(),self.recursive.isChecked()); self.result=self.processor.run(paths); self.output.setPlainText("\n".join(f"{'OK' if i.ok else 'ERR'}  {i.path}  {i.summary}" for i in self.result.items))
    def _report(self,kind):
        if self.result is None: QMessageBox.information(self,"Batch report","Run a batch first."); return
        ext=kind; path,_=QFileDialog.getSaveFileName(self,"Save report",f"graphvis_batch_report.{ext}",f"*.{ext}");
        if not path: return
        if kind=="html": self.processor.write_html_report(self.result,path)
        elif kind=="docx": self.processor.write_word_report(self.result,path)
        else: self.processor.write_pdf_report(self.result,path)


class WorkflowBuilderDialog(QDialog):
    """JSON-backed builder for reusable chained analysis operations."""
    def __init__(self, dataset, store, parent=None) -> None:
        super().__init__(parent); self.dataset=dataset; self.store=store; self.setWindowTitle("GraphVis Analysis Workflow Builder"); self.resize(900,650)
        root=QVBoxLayout(self); root.addWidget(QLabel("Steps may reference earlier outputs with $name. Built-ins: select_column, descriptive, savgol, linear_fit, pca."))
        self.editor=QPlainTextEdit(); cols=list(map(str,dataset.df.columns)); x=cols[0] if cols else "x"; y=cols[1] if len(cols)>1 else x
        template={"name":"Analysis template","steps":[{"operation":"select_column","inputs":{"frame":"$frame","column":x},"output":"x"},{"operation":"select_column","inputs":{"frame":"$frame","column":y},"output":"y"},{"operation":"linear_fit","inputs":{"x":"$x","y":"$y"},"output":"fit"}]}
        self.editor.setPlainText(json.dumps(template,indent=2)); root.addWidget(self.editor,1)
        self.output=QPlainTextEdit(); self.output.setReadOnly(True); self.output.setMaximumHeight(180); root.addWidget(self.output)
        row=QHBoxLayout(); run=QPushButton("Validate + Run"); save=QPushButton("Save Analysis Template"); close=QPushButton("Close"); row.addWidget(run); row.addWidget(save); row.addStretch(1); row.addWidget(close); root.addLayout(row)
        run.clicked.connect(self._run); save.clicked.connect(self._save); close.clicked.connect(self.accept)
    def _workflow(self) -> AnalysisWorkflow:
        return AnalysisWorkflow.from_dict(json.loads(self.editor.toPlainText()))
    def _run(self) -> None:
        try:
            wf=self._workflow(); result=WorkflowEngine(default_workflow_registry()).execute(wf,{"dataset":self.dataset,"frame":self.dataset.df})
            visible={k:v for k,v in result.items() if k not in {"dataset","frame"}}; self.output.setPlainText("\n\n".join(f"{k}:\n{_format_payload(v)}" for k,v in visible.items()))
        except Exception as exc: QMessageBox.warning(self,"Workflow failed",f"{type(exc).__name__}: {exc}")
    def _save(self) -> None:
        try:
            path=self.store.save(self._workflow()); QMessageBox.information(self,"Workflow saved",str(path))
        except Exception as exc: QMessageBox.warning(self,"Save failed",str(exc))


class ObjectManagerDock(QDockWidget):
    def __init__(self,parent=None) -> None:
        super().__init__("Object Manager",parent); self.tree=QTreeWidget(); self.tree.setHeaderLabels(["Object","Type / status"]); self.setWidget(self.tree); self.setAllowedAreas(Qt.LeftDockWidgetArea|Qt.RightDockWidgetArea)
    def refresh(self, project_name: str, datasets: dict[str,Any], tabs, context_store=None) -> None:
        self.tree.clear(); root=QTreeWidgetItem([project_name,"Project"]); self.tree.addTopLevelItem(root)
        droot=QTreeWidgetItem(["Datasets","Folder"]); wroot=QTreeWidgetItem(["Workbooks","Folder"]); groot=QTreeWidgetItem(["Graphs","Folder"]); croot=QTreeWidgetItem(["Project Context","Folder"])
        root.addChild(droot); root.addChild(wroot); root.addChild(groot); root.addChild(croot)
        workbooks: dict[str,QTreeWidgetItem]={}
        for name,ds in datasets.items():
            workbook=str(getattr(ds,"meta",{}).get("workbook","") or "")
            if workbook:
                parent=workbooks.get(workbook)
                if parent is None:
                    parent=QTreeWidgetItem([workbook,"Workbook"]); wroot.addChild(parent); workbooks[workbook]=parent
                QTreeWidgetItem(parent,[str(getattr(ds,"meta",{}).get("sheet",name)),ds.describe()])
            else:
                kind=str(getattr(ds,"meta",{}).get("kind","dataset")); QTreeWidgetItem(droot,[name,kind])
        for i in range(tabs.count()): QTreeWidgetItem(groot,[tabs.tabText(i),"Graph tab"])
        if context_store is not None:
            try:
                active=context_store.active_group_name
                for group in context_store.groups():
                    status="Active group" if group.name==active else "Group"
                    gnode=QTreeWidgetItem([group.name,status]); croot.addChild(gnode)
                    for p in group.literature:
                        QTreeWidgetItem(gnode,[Path(p).name,"Literature"] )
                    for name in group.datasets:
                        QTreeWidgetItem(gnode,[name,"Dataset link"] )
                    for p in group.scripts:
                        QTreeWidgetItem(gnode,[Path(p).name,"Script context"] )
                    gnode.setExpanded(group.name==active)
            except Exception:
                pass
        for node in (root,droot,wroot,groot,croot,*workbooks.values()): node.setExpanded(True)
