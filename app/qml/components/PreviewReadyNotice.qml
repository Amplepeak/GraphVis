// "The full render is ready" notice.
//
// Sits at the bottom right INSIDE the preview, because that is where someone
// looking at the preview is already looking. It exists so the preview is
// dismissed deliberately rather than yanked away mid-edit: a render that lands
// while you are dragging an axis must not steal the view from under you, so it
// waits here until you say so.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var canvas

    // Same as RenderProgressBadge: the fade drives `visible`, not the reverse,
    // or the notice can never animate away.
    opacity: canvas.fullRenderWaiting ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 160 } }

    implicitWidth: content.implicitWidth + 22
    implicitHeight: content.implicitHeight + 16
    radius: 7
    color: Qt.rgba(Theme.surfaceAlt.r, Theme.surfaceAlt.g, Theme.surfaceAlt.b, 0.96)
    border.color: Theme.accent

    RowLayout {
        id: content
        anchors.centerIn: parent
        spacing: 10

        Label {
            text: "✓"
            color: Theme.positive
            font.bold: true
        }
        ColumnLayout {
            spacing: 1
            Label {
                text: "Full-resolution render ready"
                color: Theme.text
                font.pixelSize: Theme.fontSizeSmall
            }
            Label {
                text: root.canvas.pendingEditCount > 0
                      ? root.canvas.pendingEditCount + " edit"
                        + (root.canvas.pendingEditCount === 1 ? "" : "s")
                        + " since — showing it will re-render"
                      : "Preview is drawing " + root.canvas.previewPointCount
                        + " of " + root.canvas.fullPointCount + " points"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
        }
        Button {
            text: "Show it"
            onClicked: root.canvas.acceptFullRender()
        }
    }
}
