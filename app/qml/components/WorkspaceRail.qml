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

    // Folded, this is a narrow tinted strip. Still on screen, because the
    // layouts that use this have no top bar: hiding it completely would leave
    // Ctrl+K as the only way to reach another workspace, which is the thing the
    // rail exists to prevent. Tinted and 18 px rather than a muted caret in
    // 14, for the same reason the folded toolbar is - a way back nobody can
    // find is not a way back.
    readonly property bool folded: root.app.navRailCollapsed
    implicitWidth: root.folded ? 18 : 46
    color: Theme.surfaceAlt
    border.color: Theme.border

    Rectangle {
        anchors.fill: parent
        visible: root.folded
        color: foldedHover.hovered
               ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.26)
               : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.13)
        Label {
            anchors.centerIn: parent
            rotation: -90
            text: (root.onRight ? "◂" : "▸") + "  WORKSPACES"
            color: foldedHover.hovered ? Theme.text : Theme.textSecondary
            font.pixelSize: 9
            font.bold: true
            font.letterSpacing: 1
        }
        HoverHandler { id: foldedHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.app.navRailCollapsed = false }
        ToolTip.visible: foldedHover.hovered
        ToolTip.text: "Show the workspace bar  ·  also under View ▸ Workspace bar"
    }

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
        visible: !root.folded

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
        // Fold the spine away, leaving the strip that brings it back.
        ItemDelegate {
            id: foldEntry
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            contentItem: Label {
                text: root.onRight ? "▸" : "◂"
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.textMuted
            }
            ToolTip.visible: foldEntry.hovered
            ToolTip.text: "Fold the workspace bar away"
            onClicked: root.app.navRailCollapsed = true
        }
    }
}
