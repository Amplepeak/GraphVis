// A panel that can be torn out into its own resizable window and docked back.
//
// GraphVis 17 let any dock or toolbar be popped out; that flexibility was lost
// in the rewrite. This restores it generically: wrap content in a DockPanel and
// it gains a compact header with a pop-out control. The content item is
// reparented rather than recreated, so state and bindings survive the trip out
// and back.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import GraphVis

Item {
    id: root

    property string title: ""
    // Some content cannot travel: a viewport that embeds a native window is
    // reparented across QWindows by tearing it out, which is not something to
    // do behind the user's back. Such a panel keeps its header and loses only
    // the pop-out control.
    property bool floatable: true
    property bool floating: false
    onFloatableChanged: if (!root.floatable) root.floating = false
    property int floatingWidth: 820
    property int floatingHeight: 620
    default property alias content: contentHost.data
    // Controls that belong to the panel rather than to what is inside it, laid
    // out in the header to the left of the pop-out button.
    //
    // Before this, the canvas's own controls had nowhere to go but a floating
    // row along the bottom of the figure, where they sat on top of the plot,
    // covered whatever was drawn near that corner, and grew sideways over each
    // other as more of them appeared. A panel header is where a panel's buttons
    // belong, and it is the one strip of the canvas that is not the figure.
    property alias headerActions: headerActionRow.data

    implicitWidth: 240
    implicitHeight: 240

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Compact chrome, close to GraphVis 17's 22-24 px panel headers.
        Rectangle {
            Layout.fillWidth: true
            // Tall enough for a combo box when the header carries controls, and
            // back to GraphVis 17's 26 px strip when it does not.
            implicitHeight: headerActionRow.children.length > 0 ? 34 : 26
            color: Theme.surfaceAlt
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 6

                Label { text: "⠿"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall }
                Label {
                    text: root.title
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    elide: Text.ElideRight
                    // Gives way to the controls rather than pushing them off the
                    // end: a button that has been shoved past the edge cannot be
                    // clicked, and a title that has been elided can still be
                    // read from what is left of it.
                    Layout.maximumWidth: Math.max(120, root.width * 0.34)
                }
                Item { Layout.fillWidth: true }
                RowLayout {
                    id: headerActionRow
                    spacing: 6
                    Layout.alignment: Qt.AlignVCenter
                }
                ToolButton {
                    visible: root.floatable
                    implicitWidth: 26
                    implicitHeight: 22
                    ToolTip.visible: hovered
                    ToolTip.text: root.floating ? "Dock back into the main window"
                                                : "Tear out into a floating window"
                    onClicked: root.floating = !root.floating
                    contentItem: Label {
                        text: root.floating ? "▣" : "⧉"
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // The slot the content sits in while docked.
        Item {
            id: dockedSlot
            Layout.fillWidth: true
            Layout.fillHeight: true

            Label {
                anchors.centerIn: parent
                visible: root.floating
                text: root.title + " is in a floating window"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }

    Window {
        id: floater
        width: root.floatingWidth
        height: root.floatingHeight
        minimumWidth: 320
        minimumHeight: 240
        visible: root.floating
        title: "GraphVis · " + root.title
        color: Theme.background
        onClosing: root.floating = false

        Item { id: floatSlot; anchors.fill: parent }
    }

    // The content lives in exactly one place at a time. anchors.fill re-resolves
    // against whichever slot is the current parent, so no manual geometry.
    Item {
        id: contentHost
        parent: root.floating ? floatSlot : dockedSlot
        anchors.fill: parent
    }
}
