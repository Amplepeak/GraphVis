// Full-resolution render progress.
//
// Sits at the bottom right of the plot area, OUTSIDE the preview, and only
// appears when the render is going to take long enough to be worth saying so
// about. The estimate is measured rather than invented: PlotCanvas keeps a
// moving average of milliseconds per point and multiplies it by the points in
// the full dataset, so the first estimate on a new machine is rough and every
// one after that is not.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var canvas

    // Below this the full render lands before a badge would finish fading in,
    // and interrupting someone to say "this took two seconds" is noise.
    readonly property real quietThresholdSeconds: 2.0

    // The fade has to drive `visible`, not follow it. `opacity: visible ? 1 : 0`
    // can never animate out: the item stops rendering in the same frame the
    // condition flips, so the badge popped instead of fading.
    readonly property bool wanted:
        canvas.renderInFlight && canvas.renderEstimateSeconds >= root.quietThresholdSeconds
    opacity: root.wanted ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 160 } }

    implicitWidth: 236
    implicitHeight: 54
    radius: 8
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.94)
    border.color: Theme.borderStrong

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 9

        // The same otter as the splash, smaller.
        // The drift is animated on an Item INSIDE the layout cell, not on the
        // Image itself. A value source on the `y` of a RowLayout child takes
        // the property away from the layout, so the icon lost its vertical
        // centring and sat near the top of the row.
        Item {
            Layout.preferredWidth: 34; Layout.preferredHeight: 34
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                source: "qrc:/qt/qml/GraphVis/assets/graphvis_icon.png"
                sourceSize.width: 34; sourceSize.height: 34
                width: 34; height: 34
                fillMode: Image.PreserveAspectFit
                // A slow drift rather than a spinner: it says "working" without
                // pretending to know more about progress than the bar does.
                SequentialAnimation on anchors.verticalCenterOffset {
                    running: root.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 0; to: -3; duration: 1400; easing.type: Easing.InOutSine }
                    NumberAnimation { from: -3; to: 0; duration: 1400; easing.type: Easing.InOutSine }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: "Full resolution · " + root.canvas.renderRemainingText
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                radius: 3
                color: Theme.surfaceAlt
                Rectangle {
                    width: Math.max(6, parent.width * Math.max(0, Math.min(1, root.canvas.renderProgress)))
                    height: parent.height
                    radius: 3
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: 180 } }
                }
            }
        }

        Button {
            flat: true
            text: "✕"
            implicitWidth: 26
            ToolTip.visible: hovered
            ToolTip.text: "Stop the full-resolution render and keep the preview"
            onClicked: root.canvas.cancelFullRender()
        }
    }
}
