"""DOI resolution, citation formatting, and provenance for extracted data.

Deliberately dependency-free. Resolving a DOI is one HTTPS GET against
Crossref and one JSON parse, both in the standard library, so this is not an
optional component that can be missing when you need it - which for the thing
that records where a number came from is the wrong failure mode entirely.

The point is provenance. Literature extraction pulls tables and series out of a
PDF; without the citation attached, a figure built from them cannot say where
they came from, and neither can you six months later.
"""
from __future__ import annotations

import json
import re
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

CROSSREF = "https://api.crossref.org/works/"
# Crossref asks for a contactable agent and gives faster service in return.
USER_AGENT = "GraphVis/18.4 (https://github.com/graphvis; mailto:support@graphvis.local)"

_DOI = re.compile(r"\b(10\.\d{4,9}/[-._;()/:A-Z0-9]+)\b", re.I)


class CitationError(ValueError):
    """A DOI that cannot be resolved, said in a sentence."""


def find_dois(text: str) -> list[str]:
    """Every DOI in a block of text, in order, without duplicates.

    Extracted PDF text is where these come from, so trailing punctuation from
    a sentence end is stripped - "10.1016/j.watres.2019.05.001." is a DOI with
    a full stop on it, not a different DOI.
    """
    seen: list[str] = []
    for match in _DOI.finditer(str(text or "")):
        doi = match.group(1).rstrip(".,;)]}")
        if doi.lower() not in {d.lower() for d in seen}:
            seen.append(doi)
    return seen


def _fetch(doi: str, timeout: float) -> dict:
    url = CROSSREF + urllib.parse.quote(doi, safe="")
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT,
                                                   "Accept": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            raise CitationError(f"Crossref has no record of '{doi}'.") from exc
        raise CitationError(f"Crossref returned {exc.code} for '{doi}'.") from exc
    except urllib.error.URLError as exc:
        raise CitationError(
            f"Could not reach Crossref ({exc.reason}). "
            f"Resolving a DOI needs an internet connection."
        ) from exc
    except (ValueError, TimeoutError) as exc:
        raise CitationError(f"Crossref sent something unreadable for '{doi}': {exc}") from exc
    message = payload.get("message")
    if not isinstance(message, dict):
        raise CitationError(f"Crossref sent no work record for '{doi}'.")
    return message


def _authors(work: dict) -> list[dict]:
    out = []
    for person in work.get("author") or []:
        family = str(person.get("family") or "").strip()
        given = str(person.get("given") or "").strip()
        if not family and not given:
            name = str(person.get("name") or "").strip()
            if name:
                out.append({"family": name, "given": ""})
            continue
        out.append({"family": family, "given": given})
    return out


def _year(work: dict) -> int | None:
    for key in ("published-print", "published-online", "issued", "created"):
        parts = (work.get(key) or {}).get("date-parts") or []
        if parts and parts[0] and parts[0][0]:
            return int(parts[0][0])
    return None


def resolve(doi: str, *, timeout: float = 12.0) -> dict:
    """One DOI as a flat, JSON-safe citation record."""
    doi = str(doi or "").strip()
    if not doi:
        raise CitationError("Give a DOI to resolve.")
    found = find_dois(doi)
    doi = found[0] if found else doi
    work = _fetch(doi, timeout)
    title = work.get("title") or []
    container = work.get("container-title") or []
    return {
        "doi": doi,
        "title": str(title[0]) if title else "",
        "authors": _authors(work),
        "year": _year(work),
        "journal": str(container[0]) if container else "",
        "volume": str(work.get("volume") or ""),
        "issue": str(work.get("issue") or ""),
        "pages": str(work.get("page") or ""),
        "publisher": str(work.get("publisher") or ""),
        "type": str(work.get("type") or ""),
        "url": str(work.get("URL") or f"https://doi.org/{doi}"),
    }


def _key(record: dict) -> str:
    authors = record.get("authors") or []
    surname = (authors[0].get("family") if authors else "") or "anon"
    surname = re.sub(r"[^A-Za-z]", "", surname).lower() or "anon"
    year = record.get("year") or "n.d."
    return f"{surname}{year}"


def to_bibtex(record: dict) -> str:
    """A citation record as a BibTeX entry.

    Written rather than parsed, so this needs no BibTeX library. Braces are
    balanced by construction and the title keeps its own capitalisation, which
    is what the extra braces around it are for.
    """
    authors = " and ".join(
        f"{a['family']}, {a['given']}".strip().strip(",")
        for a in record.get("authors") or []
    )
    fields = [
        ("author", authors),
        ("title", "{" + str(record.get("title") or "") + "}"),
        ("journal", record.get("journal")),
        ("year", record.get("year")),
        ("volume", record.get("volume")),
        ("number", record.get("issue")),
        ("pages", record.get("pages")),
        ("publisher", record.get("publisher")),
        ("doi", record.get("doi")),
        ("url", record.get("url")),
    ]
    body = ",\n".join(f"  {name} = {{{value}}}"
                      for name, value in fields if value not in (None, "", []))
    return "@article{" + _key(record) + ",\n" + body + "\n}"


def to_text(record: dict, style: str = "apa") -> str:
    """A citation as a line of prose, for a figure caption or a status line."""
    authors = record.get("authors") or []
    if not authors:
        names = "Anon."
    elif len(authors) == 1:
        names = f"{authors[0]['family']}, {authors[0]['given'][:1]}."
    elif len(authors) <= 3:
        names = ", ".join(f"{a['family']}, {a['given'][:1]}." for a in authors)
    else:
        names = f"{authors[0]['family']}, {authors[0]['given'][:1]}. et al."
    year = record.get("year") or "n.d."
    title = record.get("title") or ""
    journal = record.get("journal") or ""
    volume = record.get("volume") or ""
    pages = record.get("pages") or ""
    if str(style).lower() == "bibtex":
        return to_bibtex(record)
    tail = ", ".join(p for p in (journal, volume, pages) if p)
    return f"{names} ({year}). {title}. {tail}. https://doi.org/{record.get('doi','')}"


def cite(doi: str, *, style: str = "apa", timeout: float = 12.0) -> dict:
    """Resolve a DOI and format it, in one call."""
    record = resolve(doi, timeout=timeout)
    return {"citation": record,
            "text": to_text(record, style),
            "bibtex": to_bibtex(record)}


def cite_many(text: str, *, style: str = "apa", timeout: float = 12.0,
              limit: int = 25) -> dict:
    """Every DOI in a block of text, resolved.

    One that fails is reported next to the ones that worked rather than ending
    the run: a reference list with one dead DOI in it is normal.
    """
    dois = find_dois(text)[:max(1, int(limit))]
    resolved, failed = [], []
    for doi in dois:
        try:
            resolved.append(cite(doi, style=style, timeout=timeout))
        except CitationError as exc:
            failed.append({"doi": doi, "error": str(exc)})
    return {"found": len(dois), "citations": resolved, "failed": failed}
