// Bindings that have shipped broken, asserted against the real components.
//
// Every fault checked here reached a user, and not one of them is visible in
// the source, to qmllint, or to --selftest-ui: a binding is only evaluated when
// something instantiates the component, and nothing here had ever done that.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "bindings"
    when: windowShown
    width: 420
    height: 320

    // A stand-in for PlotCanvas. The panels take it as a `var`, so a QtObject
    // with the right properties is indistinguishable from the real thing - and
    // using one keeps these tests free of the C++ library, which is what makes
    // them runnable anywhere.
    QtObject {
        id: fakeCanvas
        property string engine: "Line Chart"
        property int columnsMapped: 2
        property int columnsRequired: 2
        property bool renderInFlight: false
        property real renderEstimateSeconds: 0
    }

    Component {
        id: expressionBoxFactory
        ExpressionBox { }
    }

    // UNDEFINED IS NOT FALSE.
    //
    // `readonly property bool parametric: root.canvas && ...` assigns the LEFT
    // operand when it is falsy, so a null canvas puts `null` into a bool. Qt
    // refuses it - "Unable to assign [undefined] to bool", 24 times in one
    // session for the sibling of this binding in GraphLibrary.
    //
    // ASSERTED AS A WARNING, NOT AS A VALUE, and that distinction is the whole
    // reason this test is worth anything. The first version compared
    // typeof parametric against "boolean" and PASSED against the broken
    // binding: when Qt refuses an assignment it logs and leaves the property
    // at its default, so reading it back gives `false` either way. The value
    // is identical in both worlds; only the warning tells them apart.
    // UNDEFINED, not null, and that distinction was measured rather than
    // assumed: a probe showed `null && x` assigns silently and only
    // `undefined && x` produces the warning. The first version of this test
    // passed `null` and therefore passed against the broken binding too.
    //
    // undefined is also what really happens: the GraphLibrary delegate reads
    // `entry.modelData.entry` before the model row exists, which is undefined,
    // not null.
    function test_bool_binding_survives_a_canvas_that_is_not_there_yet() {
        failOnWarning(/Unable to assign/)
        var box = expressionBoxFactory.createObject(suite, { canvas: undefined })
        verify(box !== null, "ExpressionBox did not instantiate")
        compare(box.parametric, false)
        box.destroy()
    }

    function test_bool_binding_is_true_for_the_engine_it_names() {
        var box = expressionBoxFactory.createObject(
            suite, { canvas: fakeCanvas })
        fakeCanvas.engine = "Function 3D Parametric"
        compare(box.parametric, true, "parametric must be true for its own engine")
        fakeCanvas.engine = "Line Chart"
        compare(box.parametric, false, "and false for anything else")
        box.destroy()
    }
}
