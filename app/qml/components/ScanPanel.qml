// Qt 6 delegate scoping. Without this, an id from the enclosing file is not
// legally visible inside a delegate or an inline Component - it resolves only
// because the old unbound context lookup walks out of the component, which is
// slower at every evaluation and silently breaks the moment a model role
// shares a name. Bound resolves ids at compile time; delegates declare what
// they take from the model with `required property`.
pragma ComponentBehavior: Bound
// Smart Suite / Scan Dataset.
//
// Runs GraphVis 17's intelligent_scan over the active dataset through the
// optional Python science service, and turns each recommendation into a
// one-click action: stage that catalogue graph with the axis mapping the
// scanner chose. Ported behaviour, including the thinking budget - a longer
// budget widens column breadth and pair sampling rather than just waiting.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ColumnLayout {
    id: root
    required property var app
    signal recommendationChosen(string graph, var mappings)

    spacing: Theme.gap

    // Measured, not assumed: with three results in a 360 px sidebar this block
    // needs 194 px, because the summary line wraps to two. The library panel
    // capped it at 180. A QtQuick layout given less than its children's minimum
    // does not shrink them - it lays them out past its own edge - so the
    // overflow painted straight over the search field below, and the
    // recommendation text and "Search graphs..." appeared on top of each other.
    //
    // The cap is fixed next door. This is the guarantee: whatever height this
    // panel is given, it cannot draw outside it. Clipping a little content is a
    // visible, understandable loss; painting over a neighbouring control is
    // corruption that looks like a broken program.
    clip: true

    // GraphVis 17 offers seconds through hours; the scanner treats it as a
    // soft deadline and always returns the best result found so far.
    readonly property var budgets: [
        { label: "Quick · 8 s",     seconds: 8 },
        { label: "Standard · 30 s", seconds: 30 },
        { label: "Deep · 5 min",    seconds: 300 },
        { label: "Thorough · 30 min", seconds: 1800 },
        { label: "Exhaustive · 2 h",  seconds: 7200 }
    ]

    // One click: scan if needed, then draw the best answer.
    //
    // Deliberately NOT a separate top-level tab with its own data picker,
    // canvas and overlay controls. All three already exist here and in
    // Visualize, and a parallel copy of them is two of everything that can
    // disagree - which is the fault this codebase's own comments complain about
    // most often. This is the same scan, the same recommendation list and the
    // same apply path that a click on a card takes; it just skips the reading.
    property bool applyBestWhenReady: false

    function applyBest() {
        var found = root.app.scanRecommendations
        if (!found || found.length === 0) return false
        root.recommendationChosen(found[0].graph, found[0].mappings || ({}))
        return true
    }

    Connections {
        target: root.app
        // scanChanged carries the scanning flag AND the results, so this fires
        // at the start of a scan too; waiting for scanning to go false is what
        // makes it the finish rather than the start.
        function onScanChanged() {
            if (!root.applyBestWhenReady || root.app.scanning) return
            root.applyBestWhenReady = false
            root.applyBest()
        }
    }

    Button {
        id: goButton
        Layout.fillWidth: true
        highlighted: true
        text: root.app.scanning
              ? "Scanning…"
              : (root.app.scanRecommendations.length > 0
                 ? "Draw the best one"
                 : "Go — scan and draw the best")
        enabled: !root.app.busy && root.app.activeDatasetId !== ""
        onClicked: {
            if (root.applyBest()) return
            // Nothing to apply yet, so scan and pick it up when it lands.
            root.applyBestWhenReady = true
            root.app.scanDataset(root.budgets[budgetBox.currentIndex].seconds,
                                 useLiterature.checked)
        }
        ToolTip.visible: goButton.hovered
        ToolTip.text: root.app.activeDatasetId === ""
            ? "Load a dataset first"
            : "Rank the graphs for this data and draw the top one, without choosing anything"
    }

    // Said out loud rather than left as a button that appears to do nothing: a
    // scan CAN legitimately come back with no recommendation - one constant
    // column, or two rows - and silence there is indistinguishable from a
    // broken button.
    Label {
        Layout.fillWidth: true
        visible: !root.app.scanning && root.app.scanRecommendations.length === 0
                 && root.app.scanSummary.examined_pairs !== undefined
                 && !root.app.scanSummary.error
        text: "The last scan found nothing worth recommending — usually too few rows, "
            + "or columns that do not vary."
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        Label { text: "Thinking budget"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
        ComboBox {
            id: budgetBox
            Layout.fillWidth: true
            currentIndex: 1
            textRole: "label"
            model: root.budgets
        }
    }

    // Phase 4. GraphVis 17 separated "Scan Dataset" from "Scan + Literature…";
    // here it is one scan with the paper as optional context, which keeps the
    // two from drifting apart.
    CheckBox {
        id: useLiterature
        text: root.app.literatureContextAvailable
              ? "Use the open paper as context"
              : "Use the open paper as context (analyse a paper first)"
        enabled: root.app.literatureContextAvailable
        checked: root.app.literatureContextAvailable
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        Button {
            Layout.fillWidth: true
            text: root.app.scanning ? "Scanning…"
                 : (useLiterature.checked ? "Scan + Literature" : "Scan Dataset")
            enabled: !root.app.busy && root.app.activeDatasetId !== ""
            onClicked: root.app.scanDataset(root.budgets[budgetBox.currentIndex].seconds,
                                            useLiterature.checked)
        }
        // A scan of the same data, budget and paper is cached and comes back
        // instantly. Rescan forces the work to be redone.
        Button {
            text: "Rescan"
            ToolTip.visible: hovered
            ToolTip.text: "Ignore the cached result and scan again"
            enabled: !root.app.busy && root.app.activeDatasetId !== ""
            onClicked: root.app.scanDataset(root.budgets[budgetBox.currentIndex].seconds,
                                            useLiterature.checked, true)
        }
        Button {
            text: "Clear"
            ToolTip.visible: hovered
            ToolTip.text: "Clear the results shown here"
            enabled: root.app.scanRecommendations.length > 0
            onClicked: root.app.clearScan()
        }
        Button {
            text: "⌫"
            implicitWidth: 34
            ToolTip.visible: hovered
            ToolTip.text: "Delete every cached scan for this project"
            onClicked: root.app.clearScanCache()
        }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        // Two lines at most. This is the line that wraps and pushes the whole
        // block past its allowance in a narrow sidebar; an unbounded error
        // message could otherwise take the list's height entirely.
        maximumLineCount: 2
        elide: Text.ElideRight
        font.pixelSize: Theme.fontSizeSmall
        color: root.app.scanSummary.error ? Theme.warning : Theme.textMuted
        text: {
            if (root.app.scanning) return "Profiling columns, scoring pairs and ranking graph-specific mappings…"
            if (root.app.scanSummary.error) return String(root.app.scanSummary.error)
            if (root.app.scanRecommendations.length > 0)
                return root.app.scanRecommendations.length + " recommendations · "
                     + (root.app.scanSummary.cached ? "cached · "
                        : Number(root.app.scanSummary.analysis_seconds).toFixed(1) + " s · ")
                     + root.app.scanSummary.examined_pairs + " pairs examined"
                     + (root.app.scanSummary.budget_exhausted ? " · budget reached" : "")
                     + (root.app.scanSummary.literature_used > 0 ? " · paper used as context" : "")
            if (root.app.activeDatasetId === "") return "Import or select a dataset to scan."
            if (!root.app.scienceServiceAvailable) return "Scanning uses the optional GraphVis Science add-on."
            return "Scan profiles every numeric column, scores relationships and ranks graphs with a mapping for each."
        }
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        // Low enough that the whole block fits inside the share the library
        // panel gives it on a short window. A floor of 120 made the block
        // taller than its own allowance, so the buttons above it were pushed
        // out of the panel instead of the list shrinking.
        Layout.minimumHeight: 70
        clip: true
        spacing: 4
        model: root.app.scanRecommendations
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: rec
            required property var modelData
            width: ListView.view.width
            height: body.implicitHeight + 14
            radius: Theme.radius
            color: hover.hovered ? Theme.surfaceAlt : "transparent"
            border.color: hover.hovered ? Theme.borderStrong : Theme.border

            ColumnLayout {
                id: body
                anchors.fill: parent
                anchors.margins: 7
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: rec.modelData.graph
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Theme.fontSizeBody
                        elide: Text.ElideRight
                    }
                    // Confidence, as the scanner scored it.
                    Rectangle {
                        implicitWidth: scoreLabel.implicitWidth + 10
                        implicitHeight: scoreLabel.implicitHeight + 4
                        radius: 3
                        color: rec.modelData.source === "literature" ? Theme.accent : Theme.surfaceAlt
                        border.color: Theme.border
                        Label {
                            id: scoreLabel
                            anchors.centerIn: parent
                            text: Math.round(Number(rec.modelData.score) * 100) + "%"
                            font.pixelSize: Theme.fontSizeSmall
                            color: rec.modelData.source === "literature" ? Theme.onAccent : Theme.textSecondary
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: {
                        var m = rec.modelData.mappings || {}
                        var parts = []
                        if (m.x) parts.push("X " + m.x)
                        if (m.y) parts.push("Y " + m.y)
                        if (m.z) parts.push("Z " + m.z)
                        return parts.join("   ")
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: rec.modelData.reason || ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }

            HoverHandler { id: hover }
            TapHandler {
                onTapped: root.recommendationChosen(rec.modelData.graph, rec.modelData.mappings || ({}))
            }
        }
    }
}
