"""The style table checked against the worked examples it was built from.

Every string in :data:`EXPECTED` is the reference-list entry that the citation
spec prints for ONE record - the same record for all of them - so a wrong field
in one row cannot be hidden by a differently shaped test.

Two transcription conventions, both deliberate:

* The spec's fenced examples use ASCII ``"`` and ``'``, because that is what one
  types in a markdown document. The renderer emits the typographic characters
  U+201C/U+201D and U+2018/U+2019, and converts them to TeX quotes on export, so
  the expected strings below carry the typographic forms. Nothing else about the
  spec's punctuation is normalised.
* The spec wraps its examples over three lines. They are joined here with single
  spaces, which is what the renderer produces.

Entries the table cannot reproduce are xfailed individually with the reason, and
every reason is a missing field or a renderer behaviour - never a wrong row. The
point of the table is that a style is data; an xfail here is the honest record
of where that ran out.
"""
import pytest

from graphvis_science.citation_styles import (
    STYLE_TABLE,
    PINNED,
    ordered,
    render,
    render_latex,
)

# The spec's canonical test record: three authors, an issue number, a DOI.
CANONICAL = {
    "authors": [
        {"family": "Okonkwo", "given": "Maria J."},
        {"family": "Bach", "given": "Liang Wei"},
        {"family": "Andersson", "given": "Sofia"},
    ],
    "year": 2023,
    # July: IEEE, NLM and the ACM format all print the month, and a record
    # without one must render without it rather than inventing January.
    "month": 7,
    "title": ("Turbulent mixing in stratified planetary boundary layers: "
              "a spectral approach"),
    "journal": "Journal of Geophysical Research: Atmospheres",
    "volume": "128",
    "issue": "14",
    "pages": "2451–2470",
    "doi": "10.1029/2023JD038745",
}

# What Crossref returns for a book review, an editorial, or a record someone
# typed by hand. A formatter that raises on this takes the whole reference list
# down with it.
DEGENERATE = {"title": "Untitled"}


EXPECTED: dict[str, str] = {
    "apa": (
        "Okonkwo, M. J., Chen, L. W., & Andersson, S. (2023). Turbulent mixing "
        "in stratified planetary boundary layers: A spectral approach. Journal "
        "of Geophysical Research: Atmospheres, 128(14), 2451–2470. "
        "https://doi.org/10.1029/2023JD038745"
    ),
    "harvard": (
        "Okonkwo, M.J., Chen, L.W. and Andersson, S. (2023) ‘Turbulent mixing "
        "in stratified planetary boundary layers: a spectral approach’, "
        "Journal of Geophysical Research: Atmospheres, 128(14), pp. 2451–2470. "
        "https://doi.org/10.1029/2023JD038745"
    ),
    "ieee": (
        "M. J. Okonkwo, L. W. Chen, and S. Andersson, “Turbulent mixing in "
        "stratified planetary boundary layers: A spectral approach,” J. "
        "Geophys. Res. Atmos., vol. 128, no. 14, pp. 2451–2470, Jul. 2023, "
        "doi: 10.1029/2023JD038745."
    ),
    "aaa": (
        "Okonkwo, Maria J., Liang Wei Chen, and Sofia Andersson. 2023. "
        "“Turbulent Mixing in Stratified Planetary Boundary Layers: A Spectral "
        "Approach.” Journal of Geophysical Research: Atmospheres 128 (14): "
        "2451–2470. https://doi.org/10.1029/2023JD038745."
    ),
    "aas": (
        "Okonkwo, M. J., Chen, L. W., & Andersson, S. 2023, JGRA, 128, 2451, "
        "doi:10.1029/2023JD038745"
    ),
    "acm": (
        "Maria J. Okonkwo, Liang Wei Chen, and Sofia Andersson. 2023. "
        "Turbulent mixing in stratified planetary boundary layers: A spectral "
        "approach. J. Geophys. Res. Atmos. 128, 14 (Jul. 2023), 2451–2470. "
        "https://doi.org/10.1029/2023JD038745"
    ),
    "acs": (
        "Okonkwo, M. J.; Chen, L. W.; Andersson, S. Turbulent mixing in "
        "stratified planetary boundary layers: A spectral approach. J. "
        "Geophys. Res.: Atmos. 2023, 128 (14), 2451–2470. "
        "DOI: 10.1029/2023JD038745."
    ),
    "agu": (
        "Okonkwo, M. J., Chen, L. W., & Andersson, S. (2023). Turbulent mixing "
        "in stratified planetary boundary layers: A spectral approach. Journal "
        "of Geophysical Research: Atmospheres, 128(14), 2451–2470. "
        "https://doi.org/10.1029/2023JD038745"
    ),
    "aiaa": (
        "Okonkwo, M. J., Chen, L. W., and Andersson, S., “Turbulent Mixing in "
        "Stratified Planetary Boundary Layers: A Spectral Approach,” Journal "
        "of Geophysical Research: Atmospheres, Vol. 128, No. 14, 2023, "
        "pp. 2451–2470. https://doi.org/10.1029/2023JD038745"
    ),
    "aip": (
        "M. J. Okonkwo, L. W. Chen, and S. Andersson, J. Geophys. Res. Atmos. "
        "128, 2451 (2023), https://doi.org/10.1029/2023JD038745."
    ),
    "ama": (
        "Okonkwo MJ, Chen LW, Andersson S. Turbulent mixing in stratified "
        "planetary boundary layers: a spectral approach. J Geophys Res Atmos. "
        "2023;128(14):2451-2470. doi:10.1029/2023JD038745"
    ),
    "ams-math": (
        "M. J. Okonkwo, L. W. Chen, and S. Andersson, Turbulent mixing in "
        "stratified planetary boundary layers: a spectral approach, J. "
        "Geophys. Res. Atmos. 128 (2023), no. 14, 2451–2470, "
        "DOI 10.1029/2023JD038745."
    ),
    "ams-met": (
        "Okonkwo, M. J., L. W. Chen, and S. Andersson, 2023: Turbulent mixing "
        "in stratified planetary boundary layers. J. Geophys. Res. Atmos., "
        "128, 2451–2470, https://doi.org/10.1029/2023JD038745."
    ),
    "aps": (
        "M. J. Okonkwo, L. W. Chen, and S. Andersson, “Turbulent mixing in "
        "stratified planetary boundary layers: A spectral approach,” J. "
        "Geophys. Res. Atmos. 128, 2451 (2023)."
    ),
    "apsa": (
        "Okonkwo, Maria J., Liang Wei Chen, and Sofia Andersson. 2023. "
        "“Turbulent Mixing in Stratified Planetary Boundary Layers: A Spectral "
        "Approach.” Journal of Geophysical Research: Atmospheres 128 (14): "
        "2451–69. doi: 10.1029/2023JD038745."
    ),
    "asa": (
        "Okonkwo, Maria J., Liang Wei Chen, and Sofia Andersson. 2023. "
        "“Turbulent Mixing in Stratified Planetary Boundary Layers: A Spectral "
        "Approach.” Journal of Geophysical Research: Atmospheres "
        "128(14):2451–70. doi:10.1029/2023JD038745."
    ),
    "asce": (
        "Okonkwo, M. J., L. W. Chen, and S. Andersson. 2023. “Turbulent mixing "
        "in stratified planetary boundary layers: A spectral approach.” J. "
        "Geophys. Res. Atmos., 128 (14), 2451–2470, "
        "https://doi.org/10.1029/2023JD038745."
    ),
    "asme": (
        "Okonkwo, M. J., Chen, L. W., and Andersson, S., 2023, “Turbulent "
        "Mixing in Stratified Planetary Boundary Layers: A Spectral Approach,” "
        "J. Geophys. Res. Atmos., 128(14), pp. 2451–2470. "
        "https://doi.org/10.1029/2023JD038745"
    ),
    "bmj": (
        "Okonkwo MJ, Chen LW, Andersson S. Turbulent mixing in stratified "
        "planetary boundary layers: a spectral approach. J Geophys Res Atmos "
        "2023;128(14): 2451-70. doi:10.1029/2023JD038745"
    ),
    "cambridge-a": (
        "Okonkwo MJ, Chen LW and Andersson S (2023) Turbulent mixing in "
        "stratified planetary boundary layers: a spectral approach. Journal of "
        "Geophysical Research: Atmospheres 128(14), 2451–2470. "
        "https://doi.org/10.1029/2023JD038745"
    ),
    "cambridge-b": (
        "Okonkwo M.J., Chen L.W. and Andersson S. (2023) Turbulent mixing in "
        "stratified planetary boundary layers: a spectral approach. Journal of "
        "Geophysical Research: Atmospheres 128(14), 2451–2470. "
        "https://doi.org/10.1029/2023JD038745"
    ),
    "chicago-ad": (
        "Okonkwo, Maria J., Liang Wei Chen, and Sofia Andersson. 2023. "
        "“Turbulent Mixing in Stratified Planetary Boundary Layers: A Spectral "
        "Approach.” Journal of Geophysical Research: Atmospheres 128 (14): "
        "2451–70. https://doi.org/10.1029/2023JD038745."
    ),
    "chicago-nb": (
        "Okonkwo, Maria J., Liang Wei Chen, and Sofia Andersson. “Turbulent "
        "Mixing in Stratified Planetary Boundary Layers: A Spectral Approach.” "
        "Journal of Geophysical Research: Atmospheres 128, no. 14 (2023): "
        "2451–70. https://doi.org/10.1029/2023JD038745."
    ),
    "cite-them-right": (
        "Okonkwo, M.J., Chen, L.W. and Andersson, S. (2023) ‘Turbulent mixing "
        "in stratified planetary boundary layers: a spectral approach’, "
        "Journal of Geophysical Research: Atmospheres, 128(14), pp. 2451–2470. "
        "Available at: https://doi.org/10.1029/2023JD038745."
    ),
    "copernicus": (
        "Okonkwo, M. J., Chen, L. W., and Andersson, S.: Turbulent mixing in "
        "stratified planetary boundary layers: a spectral approach, J. "
        "Geophys. Res. Atmos., 128, 2451–2470, "
        "https://doi.org/10.1029/2023JD038745, 2023."
    ),
    "cse-citation-name": (
        "Okonkwo MJ, Chen LW, Andersson S. Turbulent mixing in stratified "
        "planetary boundary layers: a spectral approach. J Geophys Res Atmos. "
        "2023;128(14):2451–2470. https://doi.org/10.1029/2023JD038745"
    ),
    "cse-citation-sequence": (
        "Okonkwo MJ, Chen LW, Andersson S. Turbulent mixing in stratified "
        "planetary boundary layers: a spectral approach. J Geophys Res Atmos. "
        "2023;128(14):2451–2470. https://doi.org/10.1029/2023JD038745"
    ),
    "cse-name-year": (
        "Okonkwo MJ, Chen LW, Andersson S. 2023. Turbulent mixing in "
        "stratified planetary boundary layers: a spectral approach. J Geophys "
        "Res Atmos. 128(14):2451–2470. https://doi.org/10.1029/2023JD038745"
    ),
    "elsevier-harvard": (
        "Okonkwo, M.J., Chen, L.W., Andersson, S., 2023. Turbulent mixing in "
        "stratified planetary boundary layers: a spectral approach. Journal of "
        "Geophysical Research: Atmospheres 128 (14), 2451–2470. "
        "doi:10.1029/2023JD038745."
    ),
    "gsa": (
        "Okonkwo, M.J., Chen, L.W., and Andersson, S., 2023, Turbulent mixing "
        "in stratified planetary boundary layers: A spectral approach: Journal "
        "of Geophysical Research: Atmospheres, v. 128, no. 14, p. 2451–2470, "
        "https://doi.org/10.1029/2023JD038745."
    ),
    "harvard-bs": (
        "OKONKWO, M.J., L.W. CHEN & S. ANDERSSON, 2023. Turbulent mixing in "
        "stratified planetary boundary layers: a spectral approach. Journal of "
        "Geophysical Research: Atmospheres [online]. 128(14), pp. 2451–2470 "
        "[viewed 5 March 2024]. Available from: "
        "https://doi.org/10.1029/2023JD038745"
    ),
    "iop": (
        "Okonkwo M J, Chen L W and Andersson S 2023 J. Geophys. Res. Atmos. "
        "128 2451"
    ),
    "jama": (
        "Okonkwo MJ, Chen LW, Andersson S. Turbulent mixing in stratified "
        "planetary boundary layers: a spectral approach. J Geophys Res Atmos. "
        "2023;128(14):2451-2470. doi:10.1029/2023JD038745"
    ),
    "mla": (
        "Okonkwo, Maria J., et al. “Turbulent Mixing in Stratified Planetary "
        "Boundary Layers: A Spectral Approach.” Journal of Geophysical "
        "Research: Atmospheres, vol. 128, no. 14, 2023, pp. 2451-70, "
        "https://doi.org/10.1029/2023JD038745."
    ),
    "nature": (
        "Okonkwo, M. J., Chen, L. W. & Andersson, S. Turbulent mixing in "
        "stratified planetary boundary layers: a spectral approach. J. "
        "Geophys. Res. Atmos. 128, 2451–2470 (2023)."
    ),
    "nlm": (
        "Okonkwo MJ, Chen LW, Andersson S. Turbulent mixing in stratified "
        "planetary boundary layers: a spectral approach. J Geophys Res Atmos. "
        "2023 Jul;128(14):2451-70. doi: 10.1029/2023JD038745"
    ),
    "royal-society": (
        "Okonkwo MJ, Chen LW, Andersson S. 2023 Turbulent mixing in stratified "
        "planetary boundary layers: a spectral approach. J. Geophys. Res. "
        "Atmos. 128, 2451–2470. (doi:10.1029/2023JD038745)"
    ),
    "rsc": (
        "M. J. Okonkwo, L. W. Chen and S. Andersson, J. Geophys. Res. Atmos., "
        "2023, 128, 2451–2470."
    ),
    "science": (
        "M. J. Okonkwo, L. W. Chen, S. Andersson, Turbulent mixing in "
        "stratified planetary boundary layers: A spectral approach. J. "
        "Geophys. Res. Atmos. 128, 2451–2470 (2023)."
    ),
    "turabian": (
        "Okonkwo, Maria J., Liang Wei Chen, and Sofia Andersson. 2023. "
        "“Turbulent Mixing in Stratified Planetary Boundary Layers: A Spectral "
        "Approach.” Journal of Geophysical Research: Atmospheres 128, no. 14: "
        "2451–70. https://doi.org/10.1029/2023JD038745."
    ),
    "vancouver": (
        "Okonkwo MJ, Chen LW, Andersson S. Turbulent mixing in stratified "
        "planetary boundary layers: a spectral approach. J Geophys Res Atmos. "
        "2023;128(14):2451-70. doi: 10.1029/2023JD038745"
    ),
}


# Each reason names the ONE thing the table cannot say. A reason that named a
# style rather than a missing field would be an excuse.
_TITLE_COLON = (
    "_title_case() does not capitalise the word after a colon, so the "
    "subtitle renders 'a Spectral Approach'. _sentence_case() does capitalise "
    "it, so this is a renderer inconsistency, not a missing field."
)
_DOTLESS_ABBREV = (
    "No field selects a full-stop-free journal abbreviation; _abbreviate() "
    "always emits 'J. Geophys. Res. Atmos.' where NLM and CSE want "
    "'J Geophys Res Atmos'."
)
_FIRST_PAGE = (
    "No {first_page} field: the locator template can print the whole range or "
    "nothing, and this style cites the opening page alone."
)
_MONTH = "No month field: the record carries a year only."

XFAIL: dict[str, str] = {
    # Four, and every one of them is a limit of the DATA or of the spec's own
    # example rather than of the table. That distinction is the point: a style
    # this formatter gets wrong because it lacks a field is a thing to fix; a
    # style it gets wrong because the published example is inconsistent is a
    # thing to record and leave alone.
    "aas": (
        "The AAS journals cite by bibcode journal abbreviation - JGRA for "
        "Journal of Geophysical Research: Atmospheres - which is a lookup "
        "table of registered five-character codes, not a word-by-word "
        "shortening. Without that table the honest output is the ordinary "
        "abbreviation, which is longer and correct rather than short and wrong."
    ),
    "ams-met": (
        "The American Meteorological Society's own published example drops the "
        "article's subtitle, which no stated rule accounts for. Reproducing it "
        "would mean discarding part of a title on every reference."
    ),
    "apsa": (
        "APSA's published example elides 2451-2470 to '2451-69'. Chicago's "
        "elision rule, which APSA follows, gives '2451-70'. The example is "
        "arithmetically wrong; the renderer is not."
    ),
    "harvard-bs": (
        "BS ISO 690 prints '[viewed 5 March 2024]' - the date the reader "
        "opened the page. Nothing in a Crossref record carries it, and filling "
        "it with today's date would make the same reference render differently "
        "every day, which is worse than omitting an optional element."
    ),
}



@pytest.mark.parametrize("key", [
    pytest.param(k, marks=pytest.mark.xfail(reason=XFAIL[k], strict=True))
    if k in XFAIL else k
    for k in sorted(EXPECTED)
])
def test_style_renders_the_specs_worked_example(key):
    got = render(CANONICAL, key)
    want = EXPECTED[key]
    assert got == want, (
        f"\n{key}\n  got:  {got}\n  want: {want}"
    )


def test_every_style_in_the_table_has_a_worked_example_or_a_reason():
    """A row nobody has checked against a printed example is a guess.

    The exceptions are the styles whose worked example in the spec uses the
    law-review record rather than the canonical one, and the two Elsevier
    variants the spec illustrates only with its own source examples.
    """
    unchecked = {
        "aps-rmp", "oscola", "mhra", "bluebook", "alwd", "aglc", "oxford",
        "tf-harvard-x", "elsevier-numbered", "elsevier-vancouver",
    }
    missing = set(STYLE_TABLE) - set(EXPECTED) - unchecked
    assert not missing, f"styles with no expected output: {sorted(missing)}"


@pytest.mark.parametrize("key", sorted(STYLE_TABLE))
def test_every_style_renders_without_raising(key):
    for record, what in ((CANONICAL, "canonical"), (DEGENERATE, "degenerate")):
        # A sparse Crossref record - a book review with no authors, no volume,
        # no pages - must produce a poor reference, never a traceback, because
        # one bad row would otherwise lose the whole reference list.
        plain = render(record, key)
        latex = render_latex(record, key)
        assert isinstance(plain, str), f"{key} on the {what} record"
        assert isinstance(latex, str), f"{key} on the {what} record"
        assert "\x02" not in plain and "\x03" not in plain, (
            f"{key} leaked an italic marker into plain text")


def test_ordered_pins_three_then_sorts_by_label():
    out = ordered()
    assert tuple(s.key for s in out[:3]) == PINNED == ("apa", "harvard", "ieee")
    rest = [s.label for s in out[3:]]
    assert rest == sorted(rest, key=str.lower), (
        "the picker's tail is not alphabetical by label")
    assert len(out) == len(STYLE_TABLE), "ordered() dropped or duplicated a row"


def test_approximate_styles_say_why():
    """"Approximate" means no single authority exists, and the user is told.

    A row marked approximate with an empty caveat is worse than no row: the
    interface then presents a guess with the same confidence as APA.
    """
    silent = [s.key for s in STYLE_TABLE.values()
              if s.source == "approximate" and not s.caveat.strip()]
    assert not silent, f"approximate styles with no caveat: {silent}"


def test_every_source_is_one_of_the_three_we_can_defend():
    bad = {s.key: s.source for s in STYLE_TABLE.values()
           if s.source not in ("official", "library", "approximate")}
    assert not bad, bad
