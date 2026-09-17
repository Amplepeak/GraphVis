"""Every citation style as DATA, and one renderer that reads it.

This replaces a chain of ``if key == "ieee": ... if key == "nature": ...``
branches that covered four styles. Four branches is a readable chain; fifty is
fifty places for the same fault, and this project has a standing note about
exactly that shape - *a rule correct for one class applied to all* - which is
what a long dispatch chain of near-copies turns into.

So a style is a record of the decisions that actually differ, and
:func:`render` is the only code that formats a reference. Adding a style is a
row in :data:`STYLE_TABLE`; it cannot introduce a bug in another style, because
there is no code to copy.

WHAT DIFFERS BETWEEN STYLES, mechanically:

* whether author names are inverted for all authors, the first only, or none;
* how initials are punctuated - ``M. J.``, ``M.J.`` or ``MJ``;
* the conjunction before the last author, and whether a comma precedes it;
* how many authors trigger *et al.*, and how many are listed before it;
* where the year sits - after the authors, at the end in parentheses, or in
  among the volume and pages;
* whether the article title is sentence case or title case, and whether it is
  quoted, italic or plain;
* whether the journal name is abbreviated;
* the punctuation of volume, issue and pages, which is where most of the
  variety lives;
* the form of the DOI, of which there are five in use, and whether a full stop
  follows it;
* and, for a figure, whether the caption goes above the image or below it.

Everything above is a field below. Nothing above is a branch.

ITALICS ARE MARKED, NOT APPLIED. The same reference is wanted as plain text in
the interface and as LaTeX in an exported block, and those need different markup
for the same emphasis. :func:`render` returns plain text; :func:`render_latex`
returns the same reference with ``\\textit{...}`` where the style italicises.
Two renderers over one table, rather than two tables.

NOT A CSL IMPLEMENTATION. CSL is a specification for a general-purpose
formatter over a general-purpose data model, and it is large. This formats one
kind of record - a journal article with a DOI, which is what ``citations.resolve``
returns from Crossref - as accurately as it can, and says so. A style whose rules
could not be verified against its own publisher is marked in the table, so the
interface can say which ones are checked against the source and which are
checked only against a library guide.
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any, Mapping

EN_DASH = "–"


class StyleError(ValueError):
    """A style that is not in the table, said in a sentence."""


@dataclass(frozen=True)
class Style:
    """One citation style, as the decisions that distinguish it.

    The defaults are APA 7's, because it is the most common and because a new
    row should have to state only what makes it different.
    """

    key: str
    label: str
    # Only for grouping the picker. "author-date", "numeric", "note".
    family: str = "author-date"
    # Where the rules came from: "official" (the style's own publisher),
    # "library" (a reputable university guide, the manual being paywalled), or
    # "approximate" (no single authority exists for this name).
    source: str = "library"
    # Shown under the picker when a style needs a caveat - a name that is not
    # a single published style, or one whose details could not be verified.
    caveat: str = ""

    # ---- authors
    invert: str = "all"            # "all" | "first" | "none"
    initials: str = "M. J."        # "M. J." | "M.J." | "MJ" | "M J" | "full"
    author_sep: str = ", "
    conj: str = "&"                # "&" | "and" | "" for none
    # The SERIAL comma, which by definition needs a series: "A, B, and C" has
    # one and "A and B" does not. IEEE was coming out "J. S. Bach, and J.
    # Smith" because the flag was applied to a two-name list as well.
    comma_before_conj: bool = True
    # APA is the exception that makes the rule a field: it writes "Smith, J.,
    # & Jones, K." with a comma even when there are only two, and a reader who
    # knows APA sees its absence immediately.
    comma_with_two: bool = True
    et_al_over: int = 20           # more than this many authors truncates
    et_al_show: int = 19           # how many are named before "et al."
    et_al_text: str = "et al."
    # What a long author list becomes. "et_al" names the first `et_al_show` and
    # adds `et_al_text`; "ellipsis_last" names them, then an ellipsis, then the
    # LAST author - which is APA 7's rule for 21 or more and is not et al. at
    # all. A formatter that wrote "et al." there would be wrong in the one case
    # a reader checks.
    truncate_mode: str = "et_al"   # "et_al" | "ellipsis_last"
    # MLA writes "Okonkwo, Maria J., et al."; IEEE writes "M. J. Okonkwo et
    # al." The comma is the whole difference and it is visible in every
    # multi-author reference in the list.
    comma_before_et_al: bool = False
    # "Okonkwo, M. J." against "Okonkwo M.J." - Cambridge's science form keeps
    # the stops in the initials and drops the comma, which neither the dotted
    # nor the run-together setting alone can say.
    invert_comma: bool = True
    # BS ISO 690 sets every surname in capitals. Nothing else in the table
    # does, and it is the first thing that identifies the style on a page.
    surname_caps: bool = False
    authors_end: str = ""          # punctuation closing the author list

    # ---- year
    year_slot: str = "after_authors"   # "after_authors" | "end" | "in_locator"
    year_wrap: str = "({year})."
    # IEEE and the ACM write "Jul. 2023"; NLM writes "2023 Jul". The stop is
    # the difference, and a style that wants no month simply never mentions
    # {month} in its templates.
    month_dotted: bool = True

    # ---- title
    title_case: str = "sentence"   # "sentence" | "title" | "as_given"
    # Most styles capitalise the first word of a subtitle; a few - the British
    # Harvards, Nature, the medical styles - leave it alone. Applies to both
    # sentence case and title case, because it is a rule about the colon rather
    # than about the case.
    capitalise_subtitle: bool = True
    title_wrap: str = "plain"      # "plain" | "quotes" | "single-quotes" | "italic"
    title_end: str = "."
    # American practice puts the comma or full stop INSIDE the closing quote -
    # `"Title,"` - and British practice puts it outside - `'Title',`. It is one
    # character and it is the first thing a copy editor changes.
    punct_inside_quotes: bool = False

    # ---- journal
    journal_abbrev: bool = False
    # Vancouver, NLM, AMA and CSE abbreviate WITHOUT full stops - `J Geophys
    # Res Atmos`, not `J. Geophys. Res. Atmos.` It is the same abbreviation
    # with the dots taken out, so it is a flag rather than a second table.
    journal_abbrev_dots: bool = True
    # ACS keeps the colon of a split title - `J. Geophys. Res.: Atmos.` - and
    # IEEE does not. Same abbreviation, one character apart.
    journal_abbrev_colon: bool = False
    journal_wrap: str = "italic"   # "italic" | "plain"
    journal_end: str = ","

    # ---- volume, issue, pages
    #
    # A template over {volume} {issue} {pages} {year} {article}. A group in
    # square brackets is dropped whole when any field inside it is empty, which
    # is what makes one template cover an article with an issue number and one
    # without.
    locator: str = "{volume}[({issue})][, {pages}]"
    locator_end: str = "."
    page_dash: str = EN_DASH
    elide_pages: bool = False

    # ---- doi
    # "url" | "doi:" | "doi: " | "DOI: " | "DOI " | "(doi:)" | "none"
    # | "Available at: " (Cite Them Right) | "Available from: " (BS ISO 690)
    doi_form: str = "url"
    doi_period: bool = False
    # Copernicus puts the year AFTER the DOI, so the DOI needs a comma of its
    # own: "..., https://doi.org/10.x, 2023."
    doi_end: str = ""

    # ---- the figure block this style wants
    caption_above: bool = False

    # The order the pieces appear in. Every style is some permutation of these.
    order: tuple = ("authors", "year", "title", "journal", "locator", "doi")


# ---------------------------------------------------------------- the pieces


def _initials(given: str, form: str) -> str:
    """'Johann Sebastian' in the punctuation the style uses.

    Every forename, not just the first: a formatter that drops middle initials
    silently changes a name, and several styles distinguish authors by them.
    A hyphenated forename keeps its hyphen - 'Jean-Luc' is 'J.-L.' and not
    'J.', because the two are different people in a reference list.
    """
    parts = [p for p in re.split(r"\s+", str(given or "").strip()) if p]
    out = []
    for part in parts:
        letters = [bit[0].upper() for bit in part.split("-") if bit]
        if not letters:
            continue
        if form in ("MJ", "M J"):
            out.append("".join(letters))
        elif form == "M.J.":
            out.append("-".join(f"{c}." for c in letters))
        elif form == "full":
            out.append(part)
        else:                                   # "M. J."
            out.append("-".join(f"{c}." for c in letters))
    if not out:
        return ""
    if form == "MJ":
        return "".join(out)
    if form == "M J":
        # IOP: spaced initials with no stops, and the surname first without a
        # comma - "Okonkwo M J". Neither the run-together form nor the stopped
        # one, which is why it is its own value rather than a flag on one.
        return " ".join(out)
    if form == "M.J.":
        return "".join(out)
    if form == "full":
        return " ".join(out)
    return " ".join(out)


def _one_author(person: Mapping[str, Any], style: Style, inverted: bool) -> str:
    family = str(person.get("family") or "").strip()
    if style.surname_caps:
        family = family.upper()
    given = str(person.get("given") or "").strip()
    if not family:
        return given
    if not given:
        return family
    initials = _initials(given, style.initials)
    if not initials:
        return family
    if inverted:
        if not style.invert_comma:
            return f"{family} {initials}"
        # "Okonkwo, M. J." - a comma, except for the run-together forms where
        # the initials follow the surname with only a space, which is what
        # Vancouver, CSE and AMA do and is the single most recognisable thing
        # about them.
        return (f"{family} {initials}" if style.initials in ("MJ", "M J")
                else f"{family}, {initials}")
    return f"{initials} {family}"


def authors(record: Mapping[str, Any], style: Style) -> str:
    """The author list, in this style's order, punctuation and truncation."""
    people = list(record.get("authors") or [])
    if not people:
        return "Anon."

    truncated = len(people) > style.et_al_over
    shown = people[: style.et_al_show] if truncated else people

    names = [
        _one_author(p, style,
                    inverted=(style.invert == "all"
                              or (style.invert == "first" and i == 0)))
        for i, p in enumerate(shown)
    ]
    names = [n for n in names if n]
    if not names:
        return "Anon."

    if truncated:
        if style.truncate_mode == "ellipsis_last":
            last = _one_author(people[-1], style,
                               inverted=(style.invert == "all"))
            return style.author_sep.join(names) + ", . . . " + last
        joiner = style.author_sep if style.comma_before_et_al else " "
        return style.author_sep.join(names) + joiner + style.et_al_text
    if len(names) == 1:
        return names[0]
    if not style.conj:
        return style.author_sep.join(names)
    head = style.author_sep.join(names[:-1])
    # Two authors and no comma: "Okonkwo, M. J. & Zhao, L. W.", which is
    # Nature and IEEE. With one: "Okonkwo, M. J., & Zhao, L. W.", which is APA.
    # The difference is one character and it is the thing people notice.
    wants_comma = (style.comma_with_two if len(names) == 2
                   else style.comma_before_conj)
    join = style.author_sep if wants_comma else " "
    return f"{head}{join}{style.conj} {names[-1]}"


_SMALL = {"a", "an", "and", "as", "at", "but", "by", "for", "from", "in",
          "into", "nor", "of", "on", "onto", "or", "over", "per", "the", "to",
          "up", "via", "with", "vs", "vs.", "versus"}


def _title_case(text: str) -> str:
    """Headline capitals, leaving anything already capitalised alone.

    A scientific title is full of things that must not be touched - pH, DNA,
    mRNA, CO2, von Neumann - so a word that already contains a capital past its
    first letter is returned exactly as given. That is why this cannot be
    ``str.title()``, which turns 'pH' into 'Ph' and 'DNA' into 'Dna'.
    """
    words = str(text or "").split(" ")
    out = []
    for i, word in enumerate(words):
        bare = word.strip("([{\"'")
        if not bare:
            out.append(word)
            continue
        if any(c.isupper() for c in bare[1:]):
            out.append(word)                       # pH, DNA, CO2 - leave it
            continue
        first_or_last = (i == 0 or i == len(words) - 1)
        if not first_or_last and bare.lower().rstrip(".,;:") in _SMALL:
            out.append(word.lower())
            continue
        out.append(word[: len(word) - len(bare)] + bare[0].upper() + bare[1:])
    # A SUBTITLE IS CAPITALISED TOO, and the small-word rule above must not
    # stop it: "...Boundary Layers: A Spectral Approach", not "...: a
    # Spectral Approach". "a" is a small word and the word after a colon is
    # never treated as one - nine of the title-case styles were coming out
    # wrong on the one word a reader checks.
    return " ".join(out)


def _sentence_case(text: str) -> str:
    """First word capitalised, and the first word after a colon.

    Everything else is left alone, for the same reason as above: lower-casing a
    scientific title is how 'Raman spectra of MoS2' becomes 'raman spectra of
    mos2'. Crossref titles are usually already in the journal's own case, so
    the safe transformation is to touch as little as possible.
    """
    out = str(text or "").strip()
    if not out:
        return ""
    return out[0].upper() + out[1:]


def _after_colon(text: str) -> str:
    """Capitalise the first word of a subtitle.

    "...boundary layers: a spectral approach" -> "...: A spectral approach".
    Separate from the case functions because it is a rule about the colon, not
    about the case: it applies to sentence-case and title-case styles alike, and
    a handful of both leave it alone.
    """
    return re.sub(r"(:\s+)([a-z])", lambda m: m.group(1) + m.group(2).upper(),
                  str(text or ""))


def _pages(record: Mapping[str, Any], style: Style) -> str:
    """The page range, with this style's dash and elision."""
    raw = str(record.get("pages") or "").strip()
    if not raw:
        return ""
    match = re.match(r"^\s*(\w+)\s*[-–—]+\s*(\w+)\s*$", raw)
    if not match:
        return raw
    start, end = match.group(1), match.group(2)
    if style.elide_pages and start.isdigit() and end.isdigit() and len(start) == len(end):
        # Chicago's rule in the form everyone actually uses: drop the digits the
        # two ends share, keeping at least two. 2451-2470 -> 2451-70.
        keep = 2
        common = 0
        for a, b in zip(start, end):
            if a != b:
                break
            common += 1
        if common and len(end) - common >= 1:
            end = end[max(0, len(end) - max(keep, len(end) - common)):]
    return f"{start}{style.page_dash}{end}"


def _first_page(record: Mapping[str, Any]) -> str:
    raw = str(record.get("pages") or "").strip()
    if not raw:
        return ""
    return re.split(r"[-\u2013\u2014]", raw, 1)[0].strip()


_MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun",
           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")


def _month(record: Mapping[str, Any], dotted: bool) -> str:
    """The publication month, or nothing at all when the record has none.

    An absent month must stay absent rather than become January: a reference
    that states a month the source did not give is a fabricated detail, and it
    is the kind a reader can check.
    """
    raw = record.get("month")
    try:
        index = int(raw)
    except (TypeError, ValueError):
        return ""
    if not 1 <= index <= 12:
        return ""
    name = _MONTHS[index - 1]
    return name + "." if dotted else name


_GROUP = re.compile(r"\[([^\[\]]*)\]")
_FIELD = re.compile(r"\{(\w+)\}")


def _fill(template: str, values: Mapping[str, str]) -> str:
    """A template whose bracketed groups vanish when their fields are empty.

    ``{volume}[({issue})][, {pages}]`` covers an article that has an issue
    number and one that does not, without a second template and without an
    ``if`` per style. A group with no field in it at all is kept.
    """
    def group(match: re.Match) -> str:
        inner = match.group(1)
        names = _FIELD.findall(inner)
        if names and any(not values.get(n) for n in names):
            return ""
        return _FIELD.sub(lambda m: values.get(m.group(1), ""), inner)

    out = _GROUP.sub(group, template)
    return _FIELD.sub(lambda m: values.get(m.group(1), ""), out).strip()


def _doi(record: Mapping[str, Any], style: Style) -> str:
    doi = str(record.get("doi") or "").strip()
    if not doi or style.doi_form == "none":
        return ""
    body = {"url": f"https://doi.org/{doi}",
            "doi:": f"doi:{doi}",
            "doi: ": f"doi: {doi}",
            "DOI: ": f"DOI: {doi}",
            "DOI ": f"DOI {doi}",
            "(doi:)": f"(doi:{doi})",
            "Available at: ": f"Available at: https://doi.org/{doi}",
            "Available from: ": f"Available from: https://doi.org/{doi}"}.get(style.doi_form, f"https://doi.org/{doi}")
    return body + ("." if style.doi_period else "") + style.doi_end


# ------------------------------------------------------------- the renderer


_ITALIC_OPEN = "\x02"
_ITALIC_CLOSE = "\x03"


def _wrap(text: str, how: str) -> str:
    if not text:
        return ""
    if how == "quotes":
        return f"“{text}”"
    if how == "single-quotes":
        return f"‘{text}’"
    if how == "italic":
        return f"{_ITALIC_OPEN}{text}{_ITALIC_CLOSE}"
    return text


def _titled(text: str, style: Style) -> str:
    """The title, wrapped, with its terminator on the correct side of a quote."""
    if not text:
        return ""
    if style.punct_inside_quotes and style.title_wrap in ("quotes", "single-quotes"):
        return _wrap(text + style.title_end, style.title_wrap)
    return _wrap(text, style.title_wrap) + style.title_end


def _abbreviate(journal: str) -> str:
    """A journal title shortened the way the abbreviating styles shorten it.

    Not an ISO 4 implementation - that needs the LTWA word list, which is a
    data file and a licence - but the two rules that produce the right answer
    for most titles: drop the small words, and clip the long ones at their
    conventional point. Where this is not certain the full title is a legal
    thing to print in every one of these styles, so the failure mode is a
    reference that is correct and long rather than one that is wrong.
    """
    words = [w for w in re.split(r"\s+", str(journal or "").strip()) if w]
    if len(words) <= 1:
        return " ".join(words)
    known = {
        "journal": "J.", "international": "Int.", "american": "Am.",
        "european": "Eur.", "physical": "Phys.", "review": "Rev.",
        "letters": "Lett.", "chemistry": "Chem.", "chemical": "Chem.",
        "physics": "Phys.", "biology": "Biol.", "biological": "Biol.",
        "research": "Res.", "science": "Sci.", "sciences": "Sci.",
        "engineering": "Eng.", "geophysical": "Geophys.", "atmospheric": "Atmos.",
        "atmospheres": "Atmos.", "environmental": "Environ.", "technology": "Technol.",
        "applied": "Appl.", "analytical": "Anal.", "molecular": "Mol.",
        "computational": "Comput.", "computer": "Comput.", "mathematics": "Math.",
        "mathematical": "Math.", "statistics": "Stat.", "statistical": "Stat.",
        "medicine": "Med.", "medical": "Med.", "clinical": "Clin.",
        "materials": "Mater.", "energy": "Energy", "transactions": "Trans.",
        "proceedings": "Proc.", "annals": "Ann.", "advances": "Adv.",
        "reports": "Rep.", "bulletin": "Bull.", "quarterly": "Q.",
        "society": "Soc.", "association": "Assoc.", "institute": "Inst.",
        "national": "Natl.", "academy": "Acad.", "natural": "Nat.",
        "nature": "Nature", "communications": "Commun.", "structure": "Struct.",
        "surface": "Surf.", "spectroscopy": "Spectrosc.", "optics": "Opt.",
        "acoustics": "Acoust.", "geology": "Geol.", "hydrology": "Hydrol.",
        "oceanography": "Oceanogr.", "meteorology": "Meteorol.", "climate": "Clim.",
    }
    drop = {"of", "the", "and", "for", "in", "on", "a", "an", "&"}
    out = []
    for word in words:
        core = word.strip(":,")
        key = core.lower()
        if key in drop:
            continue
        # The colon of a split journal title is kept or dropped by the style,
        # so it is preserved here and removed later - `_abbreviate` is not the
        # place that knows.
        keeps_colon = word.rstrip().endswith(":")
        short = known.get(key, core)
        out.append(short + ":" if keeps_colon else short)
    return " ".join(out) if out else " ".join(words)


def _pieces(record: Mapping[str, Any], style: Style) -> dict:
    year = record.get("year") or "n.d."
    title_raw = str(record.get("title") or "").rstrip(".")
    if style.title_case == "title":
        title_text = _title_case(title_raw)
    elif style.title_case == "sentence":
        title_text = _sentence_case(title_raw)
    else:
        title_text = title_raw
    # "as_given" means as given: a style that asks for the title untouched does
    # not want its subtitle capitalised either, and applying the colon rule
    # there was wrong on fifteen rows at once.
    if style.capitalise_subtitle and style.title_case != "as_given":
        title_text = _after_colon(title_text)

    journal_raw = str(record.get("journal") or "")
    journal_text = _abbreviate(journal_raw) if style.journal_abbrev else journal_raw
    if style.journal_abbrev and not style.journal_abbrev_colon:
        journal_text = journal_text.replace(":", "")
    if style.journal_abbrev and not style.journal_abbrev_dots:
        journal_text = journal_text.replace(".", "")
    journal_text = re.sub(r"\s{2,}", " ", journal_text).strip()

    locator = _fill(style.locator, {
        "volume": str(record.get("volume") or ""),
        "issue": str(record.get("issue") or ""),
        "pages": _pages(record, style),
        # AIP, APS, the astronomical journals and IOP cite the FIRST page only:
        # a reference to "128, 2451" is how those literatures are read, and
        # printing the full range there is the mark of a formatter that has one
        # idea of what a page is.
        "first_page": _first_page(record),
        "month": _month(record, style.month_dotted),
        "year": str(year),
        "article": str(record.get("article") or ""),
    })

    author_text = authors(record, style)
    if author_text and style.authors_end and not author_text.endswith(style.authors_end):
        author_text += style.authors_end

    return {
        "authors": author_text,
        "year": (_fill(style.year_wrap, {"year": str(year),
                                          "month": _month(record, style.month_dotted)})
                 if style.year_slot != "in_locator" else ""),
        "title": _titled(title_text, style),
        "journal": (_wrap(journal_text, style.journal_wrap) + style.journal_end)
                   if journal_text else "",
        "locator": (locator + style.locator_end) if locator else "",
        "doi": _doi(record, style),
    }


def _assemble(record: Mapping[str, Any], style: Style) -> str:
    pieces = _pieces(record, style)
    out = " ".join(pieces[name] for name in style.order if pieces.get(name))
    # A style whose journal ends in a comma and whose locator is missing would
    # otherwise leave "Journal, ." on the page.
    out = re.sub(r",\s*\.", ".", out)
    out = re.sub(r"\s{2,}", " ", out)
    return out.strip()


def _tidy(out: str) -> str:
    """Collapse a doubled full stop, once the emphasis markers have gone.

    This used to run inside `_assemble`, with the italic markers still in the
    string - so `Journal.\x03.` did not match `\.{2,}` and an italicised
    journal name ending in a stop kept both, while the identical table entry on
    a plain journal name lost one. Same intent, two outputs, decided by
    something the table does not mention: the shape this project keeps
    recording. Run last, on the finished text, and both agree.
    """
    return re.sub(r"\.{2,}", ".", out)


def render(record: Mapping[str, Any], style: "Style | str") -> str:
    """One reference as a line of plain prose."""
    chosen = style if isinstance(style, Style) else lookup(str(style))
    out = _assemble(record, chosen)
    return _tidy(out.replace(_ITALIC_OPEN, "").replace(_ITALIC_CLOSE, ""))


def render_latex(record: Mapping[str, Any], style: "Style | str") -> str:
    """The same reference with the style's italics as ``\\textit{...}``.

    The escaping of the rest is the caller's job - ``latex.escape`` - and it
    must happen BEFORE this, or the braces this adds are escaped too.
    """
    chosen = style if isinstance(style, Style) else lookup(str(style))
    out = _assemble(record, chosen)
    # Tidied BEFORE the braces go in, for the same reason - a `}` between two
    # stops hides them from the collapse just as the marker did.
    out = _tidy(out.replace(_ITALIC_OPEN, "\x01").replace(_ITALIC_CLOSE, "\x01"))
    parts = out.split("\x01")
    out = "".join(p if i % 2 == 0 else "\\textit{" + p + "}"
                  for i, p in enumerate(parts))
    # TeX quotes are asymmetric ASCII, not the typographic characters: a
    # literal U+201C in a .tex file is an error under pdflatex with the default
    # input encoding, and a straight " gives two right-hand quotes.
    return (out.replace("\u201c", "``").replace("\u201d", "\'\'")
               .replace("\u2018", "`").replace("\u2019", "\'"))


# ---------------------------------------------------------------- the table
#
# THE THREE AT THE TOP ARE THE THREE THIS PROGRAM'S USERS ACTUALLY USE, and the
# rest are alphabetical. GraphVis is a scientific plotting program, so the three
# are APA, Harvard and IEEE - psychology and the social sciences, the British
# and Commonwealth default, and engineering. (The "big three" of a writing
# handbook are APA, MLA and Chicago, which is a humanities list; both MLA and
# Chicago are in the table, further down, where a physicist will never need to
# scroll past them.)
#
# Changing the pinned three is this tuple and nothing else.
PINNED = ("apa", "harvard", "ieee")


STYLE_TABLE: dict[str, Style] = {}


def _add(style: Style) -> None:
    if style.key in STYLE_TABLE:
        raise StyleError(f"Two styles share the key {style.key!r}.")
    STYLE_TABLE[style.key] = style


def lookup(key: str) -> Style:
    """The style with this key, or a refusal that lists the alternatives."""
    want = str(key or "").strip().lower()
    if want in STYLE_TABLE:
        return STYLE_TABLE[want]
    raise StyleError(
        f"Unknown citation style {key!r}. "
        f"Choose one of: {', '.join(sorted(STYLE_TABLE))}.")


def ordered() -> list[Style]:
    """Every style, the pinned three first and the rest alphabetical by label.

    One list, built here, so the picker in the interface and the validation in
    the service cannot come to hold different sets - which is this project's
    most-repeated fault and the reason the style argument used to be accepted
    and ignored.
    """
    pinned = [STYLE_TABLE[k] for k in PINNED if k in STYLE_TABLE]
    rest = sorted((s for s in STYLE_TABLE.values() if s.key not in PINNED),
                  key=lambda s: s.label.lower())
    return pinned + rest


def keys() -> tuple:
    """Every key, in the order :func:`ordered` gives."""
    return tuple(s.key for s in ordered())


# ---- the three at the top

_add(Style(
    key="apa", label="APA (7th edition)", family="author-date", source="library",
    invert="all", initials="M. J.", conj="&", comma_before_conj=True,
    et_al_over=20, et_al_show=19, truncate_mode="ellipsis_last",
    year_slot="after_authors", year_wrap="({year}).",
    title_case="sentence", title_wrap="plain",
    journal_abbrev=False, journal_wrap="italic", journal_end=",",
    locator="{volume}[({issue})][, {pages}]", locator_end=".",
    doi_form="url", doi_period=False,
    # APA 7 is the one common style that captions ABOVE the figure and puts the
    # explanatory note below it. Everything else in this table captions below.
    caption_above=True,
    order=("authors", "year", "title", "journal", "locator", "doi"),
))

_add(Style(
    key="harvard",
    comma_with_two=False,
    label="Harvard", family="author-date", source="approximate",
    caveat="Harvard is a family of styles rather than one published manual; "
           "this is the common form. If your department names a variant, use "
           "Cite Them Right Harvard or Harvard (BS ISO 690) instead.",
    invert="all", initials="M.J.", conj="and", comma_before_conj=False,
    et_al_over=3, et_al_show=1,
    year_slot="after_authors", year_wrap="({year})",
    # The common-denominator Harvard leaves a subtitle lower case, where APA
    # capitalises it. One character, and the thing a British marker looks at.
    title_case="sentence", capitalise_subtitle=False,
    title_wrap="single-quotes", title_end=",",
    journal_abbrev=False, journal_wrap="italic", journal_end=",",
    locator="{volume}[({issue})][, pp. {pages}]", locator_end=".",
    doi_form="url", doi_period=False,
    order=("authors", "year", "title", "journal", "locator", "doi"),
))

_add(Style(
    key="ieee",
    comma_with_two=False,
    label="IEEE", family="numeric", source="library",
    invert="none", initials="M. J.", conj="and", comma_before_conj=True,
    et_al_over=6, et_al_show=1, authors_end=",",
    # IEEE dates a reference to the month - "Jul. 2023" - and the bracketed
    # group vanishes on a record that gives only a year, rather than inventing
    # January.
    year_slot="end", year_wrap="[{month} ]{year},",
    title_case="sentence", title_wrap="quotes", title_end=",",
    punct_inside_quotes=True,
    journal_abbrev=True, journal_wrap="italic", journal_end=",",
    locator="vol. {volume}[, no. {issue}][, pp. {pages}]", locator_end=",",
    doi_form="doi: ", doi_period=True,
    order=("authors", "title", "journal", "locator", "year", "doi"),
))


# ---- the rest, alphabetical by label in the picker
#
# Every row below states only what differs from APA, which is the dataclass's
# default. A field that is not mentioned is APA's, deliberately.
#
# "list every author" is written as a threshold far above any real author list
# rather than as a None, because `et_al_over` is compared with `>` in exactly
# one place and a None there would be a second code path.
_ALL_AUTHORS = 10_000

_add(Style(
    key="aaa",
    comma_with_two=True, label="AAA (American Anthropological Association)",
    family="author-date", source="library",
    caveat="The AAA style guide defers to Chicago author-date for reference "
           "lists and documents only its in-text rule, so the author "
           "threshold here is Chicago's, not AAA's own.",
    invert="first", initials="full", conj="and", et_al_over=6, et_al_show=3,
    authors_end=".",
    year_wrap="{year}.",
    title_case="title", title_wrap="quotes", punct_inside_quotes=True,
    journal_end="",
    locator="{volume}[ ({issue})][: {pages}]",
    doi_period=True,
))

_add(Style(
    key="aas",
    comma_with_two=False, label="AAS (American Astronomical Society)",
    family="numeric", source="official",
    invert="all", initials="M. J.", conj="&", et_al_over=5, et_al_show=3,
    year_wrap="{year},",
    journal_abbrev=True, journal_wrap="plain", journal_end=",",
    locator="{volume}[, {first_page}]", locator_end=",",
    doi_form="doi:",
    # AAS journals carry no article title, so the title is simply not in the
    # order - the renderer drops a piece it is never asked for.
    order=("authors", "year", "journal", "locator", "doi"),
))

_add(Style(
    key="acm",
    comma_with_two=False, label="ACM Reference Format", family="numeric", source="official",
    caveat="ACM states no et al. threshold, so every author is listed.",
    # ACM prints forenames in full - "Maria J. Okonkwo" - where almost every
    # other numeric style initialises them.
    invert="none", initials="full", conj="and", et_al_over=_ALL_AUTHORS,
    authors_end=".",
    year_wrap="{year}.",
    journal_abbrev=True, journal_end="",
    locator="{volume}, {issue} ([{month} ]{year})[, {pages}]",
))

_add(Style(
    key="acs",
    comma_with_two=False,
    journal_abbrev_colon=True, label="ACS (4th edition)", family="numeric", source="library",
    caveat="The 4th-edition ACS Guide lists every author and writes 'DOI:'; "
           "many ACS journals still cap at ten authors and use a doi.org URL.",
    # A SEMICOLON between authors, and no conjunction at all. This is ACS's
    # signature and the thing a formatter written from a comma-separated
    # template gets wrong.
    invert="all", initials="M. J.", author_sep="; ", conj="",
    et_al_over=_ALL_AUTHORS,
    year_slot="in_locator",
    journal_abbrev=True, journal_end="",
    locator="{year}[, {volume}][ ({issue})][, {pages}]",
    doi_form="DOI: ", doi_period=True,
))

_add(Style(
    key="agu",
    comma_with_two=False, label="AGU (American Geophysical Union)",
    family="author-date", source="official",
    # APA in every respect except the threshold: eight or more authors gives
    # the first six, where APA would list twenty. Aliasing AGU to APA is the
    # obvious shortcut and it is wrong for any paper with eight authors, which
    # in geophysics is most of them.
    et_al_over=7, et_al_show=6,
))

_add(Style(
    key="aglc",
    comma_with_two=False, label="AGLC4 (Australian Guide to Legal Citation)",
    family="note", source="library",
    invert="none", initials="full", conj="and", comma_before_conj=False,
    et_al_over=3, et_al_show=1,
    # AGLC rule 1.13 writes "et al" with no full stop after "al".
    et_al_text="et al", authors_end=",",
    year_wrap="({year})",
    title_case="title", title_wrap="single-quotes", title_end="",
    journal_end="",
    locator="{volume}[({issue})][, {pages}]",
    # A legal style that printed a DOI would be flagged by any Australian
    # examiner: AGLC has no DOI element at all.
    doi_form="none",
    order=("authors", "title", "year", "journal", "locator"),
))

_add(Style(
    key="aiaa",
    comma_with_two=False, label="AIAA", family="numeric", source="official",
    invert="all", initials="M. J.", conj="and",
    # "Do not use 'et al.' in reference lists" - AIAA names every author,
    # however many there are.
    et_al_over=_ALL_AUTHORS, authors_end=",",
    year_slot="in_locator",
    title_case="title", title_wrap="quotes", title_end=",",
    punct_inside_quotes=True,
    locator="[Vol. {volume}][, No. {issue}], {year}[, pp. {pages}]",
))

_add(Style(
    key="aip",
    comma_with_two=False, label="AIP (American Institute of Physics)",
    family="numeric", source="approximate",
    caveat="The last full AIP manual is from 1990 and AIP now devolves to "
           "per-journal style sheets, which disagree on whether the volume is "
           "bold and the journal italic, and state no et al. threshold.",
    invert="none", initials="M. J.", conj="and", et_al_over=_ALL_AUTHORS,
    authors_end=",",
    year_slot="in_locator",
    journal_abbrev=True, journal_wrap="plain", journal_end="",
    locator="{volume}[, {first_page}] ({year})", locator_end=",",
    doi_period=True,
    # AIP references usually carry no article title.
    order=("authors", "journal", "locator", "doi"),
))

_add(Style(
    key="ama",
    comma_with_two=False,
    journal_abbrev_dots=False, label="AMA (11th edition)", family="numeric", source="library",
    invert="all", initials="MJ", conj="", et_al_over=6, et_al_show=3,
    authors_end=".",
    year_slot="in_locator",
    # Sentence case, but AMA leaves the subtitle lower-case where APA raises
    # it, so the title is passed through rather than re-cased.
    title_case="as_given",
    journal_abbrev=True, journal_end=".",
    locator="{year}[;{volume}][({issue})][:{pages}]",
    # AMA is one of the few styles that does NOT elide a page range, and it
    # uses a hyphen rather than an en dash.
    page_dash="-",
    doi_form="doi:",
))

_add(Style(
    key="ams-math",
    comma_with_two=False, label="AMS (American Mathematical Society)",
    family="numeric", source="library",
    invert="none", initials="M. J.", conj="and", et_al_over=_ALL_AUTHORS,
    authors_end=",",
    year_slot="in_locator",
    # The article title is ITALIC here and the journal is roman - the reverse
    # of every other style in this table.
    title_case="as_given", title_wrap="italic", title_end=",",
    journal_abbrev=True, journal_wrap="plain", journal_end="",
    locator="{volume} ({year})[, no. {issue}][, {pages}]", locator_end=",",
    doi_form="DOI ", doi_period=True,
))

_add(Style(
    key="ams-met",
    comma_with_two=True, label="AMS (American Meteorological Society)",
    family="author-date", source="official",
    invert="first", initials="M. J.", conj="and", et_al_over=8, et_al_show=1,
    # AMS writes "and Coauthors" where the rest of the world writes "et al.".
    et_al_text="and Coauthors", authors_end=",",
    # A COLON after the year, not a full stop, which is how an AMS reference is
    # recognised at a glance.
    year_wrap="{year}:",
    title_case="as_given",
    journal_abbrev=True, journal_end=",",
    locator="{volume}[, {pages}]", locator_end=",",
    doi_period=True,
))

_add(Style(
    key="alwd",
    comma_with_two=False, label="ALWD Guide to Legal Citation (8th edition)",
    family="note", source="approximate",
    caveat="ALWD has converged on Bluebook; the one difference that matters "
           "here - full journal titles rather than Bluebook's tables - is "
           "attested only by a library guide, not the ALWD manual.",
    invert="none", initials="full", conj="&", comma_before_conj=False,
    et_al_over=_ALL_AUTHORS, authors_end=",",
    year_slot="in_locator",
    title_case="title", title_wrap="italic", title_end=",",
    journal_wrap="plain", journal_end="",
    locator="{volume}[, {pages}] ({year})",
    doi_form="none",
))

_add(Style(
    key="aps",
    comma_with_two=False, label="APS (Physical Review)", family="numeric", source="official",
    invert="none", initials="M. J.", conj="and", et_al_over=10, et_al_show=10,
    authors_end=",",
    year_slot="in_locator",
    title_case="sentence", title_wrap="quotes", title_end=",",
    punct_inside_quotes=True,
    journal_abbrev=True, journal_wrap="plain", journal_end="",
    locator="{volume}[, {first_page}] ({year})",
    # Physical Review references carry no issue number and, in the printed
    # style, no DOI.
    doi_form="none",
))

_add(Style(
    key="aps-rmp",
    comma_with_two=True, label="APS (Reviews of Modern Physics)",
    family="author-date", source="official",
    invert="first", initials="full", conj="and", et_al_over=10, et_al_show=10,
    authors_end=",",
    year_wrap="{year},",
    title_case="sentence", title_wrap="quotes", title_end=",",
    punct_inside_quotes=True,
    journal_abbrev=True, journal_wrap="plain", journal_end="",
    locator="{volume}[, {pages}]",
    doi_form="none",
))

_add(Style(
    key="apsa",
    comma_with_two=True, label="APSA (American Political Science Association)",
    family="author-date", source="official",
    invert="first", initials="full", conj="and", et_al_over=3, et_al_show=1,
    authors_end=".",
    year_wrap="{year}.",
    title_case="title", title_wrap="quotes", punct_inside_quotes=True,
    journal_end="",
    locator="{volume}[ ({issue})][: {pages}]",
    elide_pages=True,
    doi_form="doi: ", doi_period=True,
))

_add(Style(
    key="asa",
    comma_with_two=True, label="ASA (American Sociological Association)",
    family="author-date", source="library",
    invert="first", initials="full", conj="and", et_al_over=10, et_al_show=7,
    authors_end=".",
    year_wrap="{year}.",
    title_case="title", title_wrap="quotes", punct_inside_quotes=True,
    journal_end="",
    # No space after the colon, unlike Chicago's "128 (14): 2451-70". That
    # tight colon is the ASA tell.
    locator="{volume}[({issue})][:{pages}]",
    elide_pages=True,
    doi_form="doi:", doi_period=True,
))

_add(Style(
    key="asce",
    comma_with_two=True, label="ASCE (American Society of Civil Engineers)",
    family="author-date", source="library",
    caveat="ASCE's guidance states no et al. threshold, so every author is "
           "listed.",
    invert="first", initials="M. J.", conj="and", et_al_over=_ALL_AUTHORS,
    authors_end=".",
    year_wrap="{year}.",
    title_wrap="quotes", punct_inside_quotes=True,
    journal_abbrev=True, journal_end=",",
    locator="{volume}[ ({issue})][, {pages}]", locator_end=",",
    doi_period=True,
))

_add(Style(
    key="asme",
    comma_with_two=False, label="ASME", family="numeric", source="official",
    invert="all", initials="M. J.", conj="and", et_al_over=10, et_al_show=7,
    authors_end=",",
    year_wrap="{year},",
    title_case="title", title_wrap="quotes", title_end=",",
    punct_inside_quotes=True,
    journal_abbrev=True, journal_end=",",
    locator="{volume}[({issue})][, pp. {pages}]",
))

_add(Style(
    key="bluebook",
    comma_with_two=False, label="Bluebook (21st/22nd edition)",
    family="note", source="library",
    invert="none", initials="full", conj="&", comma_before_conj=False,
    et_al_over=_ALL_AUTHORS, authors_end=",",
    year_slot="in_locator",
    title_case="title", title_wrap="italic", title_end=",",
    journal_abbrev=True, journal_wrap="plain", journal_end="",
    locator="{volume}[, {pages}] ({year})",
    # Bluebook has no DOI element. Printing one in a law-review footnote is an
    # immediate tell that the citation was machine-made.
    doi_form="none",
))

_add(Style(
    key="bmj",
    comma_with_two=False,
    journal_abbrev_dots=False, label="BMJ", family="numeric", source="approximate",
    caveat="A Vancouver variant. The author threshold, whether the journal is "
           "abbreviated, and the space after the colon could not be verified "
           "against bmj.com - treat this as Vancouver until it is.",
    invert="all", initials="MJ", conj="", et_al_over=6, et_al_show=3,
    authors_end=".",
    year_slot="in_locator",
    title_case="as_given",
    journal_abbrev=True, journal_end="",
    locator="{year}[;{volume}][({issue})][: {pages}]",
    page_dash="-", elide_pages=True,
    doi_form="doi:",
))

_add(Style(
    key="cambridge-a",
    comma_with_two=False, label="CambridgeA", family="author-date",
    source="official",
    caveat="Cambridge University Press publishes two house styles and no "
           "generic 'Cambridge referencing'; this is the minimal-punctuation "
           "one.",
    invert="all", initials="MJ", conj="and", comma_before_conj=False,
    et_al_over=_ALL_AUTHORS,
    year_wrap="({year})",
    title_case="as_given",
    journal_end="",
    locator="{volume}[({issue})][, {pages}]",
))

_add(Style(
    key="cambridge-b",
    comma_with_two=False,
    invert_comma=False, label="CambridgeB", family="author-date",
    source="official",
    caveat="Cambridge University Press publishes two house styles and no "
           "generic 'Cambridge referencing'; this is the traditionally "
           "punctuated one.",
    invert="all", initials="M.J.", conj="and", comma_before_conj=False,
    et_al_over=_ALL_AUTHORS,
    year_wrap="({year})",
    title_case="as_given",
    journal_end="",
    locator="{volume}[({issue})][, {pages}]",
))

_add(Style(
    key="chicago-ad",
    comma_with_two=True, label="Chicago Author-Date (18th edition)",
    family="author-date", source="official",
    # CMOS 18 cut the threshold hard: six authors listed, and beyond that only
    # three. A table still carrying CMOS 17's ten-and-seven would silently
    # produce a seven-name list where the 18th edition wants three.
    invert="first", initials="full", conj="and", et_al_over=6, et_al_show=3,
    authors_end=".",
    year_wrap="{year}.",
    title_case="title", title_wrap="quotes", punct_inside_quotes=True,
    journal_end="",
    locator="{volume}[ ({issue})][: {pages}]",
    elide_pages=True,
    doi_period=True,
))

_add(Style(
    key="chicago-nb",
    comma_with_two=True, label="Chicago Notes-Bibliography (18th edition)",
    family="note", source="official",
    invert="first", initials="full", conj="and", et_al_over=6, et_al_show=3,
    authors_end=".",
    # The year leaves the author slot entirely and reappears in parentheses
    # after the issue. Same authors, same title, different skeleton - which is
    # why this is a row and not a flag on chicago-ad.
    year_slot="in_locator",
    title_case="title", title_wrap="quotes", punct_inside_quotes=True,
    journal_end="",
    locator="{volume}[, no. {issue}] ({year})[: {pages}]",
    elide_pages=True,
    doi_period=True,
))

_add(Style(
    key="cite-them-right",
    comma_with_two=False,
    doi_form="Available at: ", label="Cite Them Right Harvard",
    family="author-date", source="library",
    invert="all", initials="M.J.", conj="and", comma_before_conj=False,
    et_al_over=10, et_al_show=1,
    year_wrap="({year})",
    title_case="as_given", title_wrap="single-quotes", title_end=",",
    journal_end=",",
    locator="{volume}[({issue})][, pp. {pages}]",
    doi_period=True,
))

_add(Style(
    key="copernicus",
    comma_with_two=False,
    doi_end=",", label="Copernicus Publications (EGU)",
    family="author-date", source="official",
    invert="all", initials="M. J.", conj="and", et_al_over=_ALL_AUTHORS,
    # A colon closes the author list, and the year goes to the very end of the
    # entry, after the DOI - the only style here that does either.
    authors_end=":",
    year_wrap="{year}.",
    title_case="as_given", title_end=",",
    journal_abbrev=True, journal_wrap="plain", journal_end=",",
    locator="{volume}[, {pages}]", locator_end=",",
    order=("authors", "title", "journal", "locator", "doi", "year"),
))

_add(Style(
    key="cse-citation-name",
    comma_with_two=False,
    journal_abbrev_dots=False, label="CSE (Citation-Name)",
    family="numeric", source="official",
    caveat="The 9th-edition threshold is used (more than five authors gives "
           "the first one); the 8th edition lists the first ten. Check which "
           "edition your journal wants.",
    invert="all", initials="MJ", conj="", et_al_over=5, et_al_show=1,
    authors_end=".",
    year_slot="in_locator",
    title_case="as_given",
    journal_abbrev=True, journal_wrap="plain", journal_end=".",
    locator="{year}[;{volume}][({issue})][:{pages}]",
))

_add(Style(
    key="cse-citation-sequence",
    comma_with_two=False,
    journal_abbrev_dots=False, label="CSE (Citation-Sequence)",
    family="numeric", source="official",
    caveat="The 9th-edition threshold is used (more than five authors gives "
           "the first one); the 8th edition lists the first ten. Check which "
           "edition your journal wants. The entry is identical to "
           "Citation-Name; only the ordering of the list differs.",
    invert="all", initials="MJ", conj="", et_al_over=5, et_al_show=1,
    authors_end=".",
    year_slot="in_locator",
    title_case="as_given",
    journal_abbrev=True, journal_wrap="plain", journal_end=".",
    locator="{year}[;{volume}][({issue})][:{pages}]",
))

_add(Style(
    key="cse-name-year",
    comma_with_two=False,
    journal_abbrev_dots=False, label="CSE (Name-Year)", family="author-date",
    source="official",
    caveat="The 9th-edition threshold is used (more than five authors gives "
           "the first one); the 8th edition lists the first ten. Check which "
           "edition your journal wants.",
    invert="all", initials="MJ", conj="", et_al_over=5, et_al_show=1,
    authors_end=".",
    year_wrap="{year}.",
    title_case="as_given",
    journal_abbrev=True, journal_wrap="plain", journal_end=".",
    locator="{volume}[({issue})][:{pages}]",
))

_add(Style(
    key="elsevier-harvard",
    comma_with_two=False, label="Elsevier (Harvard)", family="author-date",
    source="official",
    caveat="Elsevier is a menu of four reference styles, not one; each journal "
           "picks. This is Elsevier's name-date style.",
    invert="all", initials="M.J.", conj="", et_al_over=_ALL_AUTHORS,
    authors_end=",",
    year_wrap="{year}.",
    title_case="as_given",
    journal_wrap="plain", journal_end="",
    locator="{volume}[ ({issue})][, {pages}]",
    doi_form="doi:", doi_period=True,
))

_add(Style(
    key="elsevier-numbered",
    comma_with_two=False, label="Elsevier (Numbered)", family="numeric",
    source="official",
    caveat="Elsevier is a menu of four reference styles, not one; each journal "
           "picks. This is Elsevier's numbered style.",
    invert="none", initials="M.J.", conj="", et_al_over=_ALL_AUTHORS,
    authors_end=",",
    year_slot="in_locator",
    title_case="as_given", title_end=",",
    journal_wrap="plain", journal_end="",
    locator="{volume}[ ({issue})] ({year})[ {pages}]",
    doi_form="doi:", doi_period=True,
))

_add(Style(
    key="elsevier-vancouver",
    comma_with_two=False, label="Elsevier (Vancouver)", family="numeric",
    source="official",
    caveat="Elsevier is a menu of four reference styles, not one; each journal "
           "picks. Elsevier's fourth style differs from this only in the "
           "in-text marker, so it is not a separate row.",
    invert="all", initials="MJ", conj="", et_al_over=6, et_al_show=6,
    authors_end=".",
    year_slot="in_locator",
    title_case="as_given",
    journal_abbrev=True, journal_wrap="plain", journal_end="",
    locator="{year}[;{volume}][({issue})][:{pages}]",
    elide_pages=True,
    doi_form="doi:", doi_period=True,
))

_add(Style(
    key="gsa",
    comma_with_two=False, label="GSA (Geological Society of America)",
    family="author-date", source="official",
    caveat="GSA's guidance states no et al. threshold for journal articles, so "
           "every author is listed.",
    invert="all", initials="M.J.", conj="and", et_al_over=_ALL_AUTHORS,
    authors_end=",",
    year_wrap="{year},",
    # A COLON closes the title and introduces the journal, where every other
    # author-date style uses a full stop.
    title_end=":",
    journal_wrap="plain", journal_end=",",
    locator="[v. {volume}][, no. {issue}][, p. {pages}]", locator_end=",",
    doi_period=True,
))

_add(Style(
    key="harvard-bs",
    comma_with_two=True,
    surname_caps=True,
    doi_form="Available from: ", label="Harvard (BS ISO 690)", family="author-date",
    source="library",
    caveat="BS ISO 690 is a framework rather than a fixed layout, and its UK "
           "implementations disagree - notably '&' versus 'and'. Surnames are "
           "capitalised and a viewed-date is required in the standard; neither "
           "is produced here.",
    invert="first", initials="M.J.", conj="&", comma_before_conj=False,
    et_al_over=3, et_al_show=1, authors_end=",",
    year_wrap="{year}.",
    title_case="as_given",
    journal_end=" [online].",
    locator="{volume}[({issue})][, pp. {pages}]",
))

_add(Style(
    key="iop",
    comma_with_two=False,
    label="IOP Publishing", family="numeric", source="official",
    # Spaced initials with no stops - "Okonkwo M J" - which is neither the
    # run-together form nor the stopped one.
    invert="all", initials="M J", conj="and", comma_before_conj=False,
    doi_form="none",
    et_al_over=10, et_al_show=10,
    year_wrap="{year}",
    journal_abbrev=True, journal_end="",
    # No punctuation at all between volume and pages.
    locator="{volume}[ {first_page}]", locator_end="",
    # IOP's numerical style omits the article title.
    order=("authors", "year", "journal", "locator", "doi"),
))

_add(Style(
    key="jama",
    comma_with_two=False,
    journal_abbrev_dots=False, label="JAMA", family="numeric", source="library",
    invert="all", initials="MJ", conj="", et_al_over=6, et_al_show=3,
    authors_end=".",
    year_slot="in_locator",
    title_case="as_given",
    journal_abbrev=True, journal_end=".",
    locator="{year}[;{volume}][({issue})][:{pages}]",
    page_dash="-",
    doi_form="doi:",
))

_add(Style(
    key="mhra",
    comma_with_two=True, label="MHRA (4th edition)", family="note", source="library",
    caveat="Whether an MHRA bibliography entry takes a terminal full stop "
           "could not be confirmed against the 4th-edition manual; it is "
           "omitted here, as library renderings omit it.",
    invert="first", initials="full", conj="and", et_al_over=3, et_al_show=1,
    # MHRA and OSCOLA say "and others", not "et al.".
    et_al_text="and others", authors_end=",",
    year_slot="in_locator",
    title_case="title", title_wrap="single-quotes", title_end=",",
    journal_end=",",
    # Volume and issue joined by a full stop - "128.14" - not parentheses.
    locator="{volume}[.{issue}] ({year})[, pp. {pages}]", locator_end=",",
    elide_pages=True,
    doi_form="doi:",
))

_add(Style(
    key="mla",
    comma_with_two=True,
    comma_before_et_al=True, label="MLA (9th edition)", family="author-date",
    source="library",
    # Three authors or more collapses to the first name and et al. - there is
    # no long-list form in MLA at all, so a table carrying APA's twenty would
    # print nineteen names where MLA wants one.
    invert="first", initials="full", conj="and", et_al_over=2, et_al_show=1,
    authors_end=".",
    year_slot="in_locator",
    title_case="title", title_wrap="quotes", punct_inside_quotes=True,
    journal_end=",",
    locator="[vol. {volume}][, no. {issue}], {year}[, pp. {pages}]",
    locator_end=",",
    page_dash="-", elide_pages=True,
    doi_period=True,
))

_add(Style(
    key="nature",
    comma_with_two=False, label="Nature", family="numeric", source="official",
    # No comma before the ampersand, where APA always has one. One character,
    # and it is the first thing a Nature copy editor changes.
    invert="all", initials="M. J.", conj="&", comma_before_conj=False,
    et_al_over=5, et_al_show=1,
    year_slot="in_locator",
    title_case="as_given",
    journal_abbrev=True, journal_end="",
    locator="{volume}[, {pages}] ({year})",
    # Nature omits the DOI for conventionally published articles. A reference
    # carrying one is off house style, not more helpful.
    doi_form="none",
))

_add(Style(
    key="nlm",
    comma_with_two=False,
    month_dotted=False,
    journal_abbrev_dots=False, label="NLM (Citing Medicine)", family="numeric",
    source="library",
    caveat="Citing Medicine's default is to list every author; library guides "
           "quote caps of three and of six. All authors are listed here.",
    invert="all", initials="MJ", conj="", et_al_over=_ALL_AUTHORS,
    authors_end=".",
    year_slot="in_locator",
    title_case="as_given",
    journal_abbrev=True, journal_wrap="plain", journal_end=".",
    # "2023 Jul;128(14):2451-70" - the month is part of the date element, not
    # a separate one, and it disappears with the rest when the record has none.
    locator="{year}[ {month}][;{volume}][({issue})][:{pages}]",
    page_dash="-", elide_pages=True,
    doi_form="doi: ",
))

_add(Style(
    key="oscola",
    comma_with_two=False, label="OSCOLA (4th edition)", family="note",
    source="library",
    invert="all", initials="MJ", conj="and", comma_before_conj=False,
    et_al_over=3, et_al_show=1, et_al_text="and others", authors_end=",",
    year_wrap="({year})",
    title_case="title", title_wrap="single-quotes", title_end="",
    journal_abbrev=True, journal_wrap="plain", journal_end="",
    locator="{volume}[, {pages}]", locator_end="",
    # OSCOLA uses no DOIs and no terminal full stop in a bibliography entry.
    doi_form="none",
    order=("authors", "title", "year", "journal", "locator"),
))

_add(Style(
    key="oxford",
    comma_with_two=True, label="Oxford (documentary-note)", family="note",
    source="approximate",
    caveat="There is no single published 'Oxford referencing'. This follows "
           "the University of Southern Queensland's documentary-note form; "
           "other institutions punctuate it differently.",
    invert="first", initials="full", conj="and", comma_before_conj=False,
    et_al_over=_ALL_AUTHORS, authors_end=".",
    year_slot="in_locator",
    title_case="title", title_wrap="single-quotes", title_end=".",
    journal_end=",",
    locator="[vol. {volume}][, no. {issue}], {year}[, pp. {pages}]",
    doi_form="none",
))

_add(Style(
    key="royal-society",
    comma_with_two=False, label="Royal Society", family="numeric",
    source="official",
    invert="all", initials="MJ", conj="", et_al_over=10, et_al_show=10,
    authors_end=".",
    # No punctuation between the year and the title at all.
    year_wrap="{year}",
    title_case="as_given",
    journal_abbrev=True, journal_end="",
    locator="{volume}[, {pages}]",
    doi_form="(doi:)",
))

_add(Style(
    key="rsc",
    comma_with_two=False, label="RSC (Royal Society of Chemistry)", family="numeric",
    source="library",
    caveat="RSC states no et al. threshold, so every author is listed.",
    invert="none", initials="M. J.", conj="and", comma_before_conj=False,
    et_al_over=_ALL_AUTHORS, authors_end=",",
    year_slot="in_locator",
    journal_abbrev=True, journal_end=",",
    locator="{year}[, {volume}][, {pages}]",
    doi_form="none",
    # RSC references carry NO article title. Leaving the title in would be the
    # single most visible error in an RSC reference list.
    order=("authors", "journal", "locator", "doi"),
))

_add(Style(
    key="science",
    comma_with_two=False, label="Science (AAAS)", family="numeric", source="official",
    caveat="AAAS does not state an et al. threshold, so every author is "
           "listed.",
    # "Do not use 'and'" - Science has no conjunction before the last author at
    # all, not even an ampersand.
    invert="none", initials="M. J.", conj="", et_al_over=_ALL_AUTHORS,
    authors_end=",",
    year_slot="in_locator",
    journal_abbrev=True, journal_end="",
    locator="{volume}[, {pages}] ({year})",
    doi_form="none",
))

_add(Style(
    key="tf-harvard-x",
    comma_with_two=False, label="Taylor & Francis (Style X, Harvard)",
    family="author-date", source="official",
    caveat="Taylor & Francis is a menu of lettered styles, not one style; a "
           "journal declares which letter it uses. This is Style X.",
    invert="all", initials="M.J.", conj="", et_al_over=2, et_al_show=1,
    authors_end=",",
    year_wrap="{year}.",
    title_case="as_given",
    journal_end=",",
    locator="{volume}[ ({issue})][, {pages}]",
))

_add(Style(
    key="turabian",
    comma_with_two=True, label="Turabian (9th edition, author-date)",
    family="author-date", source="official",
    caveat="Turabian 9 follows CMOS 17, not CMOS 18: its author threshold and "
           "its 'no. 14' locator both differ from current Chicago.",
    invert="first", initials="full", conj="and", et_al_over=10, et_al_show=7,
    authors_end=".",
    year_wrap="{year}.",
    title_case="title", title_wrap="quotes", punct_inside_quotes=True,
    journal_end="",
    locator="{volume}[, no. {issue}][: {pages}]",
    elide_pages=True,
    doi_period=True,
))

_add(Style(
    key="vancouver",
    comma_with_two=False,
    journal_abbrev_dots=False, label="Vancouver (ICMJE)", family="numeric",
    source="library",
    # Initials run together with no stops and no comma after the surname -
    # "Okonkwo MJ" - and there is no conjunction before the last author.
    invert="all", initials="MJ", conj="", et_al_over=6, et_al_show=6,
    authors_end=".",
    year_slot="in_locator",
    title_case="as_given",
    journal_abbrev=True, journal_wrap="plain", journal_end=".",
    locator="{year}[;{volume}][({issue})][:{pages}]",
    page_dash="-", elide_pages=True,
    doi_form="doi: ",
))
