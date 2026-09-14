# =========================================================================
# electro_models.py — generic electrochemical/kinetic models and visualisation advisor.
# Pure NumPy/SciPy/pandas: safe to call from worker threads.
# =========================================================================
from __future__ import annotations

import numpy as np
import pandas as pd
from scipy.optimize import curve_fit
from scipy.stats import spearmanr


# ------------------------------------------------------------- Pareto front
def pareto_frontier(x, y, maximise_y=True, bounds=None, n_boot=200, seed=0, cancel_check=None):
    x, y = np.asarray(x, float), np.asarray(y, float)
    ok = np.isfinite(x) & np.isfinite(y)
    x, y = x[ok], y[ok]
    if x.size < 3:
        return None

    inside = np.ones(x.size, bool)
    if bounds:
        xlo, xhi, ylo, yhi = bounds
        if xlo is not None and xhi is not None:
            inside &= (x >= xlo) & (x <= xhi)
        if ylo is not None and yhi is not None:
            inside &= (y >= ylo) & (y <= yhi)

    xs, ys = x[inside], y[inside]
    if xs.size < 3:
        xs, ys, inside = x, y, np.ones(x.size, bool)

    sort_idx = np.argsort(xs, kind="stable")
    xs_o, ys_o = xs[sort_idx], ys[sort_idx]
    env = np.maximum.accumulate(ys_o) if maximise_y else np.minimum.accumulate(ys_o)
    on_front = np.isclose(ys_o, env, rtol=1e-5, atol=1e-5)

    band_lo = band_hi = grid = None
    if n_boot and xs_o.size >= 15:
        rng = np.random.default_rng(seed)
        grid = np.linspace(xs_o.min(), xs_o.max(), 80)
        curves = np.empty((n_boot, grid.size))
        for b in range(n_boot):
            if cancel_check is not None and b % 8 == 0 and cancel_check():
                raise RuntimeError("Cancelled")
            sel = rng.integers(0, xs_o.size, xs_o.size)
            bx, by = xs_o[sel], ys_o[sel]
            o = np.argsort(bx, kind="stable")
            bx, by = bx[o], by[o]
            benv = np.maximum.accumulate(by) if maximise_y else np.minimum.accumulate(by)
            curves[b] = np.interp(grid, bx, benv, left=benv[0], right=benv[-1])
        band_lo, band_hi = np.percentile(curves, [2.5, 97.5], axis=0)

    return {'x': xs_o, 'y': ys_o, 'env': env, 'on_front': on_front,
            'grid': grid, 'band_lo': band_lo, 'band_hi': band_hi, 'excluded': int((~inside).sum())}


# ------------------------------------------------------- modified Gompertz
def gompertz(t, P, Rm, lag):
    return P * np.exp(-np.exp((Rm * np.e / max(P, 1e-9)) * (lag - t) + 1.0))


def fit_gompertz(t, H):
    t, H = np.asarray(t, float), np.asarray(H, float)
    ok = np.isfinite(t) & np.isfinite(H)
    t, H = t[ok], H[ok]
    if t.size < 5 or np.ptp(t) <= 0 or np.ptp(H) <= 0:
        return None
    order = np.argsort(t, kind="stable")
    t, H = t[order], H[order]
    grad = np.gradient(H, t)
    P0 = float(np.nanmax(H))
    Rm0 = float(np.nanmax(grad)) or 1.0
    lag0 = float(t[np.argmax(grad)] - P0 / (2 * Rm0))
    try:
        popt, _ = curve_fit(gompertz, t, H, p0=[P0, Rm0, lag0],
                            bounds=([1e-9, 1e-12, t.min() - np.ptp(t)], [P0 * 10, Rm0 * 100, t.max()]),
                            maxfev=6000)
    except Exception:
        return None
    pred = gompertz(t, *popt)
    ss_res = float(np.sum((H - pred) ** 2))
    ss_tot = float(np.sum((H - H.mean()) ** 2))
    t_fine = np.linspace(t.min(), t.max(), 300)
    return {'P': float(popt[0]), 'Rm': float(popt[1]), 'lag': float(popt[2]),
            'r2': 1.0 - ss_res / max(ss_tot, 1e-12), 't': t, 'H': H, 'fit': pred,
            't_fine': t_fine, 'fit_fine': gompertz(t_fine, *popt)}


# ----------------------------------------------------- global sensitivity
def global_sensitivity(df: pd.DataFrame, params: list[str], response: str) -> pd.DataFrame | None:
    """Standardised regression coefficients (SRC) and Spearman ρ of each
    parameter on the response. Returns a DataFrame sorted by |SRC|."""
    cols = [c for c in params if c in df.columns and c != response]
    if response not in df.columns or not cols:
        return None
    sub = df[cols + [response]].replace([np.inf, -np.inf], np.nan).dropna()
    if len(sub) < max(10, len(cols) + 2):
        return None
    X = sub[cols].to_numpy(float)
    y = sub[response].to_numpy(float)
    sx = X.std(axis=0, ddof=0)
    sy = y.std(ddof=0)
    keep = sx > 0
    if not keep.any() or sy == 0:
        return None
    Xz = (X[:, keep] - X[:, keep].mean(axis=0)) / sx[keep]
    yz = (y - y.mean()) / sy
    A = np.column_stack([Xz, np.ones(len(yz))])
    coef, *_ = np.linalg.lstsq(A, yz, rcond=None)
    src = coef[:-1]
    pred = A @ coef
    r2 = 1.0 - float(np.sum((yz - pred) ** 2)) / max(float(np.sum(yz ** 2)), 1e-12)
    kept_idx = np.flatnonzero(keep)
    rho = []
    for j in kept_idx:
        try:
            rho.append(float(spearmanr(X[:, j], y).statistic))
        except Exception:
            rho.append(np.nan)
    out = pd.DataFrame({"parameter": np.array(cols)[kept_idx], "SRC": src, "spearman": rho})
    out["abs"] = out["SRC"].abs()
    out = out.sort_values("abs", ascending=True).drop(columns="abs")
    out.attrs["r2"] = r2
    return out


# -------------------------------------------------------------- EIS helpers
def fit_nyquist_semicircle(z_re, z_im):
    """Algebraic (Kåsa) circle fit through (Z', Z'') for a single RC arc.
    Returns Rs (high-frequency intercept), Rct (arc diameter) and the fitted
    arc for drawing, or None."""
    x, y = np.asarray(z_re, float), np.abs(np.asarray(z_im, float))
    ok = np.isfinite(x) & np.isfinite(y)
    x, y = x[ok], y[ok]
    if x.size < 5:
        return None
    A = np.column_stack([2 * x, 2 * y, np.ones_like(x)])
    b = x ** 2 + y ** 2
    try:
        (cx, cy, c), *_ = np.linalg.lstsq(A, b, rcond=None)
    except Exception:
        return None
    r = np.sqrt(max(c + cx ** 2 + cy ** 2, 0.0))
    if not np.isfinite(r) or r <= 0:
        return None
    rs = float(cx - np.sqrt(max(r ** 2 - cy ** 2, 0.0)))
    rct = float(2 * np.sqrt(max(r ** 2 - cy ** 2, 0.0)))
    theta = np.linspace(0, np.pi, 200)
    arc_x = cx + r * np.cos(theta)
    arc_y = cy + r * np.sin(theta)
    keep = arc_y >= 0
    return {"Rs": rs, "Rct": rct, "arc_x": arc_x[keep], "arc_y": arc_y[keep], "centre": (float(cx), float(cy)), "r": float(r)}


def polarisation_power(current, voltage):
    """Sort a polarisation sweep by current density and compute power density."""
    i, v = np.asarray(current, float), np.asarray(voltage, float)
    ok = np.isfinite(i) & np.isfinite(v)
    i, v = i[ok], v[ok]
    if i.size < 3:
        return None
    order = np.argsort(i, kind="stable")
    i, v = i[order], v[order]
    p = i * v
    k = int(np.argmax(p))
    return {"i": i, "v": v, "p": p, "i_peak": float(i[k]), "p_peak": float(p[k]), "v_peak": float(v[k])}


# ------------------------------------------------------- domain detection
def _find_name(names, keys, exclude=()):
    for n in names:
        low = str(n).lower()
        if n in exclude:
            continue
        if any(k in low for k in keys):
            return n
    return None


def detect_domain_data(dataset) -> dict:
    """Identify electrochemical columns in a dataset.

    Accepts either a GraphVis 17 Dataset (``.df``, ``.matrices``,
    ``.parameter_columns``) or a plain pandas DataFrame. v18 has no Dataset
    class - the native side hands the service an Arrow file, which loads as a
    DataFrame - so passing one used to fail with
    ``AttributeError: 'DataFrame' object has no attribute 'df'``. service.py
    already carries an _ArrowDataset shim for scan_dataset for exactly this
    reason; this accepts the DataFrame directly instead.
    """
    frame = getattr(dataset, 'df', dataset)
    df_cols = list(frame.columns)
    mat_keys = list(getattr(dataset, 'matrices', {}) or {})
    aux_keys = list(getattr(dataset, 'aux', {}) or {})
    all_names = df_cols + mat_keys + aux_keys
    # Only DECLARED parameters, and deliberately not inferred ones. `exclude`
    # drops a column from the search entirely, on the v17 reasoning that a
    # column you swept is not the signal you measured. Inferring the same set
    # from column names would be actively wrong here: the parameter token list
    # contains "voltage", "current", "frequency" and "potential", which are
    # exactly the names this function is looking FOR, so a guessed set would
    # exclude every polarisation and EIS column and detect nothing. A bare
    # DataFrame declares nothing, so nothing is excluded - which is the right
    # answer rather than a missing feature.
    params = set(getattr(dataset, 'parameter_columns', ()) or ())

    def f(keys):
        return _find_name(all_names, keys, exclude=params)

    voltage = f(['voltage', 'e_cell', 'ecell', 'v_cell'])
    current = f(['current', 'i_dens', 'j_dens', 'current_density'])
    z_re, z_im = f(['z_re', 'zreal', "z'", 'zre']), f(['z_im', 'zimag', "z''", 'zim'])
    freq = f(['freq'])
    cumulative = f(['cumulative', 'cum_', 'accumulated', 'integrated', 'total_response'])
    time = f(['time', 't_days', 't_hours'])
    return {
        'polarisation': {'current': current, 'voltage': voltage} if (voltage and current) else None,
        'eis': {'z_re': z_re, 'z_im': z_im, 'freq': freq} if (z_re and z_im) else None,
        'gompertz': {'response': cumulative, 'time': time} if cumulative else None,
    }


# ---------------------------------------------------------------- advisor
def intelligent_visualization_advisor(dataset, literature=None) -> list[tuple[str, str]]:
    """Recommend plot layouts for the active dataset and any ingested
    literature. Each entry is (chart_type, justification)."""
    recs: list[tuple[str, str]] = []
    literature = literature or []

    # Same as detect_domain_data: accept a v17 Dataset or a plain DataFrame,
    # because v18 hands the service an Arrow file that loads as the latter.
    frame = getattr(dataset, 'df', dataset)
    if dataset is None or frame is None or frame.empty:
        recs.append(("Line Chart", "No simulation dataset selected — load a .mat run first for data-driven advice."))
    else:
        import pandas as _pd
        numeric_cols = getattr(dataset, 'numeric_columns', None)
        if numeric_cols is None:
            numeric_cols = [c for c in frame.columns if _pd.api.types.is_numeric_dtype(frame[c])]
        # Roles, declared if the caller has them and inferred from column
        # names otherwise. A bare DataFrame declares neither, and the old
        # getattr defaults made `nparams` zero for every dataset v18 can
        # produce - so the two-parameter branch below, which is the response
        # surface case this advisor exists to spot, was unreachable in the
        # shipped product. The inference is the same name-token rule the
        # scanner uses; it can only add or withhold a suggestion.
        declared_params = getattr(dataset, 'parameter_columns', None)
        response_cols = getattr(dataset, 'response_columns', None)
        if declared_params is None or response_cols is None:
            from graphvis_science.analysis.intelligent_scan import column_roles
            guessed_params, guessed_responses = column_roles(frame)
            if declared_params is None:
                declared_params = guessed_params
            if response_cols is None:
                response_cols = guessed_responses or list(numeric_cols)
        ncols, nrows = len(numeric_cols), len(frame)
        nparams = len(declared_params or ())
        domain = detect_domain_data(dataset)

        if nparams >= 2 and nrows >= 10 and response_cols:
            recs.append(("2D Heatmap",
                         "A sampled parameter sweep with a response column is available. A smoothed response(X, Y) field over two independent variables is a clear way to locate operating regions; pair it with 2D Contour for iso-response lines."))
        if ncols >= 2 and nrows >= 10:
            recs.append(("Pareto Front",
                         "Multi-objective trade-off analysis is viable: plot the non-dominated frontier between two mapped responses or a response and a cost/constraint variable, with bootstrap uncertainty when appropriate."))
        if ncols >= 4 and nrows >= 10:
            recs.append(("4D / 5D Scatter",
                         "Four or more numeric series available: map a 4th variable to colour and a 5th to marker size "
                         "to expose interactions by mapping an additional response to colour and another variable to size/opacity."))
        if nparams >= 2 and nrows >= 20:
            recs.append(("Global Sensitivity",
                         "Multiple sampled parameters are present. Standardised regression or variance-based sensitivity can rank which variables dominate the selected response."))
            recs.append(("1D Marginal Responses",
                         "Marginal mean ± σ of the response against each sampled parameter separates monotone drivers "
                         "from parameters with an interior optimum."))
        if domain.get('gompertz'):
            recs.append(("Modified Gompertz Kinetics",
                         "Cumulative response time-series detected. The modified Gompertz fit estimates asymptote, maximum rate, "
                         "asymptote, maximum rate and lag phase for comparison with imported experiments or literature."))
        if domain.get('polarisation'):
            recs.append(("Polarisation & Power Curve",
                         "Current-density and cell-voltage series identified. Polarisation with the power-density twin axis "
                         "quantifies overpotentials and peak power density."))
        if domain.get('eis'):
            recs.append(("EIS: Nyquist",
                         "Real/imaginary impedance series detected. A Nyquist plot with a semicircle fit reports ohmic "
                         "resistance Rs and charge-transfer resistance Rct of the electrochemical interface."))
            if domain['eis'].get('freq'):
                recs.append(("EIS: Bode",
                             "Frequency data present: Bode |Z| and phase plots separate the time constants that overlap "
                             "in the Nyquist representation."))
        if nrows > 5000 and ncols >= 2:
            recs.append(("Hexbin Density",
                         f"{nrows:,} samples — a hexbin density map shows where the sampled operating space is concentrated "
                         "without the overplotting of a raw scatter."))

    for lit in literature:
        sc = getattr(lit, 'semantic_context', {}) or {}
        inferred = sc.get('inferred_plot_type')
        if inferred:
            axes = [x for x in (sc.get('x_variable'), sc.get('y_variable'), sc.get('z_variable')) if x]
            conds = sc.get('conditions') or {}
            detail = f"; inferred variables: {', '.join(map(str, axes))}" if axes else ""
            if conds:
                detail += f"; experimental conditions detected: {', '.join(list(conds)[:4])}"
            recs.append((str(inferred),
                         f"Project literature '{getattr(lit, 'title', 'document')}' semantically indicates this graph/relationship{detail}."))
        for d in getattr(lit, 'datasets', []):
            kind = getattr(d, 'kind_hint', 'generic')
            src = f"'{lit.title}' p.{d.source_page}" if d.source_page else f"'{lit.title}'"
            if kind == 'gompertz':
                recs.append(("Modified Gompertz Kinetics",
                             f"Literature series {src} looks like a cumulative response vs time. Overlay it on the active Gompertz "
                             f"fit to validate P, Rm and λ against experiment."))
            elif kind == 'polarisation':
                recs.append(("Polarisation & Power Curve",
                             f"Literature series {src} contains current/voltage columns — overlay it to benchmark polarization behaviour."))
            elif kind == 'nyquist':
                recs.append(("EIS: Nyquist",
                             f"Literature series {src} contains Z'/Z'' columns — overlay the experimental arc against the model."))
            elif kind == 'time_series':
                recs.append(("Line Chart",
                             f"Literature series {src} is a time course — overlay it as an experimental validation series "
                             f"(error bars are drawn automatically when a ± column was extracted)."))
            else:
                recs.append(("Line Chart",
                             f"Literature dataset {src} extracted ({len(d.df)} rows × {len(d.df.columns)} cols); overlay its first "
                             f"two numeric columns against the matching simulation curve for validation."))

    if not recs:
        recs.append(("Line Chart", "General multi-variable trend observation."))
    # de-duplicate by chart while keeping first justification
    seen, out = set(), []
    for chart, why in recs:
        key = (chart, why[:40])
        if key not in seen:
            seen.add(key)
            out.append((chart, why))
    return out
