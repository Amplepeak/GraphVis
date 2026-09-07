// Landing page.
//
// The previous version carried a marketing strapline and a row of six chips
// naming internal technologies (Rust native core, WGPU direct GPU, Apache
// Arrow...). None of them did anything, and they competed with the two actions
// that matter. Both are gone. What remains is a single hierarchy: what this is,
// the two entry points, and current state.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var app
    signal importRequested()
    signal literatureRequested()
    color: Theme.background

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 96, 1040)
        spacing: Theme.gapWide

        Label {
            text: "GraphVis"
            font.pixelSize: Theme.fontSizeDisplay
            font.bold: true
            color: Theme.text
        }
        Label {
            text: "Read a paper, extract its data, and plot it against your own."
            font.pixelSize: 17
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.gap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapWide

            // Primary action. Literature is the differentiator, so it leads.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                radius: Theme.radius * 2
                color: Theme.surfaceAlt
                border.width: 2
                border.color: Theme.accent

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: Theme.gap
                    Label { text: "READ LITERATURE"; color: Theme.accent; font.pixelSize: Theme.fontSizeSmall; font.bold: true }
                    Label {
                        text: "Turn papers into usable data"
                        color: Theme.text; font.pixelSize: 24; font.bold: true
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Label {
                        text: "Extract tables and curves from a PDF, then compare them with your own datasets."
                        color: Theme.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Item { Layout.fillHeight: true }
                    Button { text: "Open literature"; font.bold: true; onClicked: root.literatureRequested() }
                }
            }

            Rectangle {
                Layout.preferredWidth: 320
                Layout.preferredHeight: 200
                radius: Theme.radius * 2
                color: Theme.surface
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: Theme.gap
                    Label { text: "YOUR DATA"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.bold: true }
                    Label {
                        text: "Import and plot"
                        color: Theme.text; font.pixelSize: 24; font.bold: true
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Label {
                        text: "CSV, Parquet or Arrow, straight into the graph library."
                        color: Theme.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Item { Layout.fillHeight: true }
                    Button { text: "Import dataset"; onClicked: root.importRequested() }
                }
            }
        }

        // Real state, not decoration: what is loaded right now.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.gap
            spacing: Theme.gapWide
            Label {
                text: root.app.datasets.length === 0
                      ? "No datasets loaded"
                      : root.app.datasets.length + (root.app.datasets.length === 1 ? " dataset loaded" : " datasets loaded")
                color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall
            }
            Label {
                text: root.app.graphEntryCount + " graphs in the library"
                color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall
            }
            Label {
                visible: root.app.literatureUrl.toString() !== ""
                text: "Paper open: " + root.app.literatureUrl.toString().split("/").pop()
                color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideMiddle; Layout.fillWidth: true
            }
            Item { Layout.fillWidth: true }
        }
    }
}
