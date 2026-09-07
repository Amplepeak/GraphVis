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
    property bool floating: false
    property int floatingWidth: 820
    property int floatingHeight: 620
    default property alias content: contentHost.data

    implicitWidth: 240
    implicitHeight: 240

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Compact chrome, close to GraphVis 17's 22-24 px panel headers.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 26
            color: Theme.surfaceAlt
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 6

                Label { text: "⠿"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall }
                Label {
                    Layout.fillWidth: true
                    text: root.title
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    elide: Text.ElideRight
                }
                ToolButton {
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
