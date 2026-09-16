// The help browser's navigation, asserted against the real component.
//
// The CONTENT is checked elsewhere and thoroughly: tools/make_help.py refuses
// to write a manual with a duplicate id, an undeclared section, an empty
// field, unbalanced markup or a cross-reference that points nowhere, and the
// passive audit's help_out_of_date check proves the shipped file is that
// validated output. None of that says anything about whether the browser can
// find a topic, which is what this covers.
//
// A fixture rather than the shipped manual, because these are assertions about
// behaviour - "a search for a keyword finds the topic whose keyword it is" -
// and tying them to eighty-six real articles would mean rewriting the test
// every time an article was edited.
import QtQuick
import QtTest
import GraphVis

TestCase {
    id: suite
    name: "help"
    when: windowShown
    width: 400
    height: 300

    // A stand-in for AppController. The browser takes it as a `var` and reads
    // two list properties off it, so a QtObject with those two is
    // indistinguishable from the real thing.
    QtObject {
        id: fakeApp
        readonly property var helpSections: [
            { id: "start", title: "Getting started", blurb: "" },
            { id: "trouble", title: "When something looks wrong", blurb: "" }
        ]
        readonly property var helpTopics: [
            { id: "a", section: "start", kind: "task", title: "First figure",
              keywords: "begin tutorial", body: "Step one.", see_also: ["c"] },
            { id: "b", section: "start", kind: "concept", title: "Engines",
              keywords: "catalogue", body: "An engine is one kind of figure." },
            { id: "c", section: "trouble", kind: "task", title: "Blank figure",
              keywords: "empty nothing", body: "Check the column types." }
        ]
    }

    Component {
        id: browserFactory
        HelpBrowser { app: fakeApp }
    }

    function make() {
        var b = browserFactory.createObject(suite)
        verify(b !== null, "the help browser did not instantiate")
        return b
    }

    function test_topics_arrive() {
        var b = make()
        compare(b.topics.length, 3)
        compare(b.sections.length, 2)
        compare(b.shown.length, 3, "an empty query shows everything")
        b.destroy()
    }

    // The search has to look at the KEYWORDS, not just the title. The whole
    // reason each topic carries them is that people search for the word they
    // have - "empty" - and not the word in the heading.
    function test_search_matches_keywords() {
        var b = make()
        b.query = "nothing"
        compare(b.shown.length, 1)
        compare(b.shown[0].id, "c")
        b.destroy()
    }

    function test_search_matches_body() {
        var b = make()
        b.query = "column types"
        compare(b.shown.length, 1)
        compare(b.shown[0].id, "c")
        b.destroy()
    }

    function test_search_that_matches_nothing_leaves_no_topic() {
        var b = make()
        b.query = "zzzz"
        compare(b.shown.length, 0)
        compare(b.topic, null, "the article pane must have nothing to draw "
                               + "rather than an out-of-range topic")
        b.destroy()
    }

    // goTo CLEARS THE QUERY FIRST, and this is the assertion that matters
    // most in the file.
    //
    // A see-also link points at a topic the current filter may well exclude.
    // Moving to an index within a filtered list that does not contain the
    // target lands the reader on an unrelated article - which is worse than a
    // dead link, because it looks like it worked.
    function test_goto_from_a_filtered_list_lands_on_the_target() {
        var b = make()
        b.query = "begin"                 // shows only "a"
        compare(b.shown.length, 1)
        b.goTo("c", true)
        compare(b.query, "", "the query must be cleared before the jump")
        compare(b.topic.id, "c")
        b.destroy()
    }

    function test_back_returns_to_where_you_were() {
        var b = make()
        b.goTo("b", false)
        compare(b.topic.id, "b")
        b.goTo("c", true)
        compare(b.topic.id, "c")
        b.back()
        compare(b.topic.id, "b")
        compare(b.history.length, 0, "the history must be popped, not just read")
        b.destroy()
    }

    function test_back_on_an_empty_history_does_nothing() {
        var b = make()
        b.back()
        compare(b.topic.id, "a", "Back with nowhere to go must not move")
        b.destroy()
    }

    // A jump to the same topic must not stack a history entry, or Back becomes
    // a no-op that looks broken after a double click.
    function test_goto_self_does_not_grow_the_history() {
        var b = make()
        b.goTo("a", true)
        compare(b.history.length, 0)
        b.destroy()
    }

    function test_first_of_section() {
        var b = make()
        compare(b.firstOfSection("trouble"), "c")
        compare(b.firstOfSection("start"), "a")
        compare(b.firstOfSection("nope"), "",
                "an unknown section must give nothing rather than topic zero")
        b.destroy()
    }

    function test_section_and_topic_titles_resolve() {
        var b = make()
        compare(b.sectionTitle("trouble"), "When something looks wrong")
        compare(b.titleOf("b"), "Engines")
        // An id with no title falls back to the id, so a stale cross-reference
        // shows as something obviously wrong rather than as a blank button.
        compare(b.titleOf("missing"), "missing")
        b.destroy()
    }

    // The contents draws a section heading on the FIRST topic of each section
    // rather than as a row of its own, so that filtering can never leave a
    // heading stranded above no topics. This asserts the rule that produces it.
    function test_section_headings_follow_the_filter() {
        var b = make()
        compare(b.shown[0].section, "start")
        compare(b.shown[1].section, "start", "second topic continues the section")
        compare(b.shown[2].section, "trouble", "third one starts a new heading")

        b.query = "nothing"               // only the trouble topic survives
        compare(b.shown.length, 1)
        compare(b.shown[0].section, "trouble",
                "the surviving topic carries its own heading, so no heading "
                + "can be left with nothing under it")
        b.destroy()
    }
}
