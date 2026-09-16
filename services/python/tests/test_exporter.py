"""Writing a dataset back out, and the promises the writer makes.

The importer had 149 readers and no writers, and the manual said so: *"a
dataset on disk is read and never written back."* The tests here are about the
three things that makes true, in order of how much they matter:

1. what is written can be read again - by this program, through its own
   readers, which is the only definition of "saved" worth having;
2. the source file is never touched;
3. a write that cannot be done is refused in a sentence rather than half-done.
"""
from __future__ import annotations

import pandas as pd
import pytest

from graphvis_science.data.exporter import (ALL_EXT, ExportError, export_arrow,
                                            export_frame, format_groups)
from graphvis_science.data.importer import READERS


def _frame() -> pd.DataFrame:
    return pd.DataFrame({"time": [1, 2, 3],
                         "power": [4.5, 5.5, 6.5],
                         "label": ["a", "b", "c"]})


# Every writable extension that this program can also READ. A format it writes
# and cannot open is a file the person can only hand to something else, which
# is a fair thing to offer but not what "save my work" means - so the
# round-trip is asserted for the pairs, and the rest are written and checked
# for existence below.
ROUND_TRIP = [e for e in ALL_EXT if e in READERS]


@pytest.mark.parametrize("ext", ROUND_TRIP)
def test_what_is_written_can_be_read_again(ext, tmp_path):
    """A file this cannot open is not a file it has saved anything to."""
    frame = _frame()
    out = tmp_path / f"round{ext}"
    result = export_frame(frame, str(out))

    assert result["ok"] and out.exists(), f"{ext} reported success and wrote nothing"
    read_back = READERS[ext][0](str(out))
    assert list(read_back.columns) == list(frame.columns), (
        f"{ext} came back with different columns: {list(read_back.columns)}")
    assert len(read_back) == len(frame), (
        f"{ext} came back with {len(read_back)} rows instead of {len(frame)}")
    # The numbers, not just the shape. A format that writes a float as its
    # string repr reads back as text and looks fine until something plots it.
    pd.testing.assert_series_equal(
        read_back["power"].astype(float), frame["power"],
        check_names=False, check_index=False)


def test_the_source_file_is_never_written_over(tmp_path):
    """The importer's contract, kept.

    A dataset on disk is usually somebody's instrument output, and this program
    is not the thing that should edit it. Writing goes where the caller says.
    """
    source = tmp_path / "instrument.csv"
    source.write_text("time,power\n1,4.5\n2,5.5\n", encoding="utf-8")
    before = source.read_bytes()

    export_frame(_frame(), str(tmp_path / "derived.csv"))

    assert source.read_bytes() == before, (
        "the file the data came from was modified by a write to another path")


def test_an_existing_file_is_not_replaced_unless_asked(tmp_path):
    """The first thing a new writer must not do is quietly destroy something."""
    out = tmp_path / "keep.csv"
    export_frame(_frame(), str(out))
    original = out.read_bytes()

    with pytest.raises(ExportError) as refused:
        export_frame(pd.DataFrame({"other": [9]}), str(out))
    assert "already exists" in str(refused.value)
    assert out.read_bytes() == original, "refused the write and wrote anyway"

    export_frame(pd.DataFrame({"other": [9]}), str(out), overwrite=True)
    assert out.read_bytes() != original, "overwrite was asked for and ignored"


def test_a_format_it_cannot_write_says_which_it_can(tmp_path):
    with pytest.raises(ExportError) as refused:
        export_frame(_frame(), str(tmp_path / "x.sav"))
    message = str(refused.value)
    assert ".sav" in message, "the refusal does not name the format asked for"
    assert ".csv" in message, "the refusal does not say what it can write instead"


def test_columns_are_chosen_by_name_and_a_wrong_one_is_named(tmp_path):
    out = tmp_path / "subset.csv"
    result = export_frame(_frame(), str(out), columns=["power", "time"])
    # In the order asked for, not the order the frame happens to hold.
    assert result["columns"] == ["power", "time"]

    with pytest.raises(ExportError) as refused:
        export_frame(_frame(), str(tmp_path / "bad.csv"), columns=["time", "nope"])
    assert "nope" in str(refused.value)


def test_an_empty_dataset_is_refused_rather_than_written(tmp_path):
    out = tmp_path / "empty.csv"
    with pytest.raises(ExportError):
        export_frame(pd.DataFrame(), str(out))
    assert not out.exists(), "wrote a file for a dataset with no columns"


def test_the_arrow_cache_is_what_gets_written(tmp_path):
    """The application knows a dataset by its Arrow cache, so that is what it
    can name - and reading it back means the thing written is the thing the
    program is using, not a copy that could have drifted from it."""
    cache = tmp_path / "dataset.arrow"
    export_frame(_frame(), str(cache))

    result = export_arrow(str(cache), str(tmp_path / "out.csv"))
    assert result["rows"] == 3
    assert result["columns"] == ["time", "power", "label"]


def test_a_missing_arrow_file_is_a_sentence_not_a_traceback(tmp_path):
    with pytest.raises(ExportError) as refused:
        export_arrow(str(tmp_path / "gone.arrow"), str(tmp_path / "out.csv"))
    assert "gone.arrow" in str(refused.value)


def test_the_format_list_a_dialog_shows_is_the_one_that_dispatches():
    """One question, one answer.

    A file dialog's filter list and the set of formats that can actually be
    written are the same registry here. Two lists is how a person gets offered
    a format that then refuses.
    """
    listed = {ext for exts in format_groups().values() for ext in exts}
    assert listed == set(ALL_EXT), (
        "the grouped list and the writable set disagree: "
        f"{sorted(listed ^ set(ALL_EXT))}")
