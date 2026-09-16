// Ctrl+K: one box that finds anything.
//
// There is a palette in the experimental shell with six hard-coded actions in
// it, which is a menu with a search field in front of it. This one searches the
// things there are actually a lot of - 2,116 catalogue entries, the columns of
// the loaded dataset, the six layouts - because that is the case where hunting
// through a tree is the slow part, and it is the whole premise of the Command
// bar layout.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Popup {
    id: root
    required property var app
    // The live canvas, for the results that act on the figure.
    property var canvas: null

    signal importRequested()
    signal literatureRequested()
    signal exportRequested()

    anchors.centerIn: Overlay.overlay
    // Overlay.overlay, not `parent`: a Popup's parent is whatever it was
    // declared under, and under ApplicationWindow that is not the item it is
    // centred in - so the width came from one thing and the position from
    // another.
    width: Math.min(680, Overlay.overlay ? Overlay.overlay.width - 80 : 680)
    height: 440
    modal: true
    focus: true
    padding: 0

    background: Rectangle {
        radius: 14
        color: Theme.surfaceAlt
        border.color: Theme.borderStrong
    }

    onOpened: { search.text = ""; search.forceActiveFocus() }

    // Fixed actions, then whatever the query matches. Kept as data rather than
    // as a chain of if statements so the list and the runner cannot disagree
    // about what exists.
    readonly property var actions: [
        { kind: "action", id: "import",     label: "Import dataset…",        hint: "open a file" },
        { kind: "action", id: "literature", label: "Read literature",        hint: "workspace" },
        { kind: "action", id: "data",       label: "Data workspace",         hint: "workspace" },
        { kind: "action", id: "analysis",   label: "Analysis",               hint: "workspace" },
        { kind: "action", id: "publish",    label: "Publication Studio",     hint: "workspace" },
        { kind: "action", id: "export",     label: "Save figure as…",        hint: "PDF, SVG, PNG…" },
        { kind: "action", id: "resetview",  label: "Reset the figure's view", hint: "axes or camera" }
    ]

    function results(query) {
        var q = query.trim().toLowerCase()
        var out = []

        for (var i = 0; i < root.actions.length; ++i) {
            var a = root.actions[i]
            if (q === "" || a.label.toLowerCase().indexOf(q) >= 0)
                out.push(a)
        }

        // Layouts, so the shape of the window is reachable without knowing
        // which menu it is under.
        var names = root.app.uiLayoutNames
        for (var L = 0; L < names.length; ++L) {
            if (q !== "" && names[L].toLowerCase().indexOf(q) < 0) continue
            out.push({ kind: "layout", id: String(L), label: names[L] + " layout",
                       hint: "window shape" })
        }

        // The columns of the loaded dataset, which set the x axis.
        var cols = root.app.activeColumns || []
        for (var c = 0; c < cols.length && out.length < 60; ++c) {
            if (q === "" || cols[c].toLowerCase().indexOf(q) < 0) continue
            out.push({ kind: "column", id: cols[c], label: cols[c], hint: "plot on x" })
        }

        // And the catalogue. Only on a real query: listing 1,359 graphs when
        // nothing has been typed is the tree this box exists to replace.
        if (q.length >= 2) {
            var hits = root.app.searchGraphs(q, true, 40)
            for (var h = 0; h < hits.length; ++h) {
                var e = hits[h]
                out.push({ kind: "graph", id: e.engine, scale: e.scale ? e.scale : "",
                           label: e.engine + (e.scale ? " · " + e.scale : ""),
                           hint: e.category ? e.category : "graph" })
            }
        }
        return out
    }

    function run(item) {
        if (!item) return
        if (item.kind === "action") {
            if (item.id === "import") root.importRequested()
            else if (item.id === "literature") root.app.workspaceMode = "Literature"
            else if (item.id === "data") root.app.workspaceMode = "Data"
            else if (item.id === "analysis") root.app.workspaceMode = "Analysis"
            else if (item.id === "publish") root.app.workspaceMode = "Publish"
            else if (item.id === "export") root.exportRequested()
            else if (item.id === "resetview" && root.canvas) {
                if (root.canvas.view3D) root.canvas.resetCamera()
                else root.canvas.resetView()
            }
        } else if (item.kind === "layout") {
            root.app.uiLayout = parseInt(item.id)
        } else if (item.kind === "column" && root.canvas) {
            root.canvas.xColumn = item.id
        } else if (item.kind === "graph" && root.canvas) {
            root.app.rendererMode = "Qt 2-D"
            root.canvas.engine = item.id
            root.canvas.variant = item.scale
            root.canvas.title = item.id
            root.app.noteVisualisation(item.id, item.scale, item.category ? item.category : "")
        }
        root.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        TextField {
            id: search
            Layout.fillWidth: true
            placeholderText: "Search graphs, columns, layouts and actions…"
            font.pixelSize: 16
            // Enter runs the highlighted row, and the highlighted row starts at
            // the top. Reading it out of the model rather than out of a
            // delegate: itemAtIndex returns null for a row the view has not
            // realised, which is exactly the case one keystroke after typing.
            onAccepted: root.run(list.model[Math.max(0, list.currentIndex)])
            Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
            Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.results(search.text)
            currentIndex: 0
            ScrollBar.vertical: ScrollBar {}
            highlight: Rectangle { color: Theme.accent; opacity: 0.18; radius: 6 }
            highlightMoveDuration: 0

            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                onClicked: root.run(row.modelData)
                contentItem: RowLayout {
                    spacing: 10
                    Label {
                        text: {
                            switch (row.modelData.kind) {
                            case "graph":  return "◈"
                            case "column": return "▤"
                            case "layout": return "▥"
                            default:       return "›"
                            }
                        }
                        color: Theme.textMuted
                        Layout.preferredWidth: 14
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.label
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    Label {
                        text: row.modelData.hint
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        Label {
            visible: list.count === 0
            Layout.fillWidth: true
            text: search.text.trim() === ""
                  ? "Type to search."
                  : "Nothing matches “" + search.text + "”."
            color: Theme.textMuted
        }
    }
}
