// A GroupBox whose title does not collide with its own frame.
//
// The Basic style draws the label directly on the frame line with no
// background, which on a dark palette reads as struck-through text. This
// version insets the frame, gives the label the panel colour behind it, and
// takes its colours from Theme.
import QtQuick
import QtQuick.Controls
import GraphVis

GroupBox {
    id: control

    property color panelColor: Theme.surface

    label: Item {
        implicitHeight: caption.implicitHeight + 4
        Rectangle {
            x: 10
            y: (parent.height - caption.implicitHeight) / 2
            width: caption.implicitWidth + 10
            height: caption.implicitHeight
            color: control.panelColor
        }
        Label {
            id: caption
            x: 15
            y: (parent.height - implicitHeight) / 2
            text: control.title
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            font.bold: true
        }
    }

    background: Rectangle {
        y: control.topPadding - control.bottomPadding
        width: parent.width
        height: parent.height - control.topPadding + control.bottomPadding
        color: "transparent"
        border.color: Theme.border
        radius: Theme.radius
    }

    topPadding: 18
    bottomPadding: 10
    leftPadding: 10
    rightPadding: 10
}
