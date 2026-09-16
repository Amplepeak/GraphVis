"""The extracted table that gets sent to the workspace.

"Send extracted data to workspace" took the FIRST extracted dataset. A paper
produced twelve and the first was its reference list — prose that pdfplumber
read as a table, whose only numeric-looking column was a run of publication
years. One row survived coercion, the canvas drew a single point, and the
button looked broken.

These check the measurement the choice is now made on, against tables shaped
like the ones that actually come out of a PDF.
"""
import pandas as pd

from graphvis_science.literature.extractor import plottability


def test_reference_list_is_not_drawable():
    # What the paper actually produced: clipped column names, prose cells, and
    # one column of years.
    refs = pd.DataFrame({
        "y and E": ["Smith and Jones", "Patel", "Okafor et al.", "Lund"],
        "nvironme": ["Water Research", "Bioresour. Technol.", "Env. Sci.", "J. Clean. Prod."],
        "Year": ["1997", "2001", "2004", "2006"],
    })
    score = plottability(refs)
    assert score["numeric_columns"] == 1, score
    assert score["plottable_rows"] == 0, score


def test_a_real_results_table_is_drawable():
    data = pd.DataFrame({
        "Time (h)": [0, 6, 12, 24, 48, 72],
        "H2 (mL)": [0.0, 12.4, 48.1, 96.7, 142.0, 151.3],
        "pH": [7.0, 6.4, 5.9, 5.5, 5.4, 5.4],
    })
    score = plottability(data)
    assert score["numeric_columns"] == 3, score
    assert score["plottable_rows"] == 6, score


def test_one_footnote_marker_does_not_disqualify_a_column():
    # A real table carries the occasional n/a. Throwing the column away for it
    # would reject the data and keep the reference list.
    data = pd.DataFrame({
        "Run": [1, 2, 3, 4, 5, 6],
        "Yield": ["2.1", "2.4", "n/a", "2.9", "3.1", "3.0"],
    })
    score = plottability(data)
    assert "Yield" in score["numeric_names"], score
    assert score["plottable_rows"] == 5, score


def test_a_column_that_is_mostly_prose_is_not_numeric():
    data = pd.DataFrame({
        "Sample": ["A1", "A2", "A3", "A4", "A5"],
        "Note": ["blank", "spiked", "blank", "spiked", "12"],
        "Value": [1.0, 2.0, 3.0, 4.0, 5.0],
    })
    score = plottability(data)
    assert score["numeric_names"] == ["Value"], score
    assert score["plottable_rows"] == 0, score


def test_two_numeric_columns_that_never_overlap_score_zero():
    # Counted on the frame, not summed per column: a table where the numbers
    # are never on the same row has no points on it.
    data = pd.DataFrame({
        "a": [1.0, 2.0, 3.0, None, None, None],
        "b": [None, None, None, 4.0, 5.0, 6.0],
    })
    score = plottability(data)
    assert score["numeric_columns"] == 2, score
    assert score["plottable_rows"] == 0, score


def test_a_table_of_page_furniture_has_no_column_names():
    # What twelve of twelve actually looked like: pdfplumber reading a contents
    # page, a list of equations and a running head, with no header row to find.
    for columns in (["demic", "i"], ["of Fi", "col_16"], ["List of E", "col_13"],
                    ["4.1.5", "31"], ["col_0", "col_2"], ["3 Global System", "col_6"]):
        frame = pd.DataFrame({c: [1.0, 2.0, 3.0] for c in columns})
        assert plottability(frame)["named_columns"] < 2, columns


def test_a_real_table_has_names_on_its_columns():
    data = pd.DataFrame({"Time (h)": [0, 6, 12], "H2 (mL)": [0.0, 12.4, 48.1]})
    assert plottability(data)["named_columns"] == 2


def test_an_empty_table_is_handled():
    assert plottability(pd.DataFrame())["plottable_rows"] == 0
