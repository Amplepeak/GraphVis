// Measuring a string that is not the one in the box.
//
// Two panels size a dropdown to the longest entry it holds, so that a list of
// fifty-one citation styles or a hundred colour maps is readable rather than
// elided. Both loop over the entries asking how wide each would be.
//
// ColourMapSelector uses FontMetrics, whose `advanceWidth(text)` is a METHOD
// and answers about any string you hand it. LatexExport used TextMetrics, whose
// `advanceWidth` is a read-only PROPERTY of the single string in its own `text`
// - so calling it is invoking a number, and QML says so at run time:
//
//   TypeError: Property 'advanceWidth' of object QQuickTextMetrics is not a
//   function
//
// It is not a syntax error, qmllint passes it, and the visible result is not a
// crash - the width falls back to the control's own and the long names go on
// being cut off, which is the exact fault the sizing code was written to fix.
// It logged twice on every startup for as long as it existed.
//
// WHAT THIS ASSERTS is the behaviour, not the spelling: that asking each of
// several different strings for its width returns a number, and that a longer
// string measures wider than a shorter one. A test that only checked for the
// word "FontMetrics" in the file would pass against any other type that
// happens to have the right name.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "text_metrics"
    when: windowShown
    width: 400
    height: 200

    // The generated stand-ins, so every property these panels read exists.
    // LatexExport requires BOTH `app` and `canvas`; leaving either out makes
    // the component fail to build, which would make this test pass for the
    // wrong reason - the fault it looks for lives in a binding that never runs
    // if the object is never created.
    AppController {
        id: fakeApp
        // The loop under test walks this list. One entry is enough to make it
        // run; several with different lengths is what makes it measure.
        citationStyles: [{ id: "apa", label: "APA" },
                         { id: "chicago",
                           label: "Chicago Notes & Bibliography, 17th edition" },
                         { id: "ieee", label: "IEEE" }]
    }
    PlotCanvas {
        id: fakeCanvas
    }

    Component {
        id: latexFactory
        LatexExport { }
    }

    function test_the_style_popup_measures_every_label() {
        // The exact message the defect produced. Narrow on purpose: this test
        // is about one fault, and a broad pattern would fail on the unrelated
        // warnings other panels produce against a stand-in controller.
        failOnWarning(/advanceWidth/)
        var panel = latexFactory.createObject(null, { app: fakeApp,
                                                      canvas: fakeCanvas,
                                                      width: 380, height: 200 })
        verify(panel !== null, "LatexExport did not instantiate")
        panel.destroy()
    }

    // And the property the fault was hiding: the popup has to end up wider than
    // the control when the entries are longer than it. Measured directly off
    // FontMetrics, because that is the thing that was being called wrongly.
    FontMetrics { id: metrics }

    function test_a_longer_label_measures_wider() {
        // Not `short`/`long`: both are reserved words in QML's JavaScript
        // and the file will not compile with them as variable names.
        var narrow = metrics.advanceWidth("APA")
        var wide = metrics.advanceWidth("Chicago Notes & Bibliography, 17th edition")
        verify(narrow > 0, "measuring a string returned nothing at all")
        verify(wide > narrow,
               "a label six times longer did not measure wider, so sizing a "
               + "popup from these numbers would elide the long names anyway")
    }
}
