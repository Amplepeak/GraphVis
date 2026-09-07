// The small translucent status label used over the plot and in the shell.
//
// Four sites built this by hand, each repeating the same 0.69 alpha and the
// same implicitWidth + 20 sizing. The alpha is a Theme token now rather than a
// number that has to be matched by eye.
import QtQuick
import QtQuick.Controls
import GraphVis

Rectangle {
    id: root
    property alias text: label.text
    property color textColor: Theme.textSecondary

    implicitWidth: label.implicitWidth + 20
    implicitHeight: 30
    radius: 5
    color: Qt.rgba(Theme.surfaceAlt.r, Theme.surfaceAlt.g, Theme.surfaceAlt.b, Theme.overlayAlpha)
    border.color: Theme.borderStrong

    Label {
        id: label
        anchors.centerIn: parent
        color: root.textColor
    }
}
