// Qt 6 delegate scoping. Without this, an id from the enclosing file is not
// legally visible inside a delegate or an inline Component - it resolves only
// because the old unbound context lookup walks out of the component, which is
// slower at every evaluation and silently breaks the moment a model role
// shares a name. Bound resolves ids at compile time; delegates declare what
// they take from the model with `required property`.
pragma ComponentBehavior: Bound
// Graph Library — the GraphVis 17 catalogue, native.
//
// 2,116 entries across 46 categories come from app.graphCategories, which
// AppController loads from the embedded config/graph_catalogue.json. Search
// uses app.searchGraphs(), a port of graph_library.search_entries(). Hovering
// an entry shows its real pre-rendered GraphVis thumbnail from
// share/graphvis/assets/graph_previews_compact.
//
// Selecting an entry stages it; it is not rendered until Apply is pressed,
// matching GraphVis 17's deliberate-apply behaviour.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var app

    property var staged: null
    signal applyRequested(var entry)
    // A scan recommendation carries both the graph and its axis mapping.
    signal scanApplied(string graph, var mappings)

    color: Theme.background

    // Rows are a plain JavaScript array, not a ListModel.
    //
    // ListModel builds its roles from the first appended object, and a header
    // row has no catalogue entry. Appending `entry: null` made Qt log
    //   "entry is null. Adding an object with a null member does not create a
    //    role for it"
    // once per row - 36 times per refresh in the field log - and left the
    // `entry` role undefined for the rows that followed. A JS array model has
    // no roles at all, so the problem cannot occur.
    property var rows: []

    // Which categories are open. 2,116 graphs in 46 categories is a list nobody
    // scrolls through - the one they want is forty screens down and they do not
    // know which heading it is under. Collapsed by default, so the first thing
    // on screen is the 46 headings rather than the first 12 of 2,116 entries.
    //
    // A search is different: typing a query means the person is looking for a
    // named thing, so every group opens and refresh() below skips headers
    // entirely on a query.
    property var openCategories: ({})
    function isOpen(name){ return root.openCategories[name] === true }
    function toggleCategory(name){
        var next = {}
        for (var k in root.openCategories) next[k] = root.openCategories[k]
        next[name] = !next[name]
        root.openCategories = next
        root.refresh()
    }
    function setAllCategories(open){
        var next = {}
        for (var c = 0; c < root.app.graphCategories.length; ++c)
            next[root.app.graphCategories[c].name] = open
        root.openCategories = next
        root.refresh()
    }

    function refresh() {
        var out = []
        var q = searchField.text.trim()

        if (q.length === 0 && !advancedToggle.checked) {
            // Favourites and Recently used, above the catalogue.
            //
            // 2,116 entries in 46 categories is a catalogue nobody browses
            // twice: the person who used a Nyquist plot yesterday should not
            // have to remember which of forty-six headings it is filed under.
            // These two groups are that shortcut and they come first, because
            // a shortcut below the thing it is a shortcut for is not one.
            //
            // Only on the tree. A search is already a shortcut, and repeating
            // a starred graph at the top of its own results would be the same
            // row twice.
            var shortcuts = [
                { name: "★ Favourites", entries: root.app.favouriteGraphs,
                  empty: "Star a graph to keep it here" },
                { name: "Recently used", entries: root.app.recentGraphs,
                  empty: "" }
            ]
            for (var sc = 0; sc < shortcuts.length; ++sc) {
                var group = shortcuts[sc]
                // An empty Recently used group is not shown at all - there is
                // nothing to say. An empty Favourites group IS shown once, with
                // the line that explains the star, because otherwise the
                // feature has no discoverable starting point.
                if (group.entries.length === 0 && group.empty === "") continue
                var scOpen = root.isOpen(group.name)
                out.push({ header: true, label: group.name,
                           count: group.entries.length,
                           open: scOpen, entry: undefined })
                if (!scOpen) continue
                if (group.entries.length === 0) {
                    out.push({ header: false, label: group.empty, count: 0,
                               open: false, entry: undefined, hint: true })
                    continue
                }
                for (var si = 0; si < group.entries.length; ++si)
                    out.push({ header: false, label: group.entries[si].engine,
                               count: 0, open: false, entry: group.entries[si] })
            }

            // No query: the category tree, non-advanced entries only.
            for (var c = 0; c < app.graphCategories.length; ++c) {
                var cat = app.graphCategories[c]
                var kept = []
                for (var i = 0; i < cat.entries.length; ++i)
                    if (!cat.entries[i].advanced) kept.push(cat.entries[i])
                if (kept.length === 0) continue
                var open = root.isOpen(cat.name)
                out.push({ header: true, label: cat.name, count: kept.length,
                           open: open, entry: undefined })
                if (!open) continue
                for (var k = 0; k < kept.length; ++k)
                    out.push({ header: false, label: kept[k].engine, count: 0,
                               open: false, entry: kept[k] })
            }
            rows = out
            return
        }

        // No cap, rather than a large one.
        //
        // This was 400, set when the catalogue held 433 entries and meaning
        // "all of them". The catalogue grew to 1,769 and then to 2,116, and a
        // cap of 400 silently truncated the answer - the entries it dropped
        // being the ones the fuzzy score ranked last, which is exactly where a
        // half-remembered name lands. Raising it to 2,000 fixed that day's
        // symptom and set up the next one: 2,000 is under 2,116, so the same
        // silent truncation was already waiting on a broad query.
        //
        // A number that has to be revised every time the catalogue grows is a
        // bug on a timer. searchGraphs treats a limit of 0 as no limit, and
        // nothing downstream needs one: the list is grouped by engine and the
        // view is virtualised, so the cost is the sort, which is over the
        // whole catalogue whatever this says.
        var results = app.searchGraphs(q, advancedToggle.checked, 0)

        // Results GROUPED BY ENGINE, base entry first.
        //
        // 1,503 of the catalogue's 2,116 entries are axis-scale variants - the
        // same engine again with a log, log(x+1), z-score or quantile axis - so
        // an ungrouped search for "line" returned Line Chart's five variants
        // interleaved with five other engines' variants by fuzzy score, and the
        // list read as forty near-identical rows rather than eight engines. The
        // variants are worth having and worth searching; they are not worth
        // ranking against their own parent.
        //
        // Within an engine the base entry comes first and the variants follow
        // it, and the engine keeps the best rank any of its entries earned - so
        // an exact match on a variant still brings its engine to the top.
        var order = []
        var groups = {}
        for (var r = 0; r < results.length; ++r) {
            var e = results[r]
            var key = e.category + "\u0000" + e.engine
            if (groups[key] === undefined) {
                // BASES is a list, not one entry. 33 engines have more than one
                // catalogue entry in the same category - different names and
                // descriptions for the same drawing code, which is how the
                // catalogue has always been - and keeping a single `base` per
                // group silently dropped all but the last of them: a search for
                // "line" returned 372 entries and drew 361 rows in the
                // catalogue as it stood when this was found.
                groups[key] = { bases: [], variants: [] }
                order.push(key)
            }
            if (e.scale) groups[key].variants.push(e)
            else groups[key].bases.push(e)
        }

        var lastCat = ""
        for (var g = 0; g < order.length; ++g) {
            var group = groups[order[g]]
            var leaders = group.bases
            // A group whose base entries are all filtered out - the query
            // matched only a variant - is led by that variant rather than by
            // nothing.
            if (leaders.length === 0) leaders = [group.variants.shift()]
            if (!leaders[0]) continue
            if (q.length === 0 && leaders[0].category !== lastCat) {
                out.push({ header: true, label: leaders[0].category, count: 0,
                           open: true, entry: undefined })
                lastCat = leaders[0].category
            }
            for (var L = 0; L < leaders.length; ++L)
                out.push({ header: false, label: leaders[L].engine,
                           // The variant count sits on the LAST leader, which
                           // is the row the variants actually follow.
                           count: (L === leaders.length - 1) ? group.variants.length : 0,
                           open: false, entry: leaders[L] })
            for (var v = 0; v < group.variants.length; ++v)
                out.push({ header: false, label: group.variants[v].engine, count: 0,
                           open: false, entry: group.variants[v] })
        }
        rows = out
    }

    Component.onCompleted: {
        // Both shortcut groups open on first run, so the star is discoverable
        // without anyone having to expand anything to find out it exists.
        if (Object.keys(root.openCategories).length === 0) {
            var first = {}
            first["★ Favourites"] = true
            first["Recently used"] = true
            root.openCategories = first
        }
        refresh()
    }

    // Starring a graph, or applying one, changes what the two groups above
    // hold. `rows` is a snapshot, so without this the list goes on showing the
    // state it was built from and the star appears to do nothing.
    //
    // The counter is not decoration. Each star's state comes from
    // app.isFavouriteGraph(), an ordinary invokable with no notify signal, and
    // a QML binding over a function call is evaluated once and never again -
    // there is nothing for the engine to watch. Naming this property inside
    // that binding gives it something, so bumping it here re-evaluates every
    // star on screen. Rebuilding `rows` alone was not enough: the view reuses
    // its delegates, so a row that stayed put kept its stale glyph.
    property int favouritesRevision: 0
    Connections {
        target: root.app
        function onGraphShortcutsChanged() {
            root.favouritesRevision = root.favouritesRevision + 1
            root.refresh()
        }
    }

    // Switching a pack off rebuilds app.graphCategories underneath us. `rows`
    // is a snapshot taken by refresh(), not a live view of the catalogue, so
    // without this the list would go on showing categories the controller no
    // longer holds - and clicking one would stage an entry that is not there.
    Connections {
        target: root.app
        function onGraphCatalogueChanged() { root.refresh() }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8; spacing: 6

        // One row of chrome where there were two.
        //
        // Every row above the list is a row of the list you cannot see, and in
        // a short sidebar that arithmetic decides whether the panel is usable
        // at all: the count, the Packs button, the word "Categories" and the
        // two expand buttons were 50 px of a 180 px panel. The heading said
        // "GRAPH LIBRARY" directly underneath a panel header already saying
        // "Graph Library", and "Categories" labelled a list that is visibly a
        // list of categories - so both were spending height to say what was
        // already on screen.
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            // The count gives way rather than pushing the buttons off the end:
            // a button shoved past the edge cannot be clicked, and a number
            // that has been elided can still be read from what is left of it.
            // Without this the row's minimum width was 274 px in a 264 px
            // column, and a row that cannot fit is laid out past the edge - the
            // same overflow as the vertical one, sideways.
            Label {
                text: root.app.graphEntryCount + " graphs"
                color: Theme.textMuted
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            // Only meaningful on the category tree; a search has already opened
            // everything it matched.
            ToolButton {
                text: "Expand"
                font.pixelSize: 11
                flat: true
                visible: searchField.text.trim().length === 0
                ToolTip.visible: hovered
                ToolTip.text: "Open every category"
                onClicked: root.setAllCategories(true)
            }
            ToolButton {
                text: "Collapse"
                font.pixelSize: 11
                flat: true
                visible: searchField.text.trim().length === 0
                ToolTip.visible: hovered
                ToolTip.text: "Close every category"
                onClicked: root.setAllCategories(false)
            }
            // The packs. Every engine is compiled in either way - a pack is a
            // filter over the library, not a download - so this is a button
            // that shortens a list, and it says so rather than saying
            // "Install".
            ToolButton {
                id: packButton
                text: "Packs"
                font.pixelSize: 11
                flat: true
                ToolTip.visible: packButton.hovered
                ToolTip.text: "Choose which subject areas appear in this list. "
                              + "Nothing is downloaded or removed; the graphs you "
                              + "switch off are still there when you switch them "
                              + "back on."
                onClicked: packMenu.open()

                Menu {
                    id: packMenu
                    y: packButton.height
                    Repeater {
                        model: root.app.graphPacks
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.name + "  (" + modelData.entryCount + ")"
                            checkable: true
                            checked: modelData.enabled
                            // Core carries the ordinary chart types. Switching
                            // it off would leave a plotting program that cannot
                            // draw a line, so it is shown and fixed on.
                            enabled: modelData.id !== "base"
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.description
                            onTriggered: root.app.setPackEnabled(modelData.id, !modelData.enabled)
                        }
                    }
                }
            }
        }

        // Which graphs suit THIS data, above the 433 that might suit anything.
        //
        // This was at the bottom of the list, collapsed, behind a checkbox
        // called "Smart Suite" - a feature name, not a description of what it
        // does - so the one control that answers "which of these should I be
        // using" was the hardest thing in the panel to find. It is the first
        // thing now, it says what it does, and the results land at the top of
        // the list where they can be clicked.
        Button {
            id: bestButton
            Layout.fillWidth: true
            enabled: root.app.activeArrowPath !== ""
            highlighted: true
            // The caret says this OPENS AND CLOSES. Without it the button reads
            // as an action - press it and something happens - so the block it
            // opens looked permanent, and the block is tall enough to leave
            // three or four rows of the catalogue on screen underneath it. The
            // one control that puts the list back was the button that had
            // already been pressed.
            text: (scanOpen.checked ? "▾  " : "▸  ")
                  + (root.app.scanning
                     ? "Finding the best graphs…"
                     : (root.app.scanRecommendations.length > 0
                        ? "★  Best for this data  (" + root.app.scanRecommendations.length + " found)"
                        : "★  Best for this data"))
            ToolTip.visible: bestButton.hovered
            ToolTip.text: root.app.activeArrowPath !== ""
                          ? "Reads the loaded data - and the open paper, if there is one - "
                            + "and ranks the graphs that suit it, each with the axis mapping "
                            + "it should use. Click one to draw it. "
                            + (scanOpen.checked
                               ? "Click here to fold this away and get the list back."
                               : "")
                          : "Import a dataset first."
            onClicked: scanOpen.checked = !scanOpen.checked
        }

        ScanPanel {
            id: scan
            Layout.fillWidth: true
            // Never more than two fifths of the panel. 320 was a fixed height
            // chosen against a tall window; in a short one it took everything
            // the catalogue had, and the list this panel exists for was four
            // rows at the bottom. The scan's own list scrolls inside whatever
            // it gets, so a smaller share costs a row of results rather than
            // hiding anything.
            // Sized from what the panel actually needs, not from a guess.
            //
            // The floor was 180. Measured offscreen, ScanPanel's own minimum is
            // 194 in a 360 px sidebar - the summary line wraps to two there -
            // so the floor was below the minimum and the block overflowed its
            // allowance and painted over the search field beneath it. It also
            // meant the results list only ever got its 70 px floor: one card,
            // however many the scan had found. Three found, one visible.
            //
            // Now the ask is the chrome plus room for up to three results, and
            // the share rises only while the section is OPEN - which is exactly
            // when the person is looking at it, and the case the old comment's
            // worry about starving the catalogue did not cover.
            // Both constants measured offscreen rather than estimated: the
            // chrome above the list is 141, and a recommendation card is 100 -
            // taller than it looks, because the reason wraps to two lines at
            // sidebar widths. Three cards are 308 px of content. My first guess
            // was 68 and would have shown two.
            // 141 since the Go button was added; 124 before it. Re-measured
            // rather than left alone, because a chrome constant that no longer
            // matches the panel is how the overflow this fixed comes back.
            readonly property int scanChrome: 141
            readonly property int scanCard: 100
            readonly property int scanWanted:
                scanChrome + Math.max(70, scanCard * Math.min(3, root.app.scanRecommendations.length))
            Layout.preferredHeight: scanOpen.checked
                                    ? Math.min(scanWanted, Math.max(220, root.height * 0.55))
                                    : 0
            Layout.maximumHeight: scanOpen.checked ? Math.max(220, root.height * 0.6) : 0
            visible: scanOpen.checked
            app: root.app
            onRecommendationChosen: (graph, mappings) => root.scanApplied(graph, mappings)
        }

        // Not shown; it is the open/closed state the button above toggles, kept
        // as a CheckBox so nothing else in this file had to change.
        CheckBox { id: scanOpen; visible: false; checked: false }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 1
            color: Theme.border
        }

        // Search and the advanced switch on ONE row. Every row of chrome above
        // the list is a row of the list you cannot see, and in a 400 px sidebar
        // holding a seventeen-hundred-entry catalogue that arithmetic decides
        // whether the panel is usable: four visible entries is a list you
        // cannot browse.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "Search graphs…"
                onTextChanged: root.refresh()
            }
            ToolButton {
                id: advancedButton
                text: "adv"
                font.pixelSize: 11
                flat: true
                checkable: true
                checked: advancedToggle.checked
                onToggled: { advancedToggle.checked = advancedButton.checked; root.refresh() }
                ToolTip.visible: advancedButton.hovered
                // Says what they ARE. "Advanced" told nobody that the entries
                // behind it are mostly the same engines again on a transformed
                // axis - which is a thing the Axis scale controls already do to
                // whatever is on screen. Not ONLY axis scales, though: the
                // scale field carries the variant dimension an engine has, so
                // on the map family it is the projection and on the spectral
                // family it is the FFT window.
                ToolTip.text: "Also list the " + root.app.advancedEntryCount
                              + " variants: the same engines again on a log, "
                              + "log(x+1), z-score or quantile axis — and, where "
                              + "an engine has one of its own, its projection or "
                              + "window."
            }
        }
        // Not shown; the state the button above carries, kept as a CheckBox so
        // refresh() did not have to change.
        CheckBox { id: advancedToggle; visible: false; checked: false }

        // Why a search came up short. Without this line a switched-off pack is
        // indistinguishable from a graph that was never written: someone
        // searches "Wind Rose", gets nothing, and concludes the program cannot
        // draw one.
        RowLayout {
            Layout.fillWidth: true
            visible: root.app.hiddenEntryCount > 0
            spacing: 6
            Label {
                text: root.app.hiddenEntryCount + " graphs hidden by packs"
                color: Theme.textMuted
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ToolButton {
                text: "Show all"
                font.pixelSize: 11
                flat: true
                onClicked: {
                    var packs = root.app.graphPacks
                    for (var p = 0; p < packs.length; ++p)
                        if (!packs[p].enabled) root.app.setPackEnabled(packs[p].id, true)
                }
            }
        }

        // The Categories row that used to be here is now part of the one at the
        // top of this panel.

        ListView {
            id: view
            Layout.fillWidth: true; Layout.fillHeight: true
            // NO floor.
            //
            // This was 120 - "a floor, so the chrome above cannot squeeze the
            // list out of existence" - and the reasoning was right about what
            // the list needs and wrong about what a floor does. fillHeight
            // already hands this every pixel the chrome does not take, so the
            // floor changed nothing in a panel with room. In a panel WITHOUT
            // room it made the column's minimum height larger than the panel it
            // had to fit in, and a column that cannot fit does not shrink: it
            // lays its children out past its own bottom edge, where they paint
            // over whatever is below. That is the overlap that was reported.
            // Squeezed, the list is now short and still scrolls; the way to get
            // rows back is to drag the divider above it, which is now wide
            // enough to find.
            Layout.minimumHeight: 0
            clip: true
            model: root.rows
            currentIndex: -1
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: entry
                required property var modelData
                width: view.width
                height: entry.modelData.header ? 26 : 34

                // The heading is the control that opens the group, so the
                // whole row is the target rather than a small triangle.
                Rectangle {
                    visible: entry.modelData.header
                    anchors.fill: parent
                    anchors.rightMargin: 4
                    radius: Theme.radius
                    color: headerHover.hovered ? Theme.surface : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 2
                        spacing: 6
                        Label {
                            // A right-pointing triangle when shut, down when
                            // open. The same convention as every file tree.
                            text: entry.modelData.open ? "\u25be" : "\u25b8"
                            color: Theme.textMuted
                            font.pixelSize: 10
                        }
                        Label {
                            text: entry.modelData.label
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            visible: entry.modelData.count > 0
                            text: entry.modelData.count
                            color: Theme.textMuted
                            font.pixelSize: 10
                        }
                    }

                    HoverHandler { id: headerHover }
                    TapHandler {
                        onTapped: root.toggleCategory(entry.modelData.label)
                    }
                }

                // The one line under an empty Favourites group. Not a
                // catalogue row: it has no entry, so it must not be staged, and
                // it must not offer a star for a graph that is not there.
                Label {
                    visible: !entry.modelData.header && entry.modelData.hint === true
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    verticalAlignment: Text.AlignVCenter
                    text: entry.modelData.label
                    color: Theme.textMuted
                    font.pixelSize: 10
                    font.italic: true
                    elide: Text.ElideRight
                }

                Rectangle {
                    visible: !entry.modelData.header && entry.modelData.hint !== true
                    anchors.fill: parent; anchors.rightMargin: 4
                    radius: 5
                    color: root.staged && entry.modelData.entry && root.staged.engine === entry.modelData.entry.engine
                           && root.staged.category === entry.modelData.entry.category
                           ? Theme.surfaceAlt : (hover.hovered ? Theme.surfaceAlt : "transparent")

                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 8

                        // A variant sits under its engine rather than beside
                        // it, so a group reads as one thing with options.
                        Item {
                            Layout.preferredWidth: (entry.modelData.entry
                                                    && entry.modelData.entry.scale) ? 14 : 0
                            Layout.preferredHeight: 1
                        }
                        Image {
                            Layout.preferredWidth: 46; Layout.preferredHeight: 28
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true; cache: true
                            source: entry.modelData.entry ? root.app.graphThumbnail(entry.modelData.entry.thumbnail, true) : ""
                            visible: status === Image.Ready
                        }
                        ColumnLayout {
                            id: entryText
                            Layout.fillWidth: true; spacing: 0
                            // AN ENGINE NOBODY HAS MEASURED SAYS SO.
                            //
                            // The catalogue carries a `verified` flag written
                            // by tools/mark_verified_engines.py from the
                            // evidence - a measured check in PlotSelfTest.cpp,
                            // or membership of the audited baseline - and never
                            // by hand; a flag that disagrees with the evidence
                            // fails a test.
                            //
                            // Shown rather than hidden. An engine that draws
                            // and has not been checked is still worth offering
                            // to somebody who wants to look at it; what would
                            // be wrong is offering it in the same voice as the
                            // 434 that went through the audit. So: the name in
                            // red, and the word said out loud where the
                            // description goes, which is the line a person
                            // reads before clicking.
                            //
                            // `=== false` rather than `!== true`: an older
                            // catalogue with no flag at all must not paint the
                            // whole library red.
                            //
                            // WRAPPED IN !!( ), and that is not decoration.
                            //
                            // `a && b` in JavaScript evaluates to A when A is
                            // falsy, not to false - so when this delegate is
                            // built before its entry exists, which happens on
                            // every rebuild of the list, the expression is
                            // `undefined` and QML refuses to assign it to a
                            // bool: "Unable to assign [undefined] to bool",
                            // twenty-four times in one session.
                            //
                            // Caught by the first real run of the build report
                            // on the machine this shipped to, in QML I had
                            // written the day before. qmllint passes it and
                            // --selftest-ui does not reach it, because the
                            // delegate is only built once the library is
                            // populated.
                            readonly property bool unverified:
                                !!(entry.modelData.entry
                                   && entry.modelData.entry.verified === false)
                            RowLayout {
                                Layout.fillWidth: true; spacing: 5
                                Label {
                                    text: entry.modelData.label + (entry.modelData.entry && entry.modelData.entry.scale ? " · " + entry.modelData.entry.scale : "")
                                    color: entryText.unverified ? Theme.danger : Theme.text
                                    font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
                                }
                                Rectangle {
                                    visible: entryText.unverified
                                    Layout.preferredHeight: tag.implicitHeight + 2
                                    Layout.preferredWidth: tag.implicitWidth + 8
                                    radius: 2
                                    color: "transparent"
                                    border.color: Theme.danger
                                    border.width: 1
                                    Label {
                                        id: tag
                                        anchors.centerIn: parent
                                        text: "unverified"
                                        color: Theme.danger
                                        font.pixelSize: 9
                                    }
                                }
                            }
                            Label {
                                text: entryText.unverified
                                      ? "Unverified — nothing measures this engine yet"
                                      : (entry.modelData.entry ? entry.modelData.entry.description : "")
                                color: entryText.unverified ? Theme.danger : Theme.textMuted
                                font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                        }
                        Label {
                            // How many axis-scale variants follow this row, on
                            // the engine's own line. "adv" on a variant said
                            // only that it was hidden by default, which is the
                            // least interesting thing about it.
                            text: entry.modelData.count > 0
                                  ? "+" + entry.modelData.count + " more"
                                  : (entry.modelData.entry && entry.modelData.entry.scale
                                     ? "\u21b3" : "")
                            color: Theme.textMuted; font.pixelSize: 9
                        }

                        // The star.
                        //
                        // Three things had to be right and two of them were
                        // not, which is why the first version rendered and did
                        // nothing when clicked:
                        //
                        // 1. `starred` calls isFavouriteGraph(), a plain
                        //    invokable with no notify signal behind it. QML
                        //    evaluates a binding on a function call ONCE and
                        //    has no way to know it should ever re-run, so the
                        //    glyph never changed however many times the
                        //    favourites changed underneath it. Naming
                        //    root.favouritesRevision in the binding is what
                        //    gives QML something it can watch.
                        //
                        // 2. `visible: false` means NO INPUT. The star was
                        //    hidden until the row was hovered, and hit-testing
                        //    a control whose existence depends on the pointer
                        //    being in the right place is a race with itself.
                        //    It is always present now and fades with opacity,
                        //    which looks the same and is always clickable.
                        //
                        // 3. The row has its own tap handler that stages the
                        //    graph. The star must consume the click rather
                        //    than merely also handling it, or starring
                        //    something replaces what the person was looking
                        //    at. A MouseArea above the row in z does consume
                        //    it; a TapHandler shares the press.
                        Item {
                            id: star
                            property var pick: entry.modelData.entry
                            property bool starred: star.pick && root.favouritesRevision >= 0
                                ? root.app.isFavouriteGraph(star.pick.category,
                                                            star.pick.engine,
                                                            star.pick.scale ? star.pick.scale : "")
                                : false
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            z: 2
                            opacity: star.pick === undefined ? 0.0
                                     : (star.starred || hover.hovered || starMouse.containsMouse
                                        ? 1.0 : 0.0)
                            Behavior on opacity { NumberAnimation { duration: 90 } }

                            Label {
                                anchors.centerIn: parent
                                text: star.starred ? "\u2605" : "\u2606"
                                color: star.starred ? Theme.accent
                                                    : (starMouse.containsMouse ? Theme.text
                                                                               : Theme.textMuted)
                                font.pixelSize: 13
                            }

                            MouseArea {
                                id: starMouse
                                anchors.fill: parent
                                anchors.margins: -3      // a 24px target, not 13
                                enabled: star.pick !== undefined
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton
                                ToolTip.visible: starMouse.containsMouse && star.pick !== undefined
                                ToolTip.text: star.starred ? "Remove from favourites"
                                                           : "Add to favourites"
                                onClicked: function(mouse) {
                                    mouse.accepted = true
                                    root.app.toggleFavouriteGraph(
                                        star.pick.category,
                                        star.pick.engine,
                                        star.pick.scale ? star.pick.scale : "")
                                }
                            }
                        }
                    }

                    HoverHandler { id: hover }
                    TapHandler { onTapped: { if (entry.modelData.entry) { root.staged = entry.modelData.entry; root.app.notify("Staged: " + entry.modelData.entry.engine) } } }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        // One line, not two. The category was on its own row under the engine
        // name, which cost a row of the list to repeat something the list above
        // was already showing.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: root.staged
                      ? root.staged.engine + "  ·  " + root.staged.category
                      : "Pick a graph, then Apply"
                color: root.staged ? Theme.text : Theme.textMuted
                font.pixelSize: 11
                font.bold: root.staged !== null
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Button {
                text: "▶"
                implicitHeight: 26
                ToolTip.visible: hovered
                ToolTip.text: "Apply the staged graph"
                enabled: root.staged !== null
                onClicked: root.applyRequested(root.staged)
            }
        }
    }
}
