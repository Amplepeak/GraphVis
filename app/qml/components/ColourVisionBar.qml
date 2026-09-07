// Colour vision status and control, shown above the visualisation selection.
//
// Two separate things get stated here, because conflating them is exactly the
// mistake to avoid:
//
//   - whether the *interface theme* is one of the colour vision themes, and
//     which deficiency it is for;
//   - which palette the *graphs* use, which is its own persisted setting.
//
// The second is the one that matters for a figure that leaves the application,
// and it deliberately does not follow the theme: someone who needs
// deuteranopia-safe series needs them whichever theme they happen to like, and
// needs the choice to stay put.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var app

    implicitHeight: content.implicitHeight + 14
    color: root.app.plotColourVision > 0 || Theme.cvd !== "" ? Theme.surfaceAlt : "transparent"
    border.color: root.app.plotColourVision > 0 || Theme.cvd !== "" ? Theme.accent : Theme.border
    radius: Theme.radius

    ColumnLayout {
        id: content
        anchors.left: parent.left; anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10; anchors.rightMargin: 10
        spacing: 5

        // The theme's own status, stated plainly rather than left to be inferred
        // from a theme name.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: Theme.cvd !== "" ? "◉" : "○"
                color: Theme.cvd !== "" ? Theme.accent : Theme.textMuted
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.cvd !== "" ? Theme.text : Theme.textMuted
                text: Theme.cvd !== ""
                      ? "Colourblind theme selected — " + Theme.cvdLabelCurrent
                      : "Standard theme — no colour vision adjustment"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "Graph colours"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
            ComboBox {
                Layout.fillWidth: true
                model: root.app.plotColourVisionNames
                currentIndex: root.app.plotColourVision
                onActivated: root.app.plotColourVision = currentIndex
                ToolTip.visible: hovered
                ToolTip.text: "Series palette for every graph, on screen and in exports. "
                            + "Independent of the interface theme, and remembered between sessions."
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
            text: root.app.plotColourVisionSummary
        }
    }
}
