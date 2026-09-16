"""No engine is offered as known-good until something measures it.

Five engines were added to the catalogue in an afternoon. The sweep proves an
engine draws, and draws a picture no other engine draws — which a bump chart
that ranked backwards would pass comfortably, and the first draft of one did.

So the catalogue records a `verified` flag, the graph library draws unverified
entries in red and says so, and the flag is COMPUTED from the evidence rather
than typed. These check the rule and then check the real catalogue against it.
"""
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "tools" / "mark_verified_engines.py"


def _tool():
    spec = importlib.util.spec_from_file_location("mark_verified_engines", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_an_engine_with_no_check_and_no_audit_is_unverified():
    m = _tool()
    got = m.verdict_for({"Gauge", "Brand New Thing"}, measured={"Gauge"}, audited=set())
    assert got == {"Brand New Thing": False, "Gauge": True}


def test_the_audited_baseline_counts_as_verification():
    m = _tool()
    got = m.verdict_for({"Line Chart"}, measured=set(), audited={"Line Chart"})
    assert got["Line Chart"] is True


def test_being_in_the_catalogue_is_not_verification():
    # The failure this whole mechanism exists to prevent: an engine reaching
    # users because it was added, not because it was checked.
    m = _tool()
    got = m.verdict_for({"Something Added Yesterday"}, measured=set(), audited=set())
    assert got["Something Added Yesterday"] is False


def test_the_five_new_engines_are_measured_by_the_selftest():
    m = _tool()
    measured = m.measured_engines()
    for name in ("Bump Chart", "Dumbbell Plot", "Gauge",
                 "Bullet Chart", "Marimekko Chart"):
        assert name in measured, f"{name} has no measured check in PlotSelfTest.cpp"


def test_the_catalogue_flags_match_the_evidence():
    # The live guard. A hand-edited `verified` fails here.
    m = _tool()
    want = m.verdicts()
    catalogue = json.loads((ROOT / "config" / "graph_catalogue.json")
                           .read_text(encoding="utf-8"))
    wrong = []
    for category in catalogue["categories"]:
        for entry in category["entries"]:
            expected = want[entry["engine"]]
            if entry.get("verified") != expected:
                wrong.append((entry["name"], entry["engine"],
                              entry.get("verified"), expected))
    assert not wrong, (
        f"{len(wrong)} catalogue entr(ies) carry a verified flag the evidence does not "
        f"support, e.g. {wrong[:3]}. Run tools/mark_verified_engines.py."
    )


def test_every_catalogue_entry_carries_the_flag_at_all():
    catalogue = json.loads((ROOT / "config" / "graph_catalogue.json")
                           .read_text(encoding="utf-8"))
    missing = [e["name"] for c in catalogue["categories"] for e in c["entries"]
               if "verified" not in e]
    assert not missing, f"{len(missing)} entries have no verified flag, e.g. {missing[:5]}"
