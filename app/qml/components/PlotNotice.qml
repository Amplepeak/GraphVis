// Why a figure looks the way it does, said underneath it.
//
// This started life inside the plot toolbar as a StatusPill, which sizes itself
// to `implicitWidth + 20` with no wrapping. That was fine for "Line Chart · 20000
// points" and became a disaster the moment the canvas learned to explain
// itself: a two-line sentence about mapped columns stretched the pill across
// the whole window, over the Export PDF button and the colour map picker, and
// sat there permanently because the condition that produced it was permanent.
// A message that covers the controls you would use to act on it is worse than
// no message.
//
// So it lives below the figure, wraps, and can be dismissed. It comes back if
// the reason changes, because a NEW problem is worth interrupting for even when
// the last one was waved away.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root

    // The sentence to show. Empty means there is nothing wrong.
    required property string text
    // Set false for an ordinary status line, true for something the user
    // probably needs to act on.
    property bool warning: true

    // Dismissed by text, not by a flag: waving away "needs three columns" must
    // not also hide "99% of the values are in 2% of the range" when that turns
    // up next. Comparing the sentence is the simplest thing that gets that
    // right, and the sentences are generated from the data, so a genuinely
    // different problem always reads differently.
    property string dismissed: ""
    readonly property bool showing: root.text !== "" && root.text !== root.dismissed

    visible: root.showing
    height: root.showing ? implicitHeight : 0
    implicitHeight: row.implicitHeight + 16

    color: root.warning
           ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.10)
           : Theme.surface
    border.color: root.warning ? Theme.warning : Theme.border
    radius: Theme.radius

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 10

        Label {
            text: root.warning ? "!" : "i"
            color: root.warning ? Theme.warning : Theme.textMuted
            font.bold: true
            font.pixelSize: 15
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
        }

        Label {
            text: root.text
            color: Theme.text
            wrapMode: Text.WordWrap
            // The whole point: it wraps to the width it is given instead of
            // growing until it covers something.
            Layout.fillWidth: true
        }

        ToolButton {
            id: dismiss
            text: "✕"
            flat: true
            Layout.alignment: Qt.AlignTop
            onClicked: root.dismissed = root.text
            ToolTip.visible: dismiss.hovered
            ToolTip.text: "Dismiss. It returns if a different problem appears."
        }
    }
}
