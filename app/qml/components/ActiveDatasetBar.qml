// Which dataset the graphs below are drawn from, and a one-click way to change it.
//
// Switching datasets meant leaving the Graphs tab for the Data tab, finding the
// row, clicking it, and coming back — four actions and a loss of place, to
// answer a question ("what does this look like for the other run?") that people
// ask constantly. The graph chooser sits under this bar and every graph in it
// draws from whatever is selected here, so this is where the choice belongs.
//
// It states the row count too, because "wrong dataset" and "right dataset,
// wrong columns" look identical on an empty plot, and the row count tells them
// apart at a glance.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var app
    // Emitted when the user asks to import one, so the shell can open the file
    // dialog it already owns rather than this bar opening one of its own.
    signal importRequested()

    readonly property var sets: root.app.datasets
    readonly property var current: root.app.activeDataset()

    implicitHeight: content.implicitHeight + 14
    color: Theme.surface
    border.color: Theme.border
    radius: Theme.radius

    ColumnLayout {
        id: content
        anchors.left: parent.left; anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10; anchors.rightMargin: 10
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "Dataset"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }

            ComboBox {
                id: picker
                Layout.fillWidth: true
                visible: root.sets.length > 0
                model: root.sets
                textRole: "name"
                valueRole: "id"

                currentIndex: {
                    for (var i = 0; i < root.sets.length; ++i)
                        if (root.sets[i].id === root.app.activeDatasetId) return i
                    return -1
                }

                // Set, not toggled: re-selecting the dataset already active must
                // not clear it, and assigning the same id is a no-op in the
                // controller.
                onActivated: (index) => {
                    root.app.activeDatasetId = root.sets[index].id
                }

                ToolTip.visible: picker.hovered
                ToolTip.text: "The dataset every graph below is drawn from. "
                            + "Changing it re-renders the current graph with the same "
                            + "column mapping where the names still exist."

                delegate: ItemDelegate {
                    id: entry
                    required property int index
                    required property var modelData
                    width: ListView.view ? ListView.view.width : entry.implicitWidth
                    highlighted: picker.highlightedIndex === entry.index
                    contentItem: ColumnLayout {
                        spacing: 0
                        Label {
                            text: entry.modelData.name || "dataset"
                            color: Theme.text
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: (entry.modelData.rows !== undefined
                                   ? entry.modelData.rows + " rows" : "")
                                + (entry.modelData.numeric_columns
                                   ? "  ·  " + entry.modelData.numeric_columns.length + " numeric"
                                   : "")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: root.sets.length === 0
                text: "None imported yet"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }

            Button {
                id: importButton
                text: "Import…"
                flat: root.sets.length > 0
                onClicked: root.importRequested()
                ToolTip.visible: importButton.hovered
                ToolTip.text: "Import another dataset — CSV, Parquet or Arrow"
            }
        }

        // Row and column counts, so "wrong dataset" and "right dataset, wrong
        // columns" can be told apart without leaving the tab.
        Label {
            Layout.fillWidth: true
            visible: root.sets.length > 0
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
            text: {
                var d = root.current
                if (!d || d.name === undefined) return ""
                var bits = []
                if (d.rows !== undefined) bits.push(d.rows + " rows")
                if (d.columns !== undefined) bits.push(d.columns.length + " columns")
                else if (d.numeric_columns !== undefined)
                    bits.push(d.numeric_columns.length + " numeric columns")
                if (root.sets.length > 1) bits.push(root.sets.length + " datasets loaded")
                return bits.join("  ·  ")
            }
        }
    }
}
