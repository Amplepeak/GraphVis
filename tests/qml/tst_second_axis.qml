// The right-hand y axis, as a mapping role.
//
// The secondary-axis machinery had been complete for a long time - its own
// range, ticks, log scale, label and per-series placement - and eight engines
// used it internally. What no part of the interface could do was the ordinary
// thing: say "temperature on the left, pressure on the right" about two columns
// of your own data. The feature was finished and unreachable.
//
// Two claims are made by the code that makes it reachable, and neither is
// visible to anything else this project runs. The engine sweep never loads QML;
// the linter cannot evaluate a `visible` binding; and --selftest-ui settles the
// window without touching a control. So:
//
//   1. the row appears only where the engine will honour it, and
//   2. choosing a column stages it, and "none" takes it back off.
//
// The first is the one that matters. A control offered where it does nothing is
// this project's stated worst case for an interface - "a control that cannot
// affect the figure should not be offered for it" - and the answer behind that
// binding is measured in C++ rather than listed, so what this asserts is that
// the measurement actually reaches the row.
//
// THE STAND-INS ARE THE GENERATED ONES. A hand-written QtObject carrying the
// three properties this panel reads directly was the first version of this
// test, and it drowned in "Unable to assign [undefined]" from AxisScaleBar,
// RangePanel and ExpressionBox, which MappingPanel embeds. Those warnings are
// what failOnWarning exists to catch, so a test that produces its own is a test
// that cannot use it - see the note above stage_native_stand_ins.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "second_axis"
    when: windowShown
    width: 420
    height: 640

    AppController {
        id: fakeApp
        activeColumns: ["time", "temperature", "pressure", "flow"]
    }

    // Two canvases, differing only in the answer that decides the row.
    PlotCanvas {
        id: honouringCanvas
        engine: "Line Chart"
        supportsSecondaryAxis: true
    }
    PlotCanvas {
        id: ignoringCanvas
        engine: "Pareto Chart"
        supportsSecondaryAxis: false
    }

    Component {
        id: panelFactory
        MappingPanel { }
    }

    // Rows live several layouts down inside a scroll view, so the search is a
    // walk rather than a lookup. `data` rather than `children`: a Repeater's
    // delegates are children of its parent, and non-visual items hang off
    // `data` alone.
    function findByName(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var kids = item.data ? item.data : []
        for (var i = 0; i < kids.length; ++i) {
            var found = findByName(kids[i], name)
            if (found) return found
        }
        return null
    }

    function test_the_row_appears_only_where_the_engine_honours_it() {
        failOnWarning(/Unable to assign/)
        var panel = panelFactory.createObject(null, { app: fakeApp,
                                                      canvas: honouringCanvas,
                                                      width: 380, height: 600 })
        verify(panel !== null, "MappingPanel did not instantiate")

        var row = suite.findByName(panel, "mapRow_y2")
        verify(row !== null, "the right-hand Y axis row was not built at all")
        verify(row.visible,
               "the engine honours a secondary axis and the row is hidden, so "
               + "the feature is still unreachable")
        // The roles that are not optional are there whatever the engine says.
        verify(suite.findByName(panel, "mapRow_x").visible, "the X row is hidden")
        verify(suite.findByName(panel, "mapRow_y").visible, "the Y row is hidden")

        panel.canvas = ignoringCanvas
        verify(!row.visible,
               "the engine rebuilds its own series and ignores the flag, and "
               + "the row is offered anyway - a control that cannot affect the "
               + "figure")
        verify(suite.findByName(panel, "mapRow_x").visible,
               "hiding the optional row hid the mandatory ones too")

        panel.destroy()
    }

    function test_a_column_stages_and_none_takes_it_back_off() {
        failOnWarning(/Unable to assign/)
        var panel = panelFactory.createObject(null, { app: fakeApp,
                                                      canvas: honouringCanvas,
                                                      width: 380, height: 600 })
        verify(panel !== null, "MappingPanel did not instantiate")

        // A NEW DATASET DOES NOT PUT A COLUMN ON THE SECOND AXIS.
        //
        // The other four roles are filled from the first four columns, because
        // a figure cannot draw without them. This one is a claim about the data
        // - two quantities, two scales - and filling it automatically would
        // make every freshly opened dataset draw a two-axis figure nobody asked
        // for.
        compare(panel.y2Value, "",
                "opening a dataset put a column on the right-hand axis by itself")

        panel.stage("y2", "pressure")
        compare(panel.y2Value, "pressure",
                "choosing a column did not reach the committed mapping")

        // And back off again. Without a way to clear it, choosing a column
        // would be a decision the person could not undo.
        panel.stage("y2", "")
        compare(panel.y2Value, "",
                "clearing the right-hand axis left the column on it")

        panel.destroy()
    }
}
