"""Citation styles and the LaTeX figure block.

Written against a bug this suite found: `citations.to_text(record, style)` took
a style and ignored it. Every value except "bibtex" produced the same APA-ish
string, so a user who selected IEEE got APA with nothing on screen saying their
choice had done nothing. A parameter that is accepted and silently discarded is
worse than one that does not exist, because the output looks like an answer to
the question that was asked.

The expected strings below were written from the style guides, then compared
against the code - not pasted out of its output. That order matters: a test
built from today's output cannot tell you the output is wrong.
"""
from __future__ import annotations

import pytest

from graphvis_science.citations import STYLES, CitationError, to_text
from graphvis_science.latex import LatexError, escape, figure_block

RECORD = {
    "authors": [{"family": "Bach", "given": "Johann Sebastian"},
                {"family": "Smith", "given": "Jane"}],
    "year": 2024,
    "title": "Coupling dark fermentation and microbial electrolysis",
    "journal": "Int. J. Hydrogen Energy",
    "volume": "49",
    "issue": "3",
    "pages": "101-115",
    "doi": "10.1000/xyz",
}


# ------------------------------------------------------------- citation styles
# Styles that produce the SAME reference-list entry, by design rather than by
# accident. Each pair is a documented relationship, not a bug:
#
#   ama / jama                     JAMA follows the AMA Manual of Style.
#   nlm / vancouver                Vancouver IS the ICMJE/NLM reference format.
#   cse-citation-name / -sequence  The two CSE numeric systems differ in how the
#                                  reference LIST is ordered, not in how one
#                                  entry is written.
#   alwd / bluebook                ALWD deliberately matches Bluebook citation
#                                  form for a journal article.
#
# Named here so the "every style is distinct" check can still catch two rows
# that collide because one was copied and not finished - which is the failure
# the original four-style version of this test was written for.
SAME_BY_DESIGN = (
    frozenset({"ama", "jama"}),
    frozenset({"nlm", "vancouver"}),
    frozenset({"cse-citation-name", "cse-citation-sequence"}),
    frozenset({"alwd", "bluebook"}),
)


def test_every_style_produces_a_different_string() -> None:
    """The regression test. One output for four styles was the bug.

    It is now fifty-one styles from one table, so the risk has changed shape:
    not a style argument that is ignored, but a row copied from its neighbour
    and not finished. Either way two styles come out identical, and either way
    this fails - except for the pairs above, which are identical on purpose.
    """
    rendered = {s: to_text(RECORD, s) for s in STYLES if s != "bibtex"}
    groups: dict[str, set] = {}
    for key, text in rendered.items():
        groups.setdefault(text, set()).add(key)
    unexpected = [sorted(keys) for keys in groups.values()
                  if len(keys) > 1 and frozenset(keys) not in SAME_BY_DESIGN]
    assert not unexpected, (
        f"these styles render identically and are not a documented pair: "
        f"{unexpected}")


def test_apa_puts_the_year_in_brackets_after_the_authors() -> None:
    out = to_text(RECORD, "apa")
    assert out.startswith("Bach, J. S., & Smith, J. (2024).")
    # An EN DASH in the page range. APA sets one, and a hyphen there is the
    # commonest thing a copy editor changes back.
    assert "49(3), 101\u2013115" in out
    assert out.endswith("https://doi.org/10.1000/xyz")


def test_ieee_puts_initials_first_and_the_title_in_quotes() -> None:
    out = to_text(RECORD, "ieee")
    # No comma before "and" with exactly two authors: a serial comma needs a
    # series. IEEE was rendering "J. S. Bach, and J. Smith" when the flag that
    # asks for the comma in a list of three was applied to a list of two.
    assert out.startswith("J. S. Bach and J. Smith, \u201c")
    assert "vol. 49" in out and "no. 3" in out and "pp. 101\u2013115" in out
    assert out.rstrip().endswith("doi: 10.1000/xyz.")


def test_nature_puts_the_year_last_in_brackets() -> None:
    out = to_text(RECORD, "nature")
    assert "Bach, J. S. & Smith, J." in out
    assert out.rstrip().endswith("(2024).")


def test_harvard_quotes_the_title_and_uses_pp() -> None:
    out = to_text(RECORD, "harvard")
    assert "(2024)" in out
    # Typographic single quotes, which is what British Harvard sets.
    assert "\u2018Coupling dark fermentation and microbial electrolysis\u2019" in out
    assert "pp. 101\u2013115" in out


def test_initials_cover_every_forename() -> None:
    """'Johann Sebastian' is two forenames, so APA wants 'J. S.', not 'J.'."""
    assert "Bach, J. S." in to_text(RECORD, "apa")


def test_an_unknown_style_is_refused() -> None:
    """A name that is not in the table is refused, not quietly formatted.

    This used to pass "vancouver", which was a good choice when four styles
    were understood and is a bad one now that Vancouver is among them - a test
    for "unknown" has to name something that will stay unknown.
    """
    with pytest.raises(CitationError, match="(?i)unknown citation style"):
        to_text(RECORD, "not-a-real-style")


# ------------------------------------------------------------------- escaping
@pytest.mark.parametrize("raw,expected", [
    ("H2_yield", r"H2\_yield"),
    ("50%", r"50\%"),
    ("cost $/kg", r"cost \$/kg"),
    ("a & b", r"a \& b"),
    ("#1", r"\#1"),
    ("{x}", r"\{x\}"),
])
def test_tex_special_characters_are_escaped(raw: str, expected: str) -> None:
    """Scientific column names break TeX builds; `H2_yield (%)` carries two.

    An unescaped underscore is a build error pointing at the caption rather
    than at the name inside it, and an unescaped per-cent silently comments out
    the rest of the line - which is worse, because it still builds.
    """
    assert escape(raw) == expected


def test_backslash_is_escaped_without_eating_the_others() -> None:
    """Order matters: escaping & before \\ would double-escape its backslash."""
    assert escape(r"a\b&c") == r"a\textbackslash{}b\&c"


# --------------------------------------------------------------- figure blocks
def test_apa_captions_above_and_notes_below() -> None:
    """APA 7 puts the number and title above the image and the note below it.

    This is the whole reason the generator is style-aware rather than one
    template with a dropdown in front of it.
    """
    out = figure_block("out/fig_yield.pdf", caption="Yield vs pH", style="apa",
                       note="Error bars are 1 s.d.", citation=RECORD)
    lines = out["latex"].splitlines()
    caption = next(i for i, l in enumerate(lines) if "\\caption" in l)
    graphic = next(i for i, l in enumerate(lines) if "\\includegraphics" in l)
    note = next(i for i, l in enumerate(lines) if "Note." in l)
    assert caption < graphic < note, out["latex"]
    assert out["caption_above"] is True


@pytest.mark.parametrize("style", ["ieee", "nature", "harvard"])
def test_the_other_styles_caption_below(style: str) -> None:
    out = figure_block("out/fig_yield.pdf", caption="Yield vs pH", style=style)
    lines = out["latex"].splitlines()
    graphic = next(i for i, l in enumerate(lines) if "\\includegraphics" in l)
    caption = next(i for i, l in enumerate(lines) if "\\caption" in l)
    assert graphic < caption, out["latex"]
    assert out["caption_above"] is False


def test_the_block_is_well_formed() -> None:
    out = figure_block("C:/work/out/fig_yield.pdf", caption="Yield", style="ieee")
    latex = out["latex"]
    assert latex.startswith("\\begin{figure}[htbp]")
    assert latex.rstrip().endswith("\\end{figure}")
    assert latex.count("\\begin{figure}") == latex.count("\\end{figure}") == 1
    assert latex.count("\\caption{") == 1
    assert latex.count("\\label{") == 1
    # Windows separators must not reach LaTeX, and the extension is dropped so
    # the build picks pdf or png per engine.
    assert "\\\\" not in latex
    assert "{fig_yield}" in latex
    assert out["reference"] == "Figure~\\ref{fig:fig_yield}"


def test_the_caption_does_not_run_two_sentences_together() -> None:
    """"Yield vs pH Data from Bach..." reads as a mistake in the paper."""
    out = figure_block("f.pdf", caption="Yield vs pH", style="ieee", citation=RECORD)
    assert "pH. Data from" in out["latex"]


def test_the_citation_follows_the_block_style() -> None:
    """An IEEE figure must not carry an APA source line underneath it."""
    ieee = figure_block("f.pdf", caption="c", style="ieee", citation=RECORD)["latex"]
    apa = figure_block("f.pdf", caption="c", style="apa", citation=RECORD)["latex"]
    assert 'vol. 49' in ieee and 'vol. 49' not in apa
    assert '49(3)' in apa and '49(3)' not in ieee


def test_bibtex_is_refused_as_a_figure_style_and_returned_separately() -> None:
    """It belongs in the .bib file, not inside a \\caption."""
    with pytest.raises(LatexError, match="(?i)bibliography format"):
        figure_block("f.pdf", style="bibtex")
    out = figure_block("f.pdf", caption="c", style="apa", citation=RECORD)
    assert out["bibtex"].startswith("@")


def test_a_missing_image_path_is_refused() -> None:
    with pytest.raises(LatexError, match="(?i)needs the path"):
        figure_block("   ", caption="c")


def test_the_label_is_safe_for_latex() -> None:
    """A dataset name becomes a \\label key, and keys cannot carry spaces."""
    out = figure_block("f.pdf", dataset="H2 yield (%) run 3", style="apa")
    assert out["label"] == "fig:h2-yield-run-3", out["label"]


def test_it_is_reachable_through_the_operation_dispatcher() -> None:
    """The UI calls operations, not modules."""
    import pandas as pd

    from graphvis_science.operations import run

    out = run("latex_figure", pd.DataFrame(),
              {"image_path": "out/fig.pdf", "caption": "C", "style": "nature"})
    assert out["latex"].startswith("\\begin{figure}")
    assert out["style"] == "nature"
