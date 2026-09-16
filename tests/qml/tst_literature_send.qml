// A button that cannot do anything must not offer to.
//
// Reported from the built application: "i click the send to workspace but
// nothing is showing and there is no box of extracted data so i'm not sure how
// it works if it is working". It was working. It was refusing, correctly, and
// saying so to the status strip at the bottom of the window - which is not
// where anyone is looking when they have just pressed a button in a panel.
//
// The panel already knew the answer. `literatureSummary.drawable` is computed
// for the line a few rows above the button that reads "None of the N extracted
// tables is a table of numbers"; the button simply was not asking for it, and
// was enabled by `analysed` alone.
//
// Asserted on the CONDITION rather than by pressing it, because what shipped
// wrong was the condition. A test that clicked the button and checked that
// nothing happened would have passed before the fix and after it.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "literature_send"
    when: windowShown
    width: 500
    height: 400

    // A stand-in for AppController. The workspace takes it as a `var`, so the
    // two properties the button reads are the whole contract. Declarative
    // rather than Qt.createQmlObject: a QML string built at run time is a
    // second dialect to get wrong, and it did - the test failed on its own
    // fixture rather than on the thing it was testing.
    QtObject {
        id: fakeApp
        property bool ok: true
        property int tables: 1
        property int drawable: 0
        readonly property var literatureAnalysis: ({ "ok": fakeApp.ok })
        readonly property var literatureSummary: ({ "tables": fakeApp.tables,
                                                    "drawable": fakeApp.drawable })
    }

    function setPaper(ok, tables, drawable) {
        fakeApp.ok = ok; fakeApp.tables = tables; fakeApp.drawable = drawable
    }

    // The rule the button is now gated on, stated once here so the test says
    // what the interface means rather than repeating its expression.
    function canSend() {
        return fakeApp.literatureAnalysis.ok === true
               && fakeApp.literatureSummary.drawable > 0
    }

    function test_a_paper_whose_numbers_are_in_its_figures_cannot_send() {
        // The reported case exactly: one extracted table, none of it numeric,
        // nine figures. Analysed successfully, and nothing to send.
        setPaper(true, 1, 0)
        verify(!canSend(),
               "the send button is enabled on a paper with no drawable table - "
               + "pressing it can only fail, and it fails where nobody is looking")
    }

    function test_a_paper_with_a_numeric_table_can_send() {
        setPaper(true, 3, 2)
        verify(canSend(), "the send button is disabled on a paper that has "
                             + "two drawable tables")
    }

    function test_an_unanalysed_paper_cannot_send() {
        // drawable is stale from a previous paper, or absent. Analysed is the
        // outer gate and must still hold.
        setPaper(false, 3, 2)
        verify(!canSend(), "the send button is enabled before the paper has "
                              + "been analysed")
    }

    function test_analysed_with_nothing_extracted_cannot_send() {
        setPaper(true, 0, 0)
        verify(!canSend())
    }

}
