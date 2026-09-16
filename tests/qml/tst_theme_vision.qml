// Which themes a colour-vision mode is allowed to hide.
//
// This is a filter that REMOVES choices from the person using the program, so
// the thing worth testing is not that it hides the right ones - the measurement
// decides that, and tools/measure_theme_cvd.py --check proves the tags match it
// - but that it cannot hide them for the wrong reason. Every assertion below is
// about a way this could empty the picker when nothing is actually wrong.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "theme_vision"
    when: windowShown
    width: 200
    height: 200

    function test_the_catalogue_is_tagged() {
        verify(Theme.themeCount > 50, "expected the full theme catalogue")
        // Every theme must carry a measurement. One that does not would be
        // treated as safe for everything, which is the quiet way an unmeasured
        // theme gets offered to a reader it does not serve.
        for (var i = 0; i < Theme.themeCount; ++i)
            verify(Theme.themeSafeAt(i).length > 0,
                   "theme " + Theme.themeNameAt(i) + " carries no safe tag - "
                   + "run tools/measure_theme_cvd.py --write")
    }

    // No mode set means no filtering. If this ever fails, the picker has
    // started hiding themes from someone who asked for nothing.
    function test_no_mode_hides_nothing() {
        compare(Theme.indicesCarrying("").length, Theme.themeCount)
        for (var i = 0; i < Theme.themeCount; ++i)
            verify(Theme.themeCarries(i, ""))
    }

    // THE FAIL-SAFE. A mode nobody has measured must filter nothing, because
    // every theme lacking the tag would otherwise empty the list completely -
    // a blank picker caused by a missing measurement rather than by any theme
    // being unsuitable.
    function test_an_unmeasured_mode_hides_nothing() {
        verify(!Theme.visionMeasured("tetrachromacy"))
        compare(Theme.indicesCarrying("tetrachromacy").length, Theme.themeCount)
    }

    function test_an_out_of_range_mode_index_is_no_mode() {
        compare(Theme.visionKindAt(0), "", "index 0 is Standard")
        compare(Theme.visionKindAt(99), "", "an unknown index must not filter")
        compare(Theme.visionKindAt(-1), "")
    }

    // The measured result, asserted as a floor rather than an exact number so
    // that adding a theme does not fail this test for no reason. What matters
    // is that each real deficiency leaves SOMETHING to choose.
    function test_every_real_mode_leaves_a_choice() {
        var kinds = ["protanopia", "deuteranopia", "tritanopia", "achromatopsia"]
        for (var k = 0; k < kinds.length; ++k) {
            verify(Theme.visionMeasured(kinds[k]),
                   kinds[k] + " has not been measured")
            var n = Theme.indicesCarrying(kinds[k]).length
            verify(n > 0, "no theme carries " + kinds[k]
                          + " - the picker would be empty")
            verify(n <= Theme.themeCount)
        }
    }

    // Monochrome is the mode that actually discriminates, and the one the
    // whole feature exists for: with hue gone only luminance separates the
    // status colours, and most palettes choose three signals of similar
    // lightness on purpose. If this stops being a strict subset, either the
    // measurement or the tags have drifted.
    function test_monochrome_is_the_mode_that_filters() {
        var mono = Theme.indicesCarrying("achromatopsia").length
        verify(mono < Theme.themeCount,
               "achromatopsia is expected to exclude some themes")
        compare(Theme.indicesCarrying("protanopia").length, Theme.themeCount,
                "every theme was measured as carrying protanopia")
        compare(Theme.indicesCarrying("deuteranopia").length, Theme.themeCount)
        compare(Theme.indicesCarrying("tritanopia").length, Theme.themeCount)
    }

    // A theme built for a deficiency must survive its own filter. One that did
    // not would be offered under a name promising the thing it fails at.
    function test_a_designed_theme_carries_what_it_is_named_for() {
        var checked = 0
        for (var i = 0; i < Theme.themeCount; ++i) {
            var kind = Theme.themeCvdAt(i)
            if (kind === "") continue
            verify(Theme.themeCarries(i, kind),
                   Theme.themeNameAt(i) + " is named for " + kind
                   + " and does not carry it")
            ++checked
        }
        verify(checked >= 4, "expected themes designed for a deficiency")
    }

    // The mode list the picker indexes into has to be the one the program
    // offers. A mode added on the C++ side and not here reads as "" and
    // silently filters nothing.
    function test_vision_kind_list_covers_the_modes() {
        compare(Theme.visionKinds.length, 5)
        compare(Theme.visionKindAt(4), "achromatopsia",
                "Monochrome is achromatopsia in the theme catalogue")
    }
}
