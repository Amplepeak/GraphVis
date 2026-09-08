// Qt 6 delegate scoping. Without this, an id from the enclosing file is not
// legally visible inside a delegate or an inline Component - it resolves only
// because the old unbound context lookup walks out of the component, which is
// slower at every evaluation and silently breaks the moment a model role
// shares a name. Bound resolves ids at compile time; delegates declare what
// they take from the model with `required property`.
pragma ComponentBehavior: Bound
// Graph Library — the GraphVis 17 catalogue, native.
//
// 318 entries across 30 categories come from app.graphCategories, which
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

    // Which categories are open. 433 graphs in 46 categories is a list nobody
    // scrolls through - the one they want is forty screens down and they do not
    // know which heading it is under. Collapsed by default, so the first thing
    // on screen is the 46 headings rather than the first 12 of 433 entries.
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

        var results = app.searchGraphs(q, advancedToggle.checked, 400)
        var lastCat = ""
        for (var r = 0; r < results.length; ++r) {
            var e = results[r]
            if (q.length === 0 && e.category !== lastCat) {
                out.push({ header: true, label: e.category, count: 0, open: true, entry: undefined })
                lastCat = e.category
            }
            out.push({ header: false, label: e.engine, count: 0, open: false, entry: e })
        }
        rows = out
    }

    Component.onCompleted: refresh()

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8; spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Label { text: "GRAPH LIBRARY"; color: Theme.textMuted; font.pixelSize: 11; font.bold: true }
            Item { Layout.fillWidth: true }
            Label { text: root.app.graphEntryCount + " graphs"; color: Theme.textMuted; font.pixelSize: 11 }
        }

        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: "Search graphs…"
            onTextChanged: root.refresh()
        }

        CheckBox {
            id: advancedToggle
            checked: false
            onToggled: root.refresh()
            // The Basic style draws its label in a dark colour that is
            // unreadable on this panel, so the label is styled explicitly.
            contentItem: Text {
                text: "Include advanced (" + root.app.advancedEntryCount + " more)"
                color: Theme.textSecondary
                font.pixelSize: 11
                verticalAlignment: Text.AlignVCenter
                leftPadding: advancedToggle.indicator.width + 6
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: searchField.text.trim().length === 0
            Label {
                text: "Categories"
                color: Theme.textMuted
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                text: "Expand all"
                font.pixelSize: 11
                flat: true
                onClicked: root.setAllCategories(true)
            }
            ToolButton {
                text: "Collapse all"
                font.pixelSize: 11
                flat: true
                onClicked: root.setAllCategories(false)
            }
        }

        ListView {
            id: view
            Layout.fillWidth: true; Layout.fillHeight: true
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

                Rectangle {
                    visible: !entry.modelData.header
                    anchors.fill: parent; anchors.rightMargin: 4
                    radius: 5
                    color: root.staged && entry.modelData.entry && root.staged.engine === entry.modelData.entry.engine
                           && root.staged.category === entry.modelData.entry.category
                           ? Theme.surfaceAlt : (hover.hovered ? Theme.surfaceAlt : "transparent")

                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 8

                        Image {
                            Layout.preferredWidth: 46; Layout.preferredHeight: 28
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true; cache: true
                            source: entry.modelData.entry ? root.app.graphThumbnail(entry.modelData.entry.thumbnail, true) : ""
                            visible: status === Image.Ready
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 0
                            Label {
                                text: entry.modelData.label + (entry.modelData.entry && entry.modelData.entry.scale ? " · " + entry.modelData.entry.scale : "")
                                color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            Label {
                                text: entry.modelData.entry ? entry.modelData.entry.description : ""
                                color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                        }
                        Label {
                            text: entry.modelData.entry && entry.modelData.entry.advanced ? "adv" : ""
                            color: Theme.textMuted; font.pixelSize: 9
                        }
                    }

                    HoverHandler { id: hover }
                    TapHandler { onTapped: { if (entry.modelData.entry) { root.staged = entry.modelData.entry; root.app.notify("Staged: " + entry.modelData.entry.engine) } } }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        // Smart Suite: let the scanner pick the graph and its mapping.
        ScanPanel {
            id: scan
            Layout.fillWidth: true
            Layout.preferredHeight: scanOpen.checked ? 300 : 0
            visible: scanOpen.checked
            app: root.app
            onRecommendationChosen: (graph, mappings) => root.scanApplied(graph, mappings)
        }

        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                id: scanOpen
                text: "Smart Suite"
                checked: false
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: root.app.scanRecommendations.length > 0
                text: root.app.scanRecommendations.length + " suggested"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Label {
                    text: root.staged ? root.staged.engine : "No graph staged"
                    color: Theme.text; font.pixelSize: 12; font.bold: true
                    elide: Text.ElideRight; Layout.fillWidth: true
                }
                Label {
                    text: root.staged ? root.staged.category : "Pick a graph, then Apply"
                    color: Theme.textMuted; font.pixelSize: 10
                    elide: Text.ElideRight; Layout.fillWidth: true
                }
            }
            Button {
                text: "▶"
                ToolTip.visible: hovered
                ToolTip.text: "Apply the staged graph"
                enabled: root.staged !== null
                onClicked: root.applyRequested(root.staged)
            }
        }
    }
}
