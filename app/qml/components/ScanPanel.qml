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

    // GraphVis 17 offers seconds through hours; the scanner treats it as a
    // soft deadline and always returns the best result found so far.
    readonly property var budgets: [
        { label: "Quick · 8 s",     seconds: 8 },
        { label: "Standard · 30 s", seconds: 30 },
        { label: "Deep · 5 min",    seconds: 300 },
        { label: "Thorough · 30 min", seconds: 1800 },
        { label: "Exhaustive · 2 h",  seconds: 7200 }
    ]

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
        Button {
            text: "Clear"
            enabled: root.app.scanRecommendations.length > 0
            onClicked: root.app.clearScan()
        }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontSizeSmall
        color: root.app.scanSummary.error ? Theme.warning : Theme.textMuted
        text: {
            if (root.app.scanning) return "Profiling columns, scoring pairs and ranking graph-specific mappings…"
            if (root.app.scanSummary.error) return String(root.app.scanSummary.error)
            if (root.app.scanRecommendations.length > 0)
                return root.app.scanRecommendations.length + " recommendations · "
                     + Number(root.app.scanSummary.analysis_seconds).toFixed(1) + " s · "
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
        Layout.minimumHeight: 120
        clip: true
        spacing: 4
        model: root.app.scanRecommendations
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
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
                        text: modelData.graph
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
                        color: modelData.source === "literature" ? Theme.accent : Theme.surfaceAlt
                        border.color: Theme.border
                        Label {
                            id: scoreLabel
                            anchors.centerIn: parent
                            text: Math.round(Number(modelData.score) * 100) + "%"
                            font.pixelSize: Theme.fontSizeSmall
                            color: modelData.source === "literature" ? Theme.onAccent : Theme.textSecondary
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: {
                        var m = modelData.mappings || {}
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
                    text: modelData.reason || ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }

            HoverHandler { id: hover }
            TapHandler {
                onTapped: root.recommendationChosen(modelData.graph, modelData.mappings || ({}))
            }
        }
    }
}
