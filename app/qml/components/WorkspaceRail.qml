pragma ComponentBehavior: Bound
// The six workspaces as a spine of symbols down one edge.
//
// What the bar of words across the top does, in 46 pixels of width instead of
// 58 pixels of height - and on the edge rather than above, so a layout that
// uses it does not merely rearrange the panels, it changes what the window
// looks like from across the room.
//
// This is the half of "no top bar" that makes it a layout rather than a
// removal: hiding the bar and putting nothing in its place leaves Ctrl+K as
// the only way to reach Data or Publish, which is fine for the Bare shape and
// nothing else.
//
// Symbols only, with the name on hover and the current workspace marked by a
// bar down its edge rather than by colour alone - a highlight that is only a
// tint is invisible to a person who cannot see the tint, and this is the
// control that says where you are.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var app
    // Which edge it sits on, so the marker bar is drawn against the figure.
    property bool onRight: false
    signal importRequested()
    signal commandRequested()

    implicitWidth: 46
    color: Theme.surfaceAlt
    border.color: Theme.border

    readonly property var entries: [
        { mode: "Home",       glyph: "⌂", label: "Home" },
        { mode: "Literature", glyph: "▣", label: "Read literature" },
        { mode: "Visualize",  glyph: "◈", label: "Visualize" },
        { mode: "Data",       glyph: "▤", label: "Data" },
        { mode: "Analysis",   glyph: "Σ", label: "Analysis" },
        { mode: "Publish",    glyph: "↗", label: "Publish" }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 2

        Image {
            source: "qrc:/qt/qml/GraphVis/assets/graphvis_icon.png"
            sourceSize.width: 26; sourceSize.height: 26
            fillMode: Image.PreserveAspectFit
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 6
        }

        Repeater {
            model: root.entries
            delegate: ItemDelegate {
                id: entry
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                highlighted: root.app.workspaceMode === entry.modelData.mode

                contentItem: Label {
                    text: entry.modelData.glyph
                    font.pixelSize: 17
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: entry.highlighted ? Theme.text : Theme.textSecondary
                }
                // The marker, on the edge nearest the figure.
                Rectangle {
                    width: 3
                    height: parent.height - 12
                    y: 6
                    x: root.onRight ? 0 : parent.width - width
                    radius: 2
                    color: Theme.accent
                    visible: entry.highlighted
                }
                ToolTip.visible: hovered
                ToolTip.text: entry.modelData.label
                onClicked: root.app.workspaceMode = entry.modelData.mode
            }
        }

        Item { Layout.fillHeight: true }

        // The two actions that are not a workspace and are wanted from every
        // one of them.
        ItemDelegate {
            id: importEntry
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            contentItem: Label {
                text: "⤓"; font.pixelSize: 17
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.textSecondary
            }
            ToolTip.visible: importEntry.hovered
            ToolTip.text: "Import data"
            onClicked: root.importRequested()
        }
        ItemDelegate {
            id: findEntry
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            contentItem: Label {
                text: "⌕"; font.pixelSize: 17
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.textSecondary
            }
            ToolTip.visible: findEntry.hovered
            ToolTip.text: "Find anything · Ctrl+K"
            onClicked: root.commandRequested()
        }
    }
}
