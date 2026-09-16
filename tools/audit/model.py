"""What a finding is.

A finding has to survive three different readers:

  * a person skimming AUDIT.md over coffee, who needs to know in one line
    whether this is worth opening the file for;
  * an agent reading AUDIT.json, which needs an anchor it can act on and a
    reason it can evaluate rather than a verdict it must take on trust;
  * the next pass of this audit, which needs to know whether this is the same
    finding it saw last time.

So every finding carries WHERE (file, line, enclosing symbol), WHAT (one
sentence), WHY (the reasoning, spelled out), EVIDENCE (the actual source that
provoked it) and CONFIDENCE (how likely it is to be real). The key
deliberately excludes the line number: a finding that moves because somebody
added a comment above it is not a new finding, and an audit that says it is
becomes noise within a week.
"""
from __future__ import annotations

import hashlib
import re

# Severity says how much it would matter IF the finding is real.
# Confidence says how likely it is to be real. They are different questions
# and collapsing them into one number is how a report ends up either ignored
# or panic-inducing.
SEVERITY = {"high": 3, "medium": 2, "low": 1, "info": 0}
CONFIDENCE = {"certain": 3, "likely": 2, "possible": 1}


class Finding:
    __slots__ = ("check", "file", "line", "symbol", "what", "why", "evidence",
                 "severity", "confidence", "category", "suggestion", "related")

    def __init__(self, check: str, file: str, line: int, symbol: str,
                 what: str, why: str = "", evidence: str = "",
                 severity: str = "medium", confidence: str = "likely",
                 category: str = "correctness", suggestion: str = "",
                 related: list | None = None):
        self.check = check
        self.file = file
        self.line = int(line)
        self.symbol = symbol
        self.what = what
        self.why = why
        self.evidence = evidence
        self.severity = severity
        self.confidence = confidence
        self.category = category
        self.suggestion = suggestion
        self.related = related or []

    @property
    def key(self) -> str:
        """Stable across edits that only move the code."""
        base = f"{self.check}|{self.file}|{self.symbol}|{_shape(self.what)}"
        return hashlib.sha1(base.encode()).hexdigest()[:12]

    @property
    def rank(self) -> tuple:
        return (-SEVERITY.get(self.severity, 1),
                -CONFIDENCE.get(self.confidence, 1),
                self.file, self.line)

    @property
    def where(self) -> str:
        loc = f"{self.file}:{self.line}"
        return f"{loc}  {self.symbol}" if self.symbol else loc

    def to_json(self) -> dict:
        return {
            "id": self.key,
            "check": self.check,
            "category": self.category,
            "severity": self.severity,
            "confidence": self.confidence,
            "file": self.file,
            "line": self.line,
            "symbol": self.symbol,
            "what": self.what,
            "why": self.why,
            "evidence": self.evidence,
            "suggestion": self.suggestion,
            "related": self.related,
        }


def _shape(text: str) -> str:
    """The message with numbers blanked, so counts changing is not a new finding."""
    return re.sub(r"\d+", "#", text)


class CheckInfo:
    """What a check looks for, so a reader can judge the finding themselves."""

    __slots__ = ("id", "title", "rationale", "method", "false_positives")

    def __init__(self, id_: str, title: str, rationale: str, method: str,
                 false_positives: str = ""):
        self.id = id_
        self.title = title
        self.rationale = rationale
        self.method = method
        self.false_positives = false_positives


REGISTRY: dict[str, CheckInfo] = {}


def describe(id_: str, title: str, rationale: str, method: str,
             false_positives: str = "") -> None:
    REGISTRY[id_] = CheckInfo(id_, title, rationale, method, false_positives)
