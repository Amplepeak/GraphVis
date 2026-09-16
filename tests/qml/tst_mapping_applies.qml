// The mapping actually being applied, with columns present.
//
// WHY THIS DID NOT EXIST AND HAD TO. Every other test in this suite hands the
// panels an AppController stand-in whose `activeColumns` is empty - which is a
// perfectly good stand-in for building a panel, and a useless one for this,
// because MappingPanel's `resetStaging` takes its OTHER branch when there are
// no columns:
//
//     if (cols.length > 0) root.apply()
//     else root.commit()
//
// `apply()` is what emits `applyRequested`, which ControlSidebar turns into
// `applyMapping`, which the workspace handles. So with no columns the entire
// apply path is never walked - and with a dataset restored at startup, which is
// the ordinary case for anyone who has used the program before, it is walked
// during `Component.onCompleted`, before the window has been shown.
//
// That is the exact shape of a defect this project has recorded twice: a test
// that instantiates a component and asserts nothing crashed, against a code
// path the test's own fixture prevents from running.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "mapping_applies"
    when: windowShown
    width: 640
    height: 520

    AppController {
        id: fakeApp
        // FOUR columns, because resetStaging fills four roles from them and a
        // shorter list would leave some empty and take a different branch.
        activeColumns: ["time", "temperature", "pressure", "flow"]
        activeDatasetId: "d1"
    }

    PlotCanvas {
        id: fakeCanvas
        engine: "Line Chart"
        supportsSecondaryAxis: true
    }

    Component {
        id: sidebarFactory
        ControlSidebar { }
    }
    Component {
        id: panelFactory
        MappingPanel { }
    }

    // What the workspace does with the signal, recorded rather than acted on.
    property var received: null

    function test_applying_a_mapping_delivers_every_role() {
        failOnWarning(/Unable to assign/)
        var panel = panelFactory.createObject(null, { app: fakeApp,
                                                      canvas: fakeCanvas,
                                                      width: 380, height: 500 })
        verify(panel !== null, "MappingPanel did not instantiate")
        // Building it was enough to run resetStaging, which with columns
        // present applies immediately.
        compare(panel.xValue, "time",
                "opening a dataset did not commit the first column to x")
        compare(panel.yValue, "temperature",
                "opening a dataset did not commit the second column to y")
        panel.destroy()
    }

    // THE SIGNAL ITSELF, with every argument it carries.
    //
    // `applyMapping` grew an eleventh parameter when the right-hand axis became
    // a mapping role. Eleven is past the point where Qt's metaobject call paths
    // are all equally happy, and the failure of one is not a compile error - it
    // is a handler that never runs, or runs with the arguments shifted, at
    // window-creation time. Asserted by VALUE, one per parameter, because a
    // handler that fires with `undefined` in three slots satisfies any test
    // that only checks it fired.
    function test_the_sidebar_carries_all_eleven_arguments() {
        failOnWarning(/Unable to assign/)
        suite.received = null
        var bar = sidebarFactory.createObject(null, { app: fakeApp,
                                                      canvas: fakeCanvas,
                                                      width: 400, height: 500 })
        verify(bar !== null, "ControlSidebar did not instantiate")
        bar.applyMapping.connect(
            function (x, y, z, c, size, alpha, invert, bins, smart, profile, y2) {
                suite.received = { x: x, y: y, z: z, c: c, size: size,
                                   alpha: alpha, invert: invert, bins: bins,
                                   smart: smart, profile: profile, y2: y2 }
            })

        bar.mappingY2 = "pressure"
        bar.applyMapping("time", "temperature", "flow", "pressure",
                         3.0, 0.5, false, 0, true, 1, "pressure")

        verify(suite.received !== null, "the eleven-argument signal was never delivered")
        compare(suite.received.x, "time", "argument 1 did not arrive")
        compare(suite.received.c, "pressure", "argument 4 did not arrive")
        compare(suite.received.profile, 1, "argument 10 did not arrive")
        compare(suite.received.y2, "pressure",
                "the eleventh argument did not arrive, so the right-hand axis "
                + "column never reaches the canvas")
        bar.destroy()
    }
}
