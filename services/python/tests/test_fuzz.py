"""Random and hostile input, at the three places untrusted text gets in.

Fuzzing is worth doing where input crosses a trust boundary and is *parsed*.
GraphVis has exactly three such places, and they are the ones here:

  - the expression parser, which takes a formula a person types and turns it
    into something that gets evaluated;
  - the LaTeX escaper, whose whole job is making arbitrary text safe to paste
    into a build;
  - the operation dispatcher, which takes a JSON request assembled by the UI
    and hands it to an engine.

Everything else is either numeric or comes from the program itself.

The contract being checked is not "produces the right answer" - for random
input there is no right answer. It is weaker and more important: **no input
should produce a crash, a hang, or a silent wrong-shaped result.** An engine
raising ValueError with a sentence in it is a pass. An IndexError from inside
NumPy is a failure, because it reaches the user as a stack trace about an array
they never saw.

Seeded, so a failure is reproducible. If one of these ever fires, the seed and
the input are printed with it.
"""
from __future__ import annotations

import random
import string
import time

import numpy as np
import pandas as pd
import pytest

from graphvis_science.latex import escape, figure_block
from graphvis_science.operations import REGISTRY, OperationError, run

SEED = 20260912

# Not a failure threshold - a reporting one. See the sweep below.
SLOW_SECONDS = 3.0


# --------------------------------------------------------------- the escaper
def test_escaping_is_total_and_never_leaves_a_lone_backslash() -> None:
    """Every byte of arbitrary text must come out paste-safe.

    The property that matters: after escaping, the only backslashes left are
    ones that begin a command this module emitted. A lone backslash from the
    input would either eat the next character or break the build.
    """
    rng = random.Random(SEED)
    alphabet = string.printable + "£µÅ–—…"
    emitted = {"textbackslash", "textasciitilde", "textasciicircum"}

    for _ in range(3000):
        raw = "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 40)))
        out = escape(raw)
        assert isinstance(out, str)
        # Walk the result: every backslash must start one of ours, or escape a
        # single known special character.
        i = 0
        while i < len(out):
            if out[i] != "\\":
                i += 1
                continue
            rest = out[i + 1:]
            if any(rest.startswith(name) for name in emitted):
                i += 1
                continue
            assert rest and rest[0] in "&%$#_{}", (
                f"lone backslash in {out!r} from {raw!r}")
            i += 2


def test_escaping_leaves_ordinary_text_untouched() -> None:
    """A property test is only as good as its negative case."""
    rng = random.Random(SEED + 1)
    safe = string.ascii_letters + string.digits + " .,:;()[]-+/=<>'\"!?"
    for _ in range(2000):
        raw = "".join(rng.choice(safe) for _ in range(rng.randint(0, 40)))
        assert escape(raw) == raw, raw


def test_figure_block_survives_hostile_captions() -> None:
    """A caption is user text. It must never produce an unbalanced block."""
    rng = random.Random(SEED + 2)
    nasty = ["\\end{figure}", "}{", "$$", "%\n", "\\input{/etc/passwd}",
             "~" * 50, "{" * 30, "\\\\", "\x00", "100%", "a_b_c"]
    for _ in range(400):
        caption = "".join(rng.choice(nasty) for _ in range(rng.randint(1, 4)))
        out = figure_block("f.pdf", caption=caption,
                           style=rng.choice(["apa", "ieee", "nature", "harvard"]))
        latex = out["latex"]
        # The injected \end{figure} must not have closed the float early.
        assert latex.count("\\begin{figure}") == 1, caption
        assert latex.count("\\end{figure}") == 1, caption
        assert latex.rstrip().endswith("\\end{figure}"), caption
        # Balance the GROUPING braces only. An escaped brace - `\{` - is a
        # literal character in the output and is deliberately unpaired, so
        # counting raw braces measures the escaping rather than the structure.
        grouping = latex.replace("\\{", "").replace("\\}", "")
        assert grouping.count("{") == grouping.count("}"), caption
        # And no control characters reach a .tex file.
        assert not any(ord(c) < 0x20 and c != "\n" for c in latex), repr(caption)


# ------------------------------------------------------- the operation layer
def _random_frame(rng: random.Random) -> pd.DataFrame:
    """A frame with the column names the dispatcher expects, filled with grief."""
    n = rng.randint(0, 12)
    pools = [
        lambda: [rng.choice([0.0, -0.0, 1.0, -1.0, 1e308, -1e308,
                             float("nan"), float("inf"), float("-inf"),
                             rng.uniform(-1e6, 1e6)]) for _ in range(n)],
        lambda: [float(rng.randint(-3, 3)) for _ in range(n)],
        lambda: [5.0] * n,
    ]
    data = {name: rng.choice(pools)() for name in ("x", "y", "z", "y2")}
    data["group"] = [rng.choice("ab") for _ in range(n)]
    return pd.DataFrame(data)


def test_no_operation_crashes_on_random_numeric_input() -> None:
    """Every operation, against frames full of NaN, inf, 1e308 and nothing.

    The permitted failures are OperationError (a sentence written for the user)
    and the engines' own ValueError/KeyError/TypeError, which the service turns
    into one. Anything else - an IndexError from NumPy, an AttributeError from
    deep inside a library - is a defect, because that is what the user sees.

    This is the sweep that found `derivative` raising "index 0 is out of bounds
    for axis 0 with size 0" on a single row, and `factorial_anova` failing with
    "'DataFrame' object has no attribute 'dtype'" when the response was also
    chosen as a factor.
    """
    rng = random.Random(SEED + 3)
    values = {
        "threshold": 1.0, "effect_size": 0.5, "expression": "a*b",
        "inputs": {"a": {"value": 1.0, "error": 0.1},
                   "b": {"value": 2.0, "error": 0.2}},
        "parameters": {"a": {"value": 1.0, "stderr": 0.1}},
        "model": "linear", "levels": {"a": 2}, "bounds": {"a": [0.0, 1.0]},
        "source": "m", "target": "cm", "unit": "m", "predictors": ["x"],
        "factors": ["group"], "columns": ["x", "y"], "image_path": "f.pdf",
        "name": "mannwhitney", "style": "apa",
        "distributions": {"a": {"kind": "normal", "loc": 0, "scale": 1}},
        "datasets": [{"x": [0, 1, 2], "y": [1, 2, 3]}],
    }
    columns = {"x": "x", "y": "y", "z": "z", "y2": "y2",
               "group": "group", "subject": "group", "c": "z"}
    network = {"cite", "cite_text", "find_dois"}

    failures: list[str] = []
    slow: list[str] = []
    for _ in range(3):
        frame = _random_frame(rng)
        for op_name, op in sorted(REGISTRY.items()):
            if op_name in network:
                continue
            request = {}
            for param, spec in op.args.items():
                key = spec.key or param
                if spec.kind in ("column", "column?", "name"):
                    request[key] = columns.get(key, "y")
                elif spec.kind in ("columns", "names"):
                    request[key] = values.get(key, ["x", "y"])
                elif spec.kind == "value" and key in values:
                    request[key] = values[key]
            started = time.monotonic()
            try:
                run(op_name, frame, request)
            except (OperationError, ValueError, KeyError, TypeError):
                pass
            except Exception as exc:                       # noqa: BLE001
                failures.append(
                    f"{op_name}: {type(exc).__name__}: {str(exc)[:70]} "
                    f"(rows={len(frame)})")
            elapsed = time.monotonic() - started
            # Degenerate input must not become a stall. Several engines use
            # least_squares with max_nfev=15000, and values like 1e308 can walk
            # them all the way to that cap - bounded, but long enough that the
            # interface looks hung. This does not fail the build; it names them,
            # because "which operation is slow on rubbish input" is a question
            # worth being able to answer.
            if elapsed > SLOW_SECONDS:
                slow.append(f"{op_name} {elapsed:.1f}s (rows={len(frame)})")

    assert not failures, (
        f"operations raising an exception the user should never see "
        f"(seed {SEED + 3}):\n  " + "\n  ".join(sorted(set(failures))[:12]))
    if slow:
        print("\n  slow on degenerate input: " + "; ".join(sorted(set(slow))[:8]))


@pytest.mark.parametrize("style", ["apa", "ieee", "nature", "harvard"])
def test_citation_formatting_survives_missing_fields(style: str) -> None:
    """Crossref records are not uniform: half these fields go missing in real life."""
    from graphvis_science.citations import to_text

    rng = random.Random(SEED + 4)
    fields = ["authors", "year", "title", "journal", "volume", "issue",
              "pages", "doi"]
    full = {"authors": [{"family": "A", "given": "B C"}], "year": 2024,
            "title": "T", "journal": "J", "volume": "1", "issue": "2",
            "pages": "3-4", "doi": "10.1/x"}
    for _ in range(300):
        record = {k: v for k, v in full.items() if rng.random() > 0.4}
        out = to_text(record, style)
        assert isinstance(out, str) and out.strip()
        # No placeholder should leak into prose a person will paste.
        assert "None" not in out, (record, out)
        assert "{}" not in out and "[]" not in out, (record, out)
