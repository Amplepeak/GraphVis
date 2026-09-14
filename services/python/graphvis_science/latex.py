"""A figure as a LaTeX block, in the style the journal actually wants.

The part of "export for my paper" that is not the image. GraphVis can already
write a figure as SVG, PDF or PNG; what costs a person twenty minutes per figure
is the block around it - the float, the width, the caption, the label, the
citation of the paper the data came from, and the fact that every style puts
those in a different order.

That last point is the whole reason this is not one hard-coded template. APA 7
puts the figure number and title ABOVE the image and an explanatory note BELOW
it; IEEE, Nature and Harvard all caption below. A generator that emits one
layout and lets the user pick a style from a dropdown is the same fault as a
citation formatter that ignores its style argument, which is what this module's
neighbour used to do.

Nothing here talks to the network or the filesystem. It takes what the caller
already has - a saved image path and, optionally, a resolved citation record -
and returns text.
"""
from __future__ import annotations

from typing import Any, Mapping

from graphvis_science.citations import STYLES, to_text


class LatexError(ValueError):
    """An input that cannot be turned into a figure block, said in a sentence."""


# Characters that mean something to TeX, and what each becomes.
#
# Applied in ONE pass over the input, not as a sequence of str.replace calls.
# Sequential replacement cannot work here whatever order it is done in: the
# replacements for \, ~ and ^ themselves contain braces, so any later rule for
# { and } corrupts them - "a\b" came out as "a\textbackslash\{\}b", which is
# not valid LaTeX. Doing it character by character means no replacement is ever
# re-examined.
_ESCAPES = {
    "\\": r"\textbackslash{}",
    "&": r"\&", "%": r"\%", "$": r"\$", "#": r"\#",
    "_": r"\_", "{": r"\{", "}": r"\}",
    "~": r"\textasciitilde{}", "^": r"\textasciicircum{}",
}


def escape(text: Any) -> str:
    """Prose made safe to paste into a .tex file.

    A caption is the one place a dataset name reaches LaTeX unfiltered, and
    scientific column names are full of underscores and per-cent signs -
    `H2_yield (%)` alone carries two characters that break a build and one that
    silently eats the rest of the line. Getting this wrong produces an error
    message pointing at the caption rather than at the name inside it.
    """
    out = []
    for c in str(text if text is not None else ""):
        if c in _ESCAPES:
            out.append(_ESCAPES[c])
        elif c in "\n\t":
            # A newline inside \caption{} is legal but reads as a paragraph
            # break in some classes; a space is what was meant.
            out.append(" ")
        elif ord(c) < 0x20 or ord(c) == 0x7F:
            # Control characters have no meaning in a caption, and a NUL in a
            # .tex file is a build error at best. Dropped rather than escaped:
            # there is no correct rendering of one.
            continue
        else:
            out.append(c)
    return "".join(out)


def _sentence(text: str) -> str:
    """End a caption fragment with a stop, so the next clause does not run on.

    "H2 yield vs pH Data from Bach..." is what happens without this, and it
    reads as a mistake in the paper rather than in the tool that wrote it.
    """
    out = str(text).rstrip()
    return out if (not out or out[-1] in ".?!") else out + "."


def _label(stem: str) -> str:
    """A \\label key: lower case, words joined by hyphens, nothing exotic."""
    safe = "".join(c if (c.isalnum() or c in "-_") else "-" for c in str(stem).lower())
    while "--" in safe:
        safe = safe.replace("--", "-")
    return safe.strip("-") or "figure"


def figure_block(image_path: str,
                 *,
                 caption: str = "",
                 style: str = "apa",
                 label: str = "",
                 width: float = 0.9,
                 placement: str = "htbp",
                 note: str = "",
                 citation: Mapping[str, Any] | None = None,
                 dataset: str = "") -> dict:
    """One `figure` environment, ready to paste.

    `citation`, when given, is a record as `citations.resolve` returns it; it is
    formatted in the same style as the block and appended to the caption or the
    note, wherever that style puts a source.
    """
    key = str(style or "apa").strip().lower()
    if key == "bibtex":
        # BibTeX is a bibliography format, not a caption style; silently
        # treating it as one would put an @article block inside a \caption.
        raise LatexError(
            "BibTeX is a bibliography format, not a figure style. "
            "Choose apa, ieee, nature or harvard - the BibTeX entry is returned "
            "alongside the block for your .bib file.")
    if key not in STYLES:
        raise LatexError(
            f"Unknown citation style {style!r}. "
            f"Choose one of: {', '.join(s for s in STYLES if s != 'bibtex')}.")
    if not str(image_path).strip():
        raise LatexError("A figure block needs the path of a saved image.")

    # LaTeX wants forward slashes even on Windows, and \includegraphics is
    # happier without the extension so the build picks pdf/png per engine.
    path = str(image_path).replace("\\", "/")
    stem = path.rsplit("/", 1)[-1]
    graphic = stem.rsplit(".", 1)[0] if "." in stem else stem

    key_label = _label(label or dataset or graphic)
    caption_text = escape(caption or dataset or graphic.replace("_", " "))
    note_text = escape(note)

    source = ""
    if citation:
        try:
            source = escape(to_text(dict(citation), key))
        except Exception as exc:                        # noqa: BLE001
            raise LatexError(f"The citation could not be formatted: {exc}") from exc

    lines = [f"\\begin{{figure}}[{placement}]", "  \\centering"]

    if key == "apa":
        # APA 7: the number and title sit ABOVE the image; the note goes below.
        lines.append(f"  \\caption{{{caption_text}}}")
        lines.append(f"  \\label{{fig:{key_label}}}")
        lines.append(f"  \\includegraphics[width={width}\\linewidth]{{{graphic}}}")
        below = " ".join(p for p in (_sentence(note_text),
                                     (f"Data from {source}" if source else "")) if p)
        if below:
            lines.append(f"  \\par\\vspace{{2pt}}")
            lines.append(f"  \\begin{{flushleft}}\\footnotesize \\textit{{Note.}} {below}\\end{{flushleft}}")
    else:
        # IEEE, Nature and Harvard all caption below the image.
        lines.append(f"  \\includegraphics[width={width}\\linewidth]{{{graphic}}}")
        full = _sentence(caption_text)
        if source:
            full = f"{full} Data from {source}"
        if note_text:
            full = f"{_sentence(full)} {note_text}"
        lines.append(f"  \\caption{{{full}}}")
        lines.append(f"  \\label{{fig:{key_label}}}")

    lines.append("\\end{figure}")
    block = "\n".join(lines)

    out: dict[str, Any] = {
        "latex": block,
        "label": f"fig:{key_label}",
        "style": key,
        "graphic": graphic,
        "reference": f"Figure~\\ref{{fig:{key_label}}}",
        "caption_above": key == "apa",
        # \includegraphics needs this in the preamble, and forgetting it is the
        # commonest reason a pasted block does not build.
        "preamble": "\\usepackage{graphicx}",
    }
    if citation:
        from graphvis_science.citations import to_bibtex
        out["bibtex"] = to_bibtex(dict(citation))
        out["citation_text"] = to_text(dict(citation), key)
    return out
