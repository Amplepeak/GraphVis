// The open figures, as a row of tabs.
//
// Presentation only. It owns no figures and creates none: it is handed a model
// and reports what was done to it, so the workspace stays the one place that
// decides what a figure IS. That matters more here than usual, because the
// alternative - a bar that keeps its own list alongside the workspace's - is
// the shape of fault this program has produced four times now, most recently a
// column count the panel worked out for itself that disagreed with the figure
// it was printed beside.
//
// What a tab carries:
//   - the figure's title, which is its engine unless it has been renamed
//   - a LOCK, which is the point of the star: a locked figure refuses to close,
//     so an afternoon's work cannot go to a mis-aimed click on a small target
//   - a CLOSE, which is absent rather than disabled on a locked tab, because a
//     close button that does nothing invites a second, harder click
//   - a FLOAT, which moves the figure into a window of its own and back. It
//     MOVES: the live canvas is reparented, not copied, so the sidebar still
//     edits the figure you are looking at. This was left out of the first pass
//     rather than stubbed, because a window showing a figure the controls do
//     not reach is worse than no menu item.
//
// Dragging reorders. The drag moves the MODEL rather than the tab, so the order
// is the order of the figures themselves and survives being read back.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Theme is a singleton registered into this module from C++.
import GraphVis

Rectangle {
    id: root

    // {title, locked, floating} per figure. Owned by the workspace.
    required property var model
    required property int currentIndex

    // FOLDED, not hidden. The caret leaves a strip carrying the caret itself
    // and the current figure's name, because a control whose only "collapse"
    // is to vanish gives the person nothing to click to get it back - they
    // have to know it came from the View menu. Hiding it outright is what the
    // View menu is for, and that is a different decision.
    property bool folded: false

    signal selected(int index)
    signal closeRequested(int index)
    signal lockToggled(int index)
    signal floatToggled(int index)
    signal reordered(int from, int to)
    signal addRequested()

    implicitHeight: 32
    color: Theme.surface
    // A single line, so the bar reads as an edge of the canvas rather than as
    // another panel stacked on top of it.
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 0

        ToolButton {
            text: root.folded ? "›" : "⌄"
            Layout.preferredHeight: 24
            Layout.preferredWidth: 22
            onClicked: root.folded = !root.folded
            ToolTip.visible: hovered
            ToolTip.text: root.folded ? "Show the open figures"
                                      : "Fold the figure strip"
        }

        // What the strip says when it is folded: which figure you are on, so
        // the fold costs the one piece of information the bar exists to give.
        Label {
            visible: root.folded
            Layout.fillWidth: true
            Layout.leftMargin: 6
            elide: Text.ElideRight
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            text: root.currentIndex >= 0 && root.model && root.currentIndex < root.model.count
                  ? root.model.get(root.currentIndex).title + "  ·  "
                    + root.model.count + (root.model.count === 1 ? " figure" : " figures")
                  : ""
        }

        ListView {
            id: tabs
            visible: !root.folded
            Layout.fillWidth: !root.folded
            Layout.preferredWidth: root.folded ? 0 : -1
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            clip: true
            model: root.model
            // The tabs are the whole point of the bar, so they get the space
            // and the Add button is what gives way.
            spacing: 2

            delegate: Item {
                id: tab
                required property int index
                required property string title
                required property bool locked
                required property bool floating
                height: tabs.height
                width: Math.min(200, Math.max(110, label.implicitWidth + 62))

                readonly property bool current: tab.index === root.currentIndex

                Rectangle {
                    id: chrome
                    anchors.fill: parent
                    anchors.topMargin: 3
                    anchors.bottomMargin: 0
                    radius: 4
                    color: tab.current ? Theme.background
                                       : (hover.hovered ? Theme.surfaceAlt : "transparent")
                    border.color: tab.current ? Theme.border : "transparent"
                    // The current tab joins the canvas below it: the bottom
                    // corners are square and the border stops, so the tab and
                    // the figure read as one surface rather than two.
                    Rectangle {
                        visible: tab.current
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 2
                        color: Theme.background
                    }
                }
                HoverHandler { id: hover }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 4
                    spacing: 4

                    // The star reads as state, not as a button: filled when the
                    // figure is locked, outlined when it is not, and always
                    // present so its position never moves.
                    Label {
                        text: tab.locked ? "★" : "☆"
                        color: tab.locked ? Theme.accent : Theme.textSecondary
                        font.pixelSize: 12
                        TapHandler { onTapped: root.lockToggled(tab.index) }
                        ToolTip.visible: starHover.hovered
                        ToolTip.text: tab.locked
                            ? "Locked — this figure cannot be closed. Click to unlock."
                            : "Lock this figure so it cannot be closed by accident."
                        HoverHandler { id: starHover }
                    }

                    // A floated figure is not in the canvas area, so without
                    // this its tab looks like a figure that failed to draw.
                    Label {
                        visible: tab.floating
                        text: "⧉"
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        ToolTip.visible: floatHover.hovered
                        ToolTip.text: "In its own window. Click the tab to bring it forward."
                        HoverHandler { id: floatHover }
                    }

                    Label {
                        id: label
                        Layout.fillWidth: true
                        text: tab.title
                        elide: Text.ElideRight
                        color: tab.current ? Theme.text : Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: tab.current
                    }

                    // Absent on a locked figure rather than disabled - see the
                    // note at the top.
                    Label {
                        visible: !tab.locked && (tab.current || hover.hovered)
                        text: "✕"
                        color: closeHover.hovered ? Theme.warning : Theme.textSecondary
                        font.pixelSize: 11
                        TapHandler { onTapped: root.closeRequested(tab.index) }
                        HoverHandler { id: closeHover }
                        ToolTip.visible: closeHover.hovered
                        ToolTip.text: "Close this figure"
                    }
                }

                // Selecting, and dragging to reorder. One handler for the
                // press so a drag that goes nowhere still selects the tab.
                TapHandler { onTapped: root.selected(tab.index) }
                DragHandler {
                    id: drag
                    yAxis.enabled: false
                    onActiveChanged: {
                        if (drag.active) { tab.z = 2; return }
                        tab.z = 0
                        // Where it was dropped, in tab widths from where it
                        // started. Reordering by POSITION rather than by which
                        // tab is underneath keeps a drag past the end working.
                        var moved = Math.round(chrome.x / tab.width)
                        chrome.x = 0
                        if (moved !== 0)
                            root.reordered(tab.index,
                                           Math.max(0, Math.min(tabs.count - 1,
                                                                tab.index + moved)))
                    }
                    target: chrome
                }

                // Right-click gets the same three actions as the tab itself,
                // for a tab too narrow to show them.
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: tabMenu.popup()
                }
                Menu {
                    id: tabMenu
                    MenuItem {
                        text: tab.locked ? "Unlock figure" : "Lock figure"
                        onTriggered: root.lockToggled(tab.index)
                    }
                    MenuItem {
                        text: tab.floating ? "Put back in the window"
                                           : "Open in its own window"
                        onTriggered: root.floatToggled(tab.index)
                    }
                    MenuItem {
                        text: "Close figure"
                        enabled: !tab.locked
                        onTriggered: root.closeRequested(tab.index)
                    }
                }
            }
        }

        ToolButton {
            visible: !root.folded
            text: "+"
            Layout.preferredHeight: 24
            Layout.preferredWidth: 26
            onClicked: root.addRequested()
            ToolTip.visible: hovered
            ToolTip.text: "Add a figure"
        }
    }
}
