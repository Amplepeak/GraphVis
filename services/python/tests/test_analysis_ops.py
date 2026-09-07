"""The analysis service operations.

These modules were written and tested and had no route from the application:
the service exposed no operation for any of them, so ~1,800 lines of working
code sat behind three disabled buttons. This file covers the contract that
connects them.

It checks the SHAPE of every reply, not just that one came back. The first
wiring of these operations returned {"ok": true} with empty results for weibull,
pca and doe, because it read fields the result dataclasses do not have and
getattr defaults hid the mismatch. An "ok" with nothing in it is the failure
this file exists to catch.

Run with:  python tests/test_analysis_ops.py
"""
import os, sys, tempfile, warnings
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
warnings.filterwarnings('ignore')

import numpy as np
import pyarrow as pa
import pyarrow.ipc as ipc
from graphvis_science.service import dispatch

d = tempfile.mkdtemp()
x = np.linspace(0.1, 40, 300)
rng = np.random.default_rng(2)
y = 3.2 * (1 - np.exp(-x / 6)) + rng.normal(0, 0.03, 300)
table = pa.table({
    "time_h": pa.array(x),
    "H2_mL": pa.array(y),
    "voltage": pa.array(0.8 - 0.004 * x + rng.normal(0, 0.002, 300)),
    "current_density": pa.array(2.5 + 0.02 * x + rng.normal(0, 0.01, 300)),
})
SRC = os.path.join(d, "run.arrow")
with pa.OSFile(SRC, "wb") as fh:
    with ipc.new_file(fh, table.schema) as w:
        w.write_table(table)

ok = fail = 0


def check(name, condition, detail=""):
    global ok, fail
    if condition:
        print(f"  ok   {name:40s} {detail}"); ok += 1
    else:
        print(f"  FAIL {name:40s} {detail}"); fail += 1


def call(op, **req):
    return dispatch({"op": op, "arrow_path": SRC, **req})


def nonempty_table(t):
    return isinstance(t, dict) and bool(t.get("columns")) and bool(t.get("rows"))


print("=== every operation answers, and the answer has content in it ===")

r = call("analysis.limits", x="time_h", y="H2_mL")
check("limits", r.get("ok") and isinstance(r.get("features"), list) and "text" in r,
      f"{len(r.get('features', []))} features")

r = call("analysis.forecast", x="time_h", y="H2_mL", extension=0.25)
check("forecast returns a band", r.get("ok") and len(r.get("x", [])) > 0
      and len(r["lower"]) == len(r["x"]) == len(r["upper"]),
      f"model={r.get('model','')[:20]} r2={r.get('r2', 0):.3f}")

r = call("analysis.fft", x="time_h", y="H2_mL")
check("fft returns a spectrum", r.get("ok") and len(r.get("x", [])) > 10
      and len(r["y"]) == len(r["x"]), f"{len(r.get('x', []))} bins")

r = call("analysis.weibull", y="H2_mL")
check("weibull returns its table", r.get("ok") and nonempty_table(r.get("table")),
      f"columns={r.get('table', {}).get('columns')}")

r = call("analysis.regression", y="H2_mL", predictors=["time_h", "current_density"])
check("regression returns coefficients", r.get("ok") and nonempty_table(r.get("coefficients"))
      and "r2" in (r.get("metrics") or {}),
      f"r2={(r.get('metrics') or {}).get('r2', 0):.3f}")

r = call("analysis.pca", columns=["time_h", "H2_mL", "voltage", "current_density"])
check("pca returns scores and loadings", r.get("ok") and nonempty_table(r.get("scores")),
      f"scores={r.get('scores', {}).get('columns')}")

r = call("analysis.doe", bounds={"T": [30, 50], "pH": [6, 8]}, runs=12)
check("doe returns a design matrix", r.get("ok") and nonempty_table(r.get("design"))
      and len(r["design"]["rows"].get("T", [])) == 12,
      f"kind={r.get('kind','')[:24]}")

r = call("analysis.advisor")
check("advisor recommends engines", r.get("ok") and len(r.get("recommendations", [])) > 0
      and all("engine" in q and "why" in q for q in r["recommendations"]),
      f"{len(r.get('recommendations', []))} recommendations")

r = call("analysis.domain")
check("domain detection", r.get("ok") and isinstance(r.get("domains"), dict),
      f"{list((r.get('domains') or {}).keys())}")

print("\n=== bad input is refused with a reason, not a traceback ===")
r = call("analysis.nonsense")
check("unknown operation", r.get("ok") is False and "operations" in r, r.get("error", "")[:44])
r = call("analysis.limits", x="nope", y="H2_mL")
check("unknown column", r.get("ok") is False and "nope" in r.get("error", ""), r.get("error", "")[:44])
r = call("analysis.regression", y="H2_mL", predictors=[])
check("regression with no predictors", r.get("ok") is False, r.get("error", "")[:44])
r = call("analysis.doe", bounds={})
check("doe with no bounds", r.get("ok") is False, r.get("error", "")[:44])

print("\n=== JSON-safety: no NaN or numpy types can reach the wire ===")
import json
r = call("analysis.forecast", x="time_h", y="H2_mL")
try:
    json.dumps(r, allow_nan=False)
    check("forecast reply is strict JSON", True, "serialises with allow_nan=False")
except (ValueError, TypeError) as exc:
    check("forecast reply is strict JSON", False, str(exc)[:50])

print(f"\n{ok} passed, {fail} failed")
sys.exit(1 if fail else 0)
