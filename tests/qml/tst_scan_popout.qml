// The scan results list, in the sidebar and out of it.
//
// Reported as "need to be bigger to see more graphs here make it draggable and
// floatable". The list now moves between the sidebar slot and a window of its
// own, and MOVES is the word: there is one ListView and its `parent` is a
// binding, the same design FigureWindow uses and for the reason recorded there
// - a second list built from the same model looks right until the model
// changes, and then one of the two is showing a scan that no longer exists.
//
// That design is exactly the kind nothing else in this project can check. A
// reparenting binding is not visible to the linter, not visible to the engine
// sweep, and not visible to --selftest-ui, which loads the window and lets it
// settle without pressing anything. It is only wrong once somebody presses the
// button, which is what this does.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "scan_popout"
    when: windowShown
    width: 420
    height: 320

    // A stand-in for AppController. ScanPanel takes it as a `var` and reads a
    // handful of properties off it, so a QtObject carrying those is
    // indistinguishable from the real controller - and using one keeps this
    // test free of the C++ library.
    QtObject {
        id: fakeApp
        property bool busy: false
        property bool scanning: false
        property string activeDatasetId: "d1"
        property bool scienceServiceAvailable: true
        // Declared because the panel reads it into two `bool` properties, and a
        // stand-in that leaves it out puts `undefined` into both. That is the
        // fault tst_bindings exists for, and a test that produces it itself
        // teaches the next reader to ignore the warning it should be catching -
        // so the stand-in carries every property the panel reads, and
        // failOnWarning below makes a missing one a failure rather than noise.
        property bool literatureContextAvailable: true
        property var scanSummary: ({ examined_pairs: 1, analysis_seconds: 0.1,
                                     cached: false, literature_used: 0 })
        property var scanRecommendations: [
            { graph: "Line Chart", score: 0.9, source: "scan",
              mappings: { x: "time", y: "power" }, reason: "monotonic in time" },
            { graph: "4D / 5D Scatter", score: 0.7, source: "literature",
              mappings: { x: "a", y: "b" }, reason: "paired columns" }
        ]
        function scanDataset(seconds, literature, force) {}
        function clearScan() {}
        function clearScanCache() {}
    }

    Component {
        id: scanPanelFactory
        ScanPanel { }
    }

    // THE LIST IS ONE LIST, AND IT TRAVELS.
    //
    // The assertion that matters is not "a window appeared" - a copied list
    // would satisfy that - but that the item in the window is the SAME object
    // that was in the sidebar, and that it goes home again. An identity
    // comparison is the only thing that can tell a move from a copy.
    function test_the_results_list_moves_into_the_window_and_back() {
        failOnWarning(/Unable to assign/)
        var panel = scanPanelFactory.createObject(suite, { app: fakeApp })
        verify(panel !== null, "ScanPanel did not instantiate")

        var list = panel.resultsList
        verify(list !== null, "the panel exposes no results list")
        var docked = list.parent
        verify(docked !== null, "the list has no parent while docked")

        panel.listFloating = true
        verify(list.parent !== docked,
               "turning on floating left the list in the sidebar slot")
        // The same object in both places, which is what makes it a move.
        compare(panel.resultsList, list,
                "the floating list is a different object - it was copied, not moved")

        panel.listFloating = false
        compare(list.parent, docked,
               "docking again did not put the list back where it came from")

        panel.destroy()
    }

    // The model is shared by construction, so this cannot drift - which is the
    // whole point of moving one list rather than building two. Asserted anyway,
    // because "by construction" is what was said about the legend rectangle and
    // the field test before both turned out to be asking different questions of
    // different objects.
    function test_the_floating_list_shows_the_same_results() {
        failOnWarning(/Unable to assign/)
        var panel = scanPanelFactory.createObject(suite, { app: fakeApp })
        var list = panel.resultsList
        verify(list !== null, "the panel exposes no results list")
        compare(list.count, 2, "the docked list does not show both results")
        panel.listFloating = true
        compare(list.count, 2, "the floating list does not show both results")
        panel.destroy()
    }
}
