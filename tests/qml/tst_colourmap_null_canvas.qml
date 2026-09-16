// The colour-map picker, built before there is a canvas.
//
// `required property var canvas` guarantees a value is PASSED. It guarantees
// nothing about that value being an object - and in the Notebook layout it is
// null for the first moments of the window's life, because NotebookCanvas keeps
// `currentCanvas: null` until its first FigureCell completes.
//
// Four bindings read straight through it, and the interface walk in
// --selftest-ui caught all four on one startup: colourMap, colourMapCategories
// twice, and colourMapPreview. Two of them sit on the line after a guarded read
// of the same object.
//
// Asserted as a WARNING rather than a value: when a binding throws, QML leaves
// the property at its previous value, so reading it back afterwards looks
// identical whether the binding ran or blew up. Only the message tells them
// apart - the same reason tst_bindings gives.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "colourmap_null_canvas"
    when: windowShown
    width: 400
    height: 200

    Component {
        id: selectorFactory
        ColourMapSelector { }
    }

    function test_it_survives_being_built_before_the_canvas_exists() {
        failOnWarning(/TypeError/)
        var sel = selectorFactory.createObject(null, { canvas: null,
                                                       width: 300, height: 40 })
        verify(sel !== null, "ColourMapSelector did not instantiate")
        // And it must still say something sensible rather than nothing.
        compare(sel.currentMap, "Viridis",
                "with no canvas the selector does not fall back to a map name")
        sel.destroy()
    }
}
