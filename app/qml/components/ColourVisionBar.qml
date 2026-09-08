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
    // The live PlotCanvas, for the field colour map. Null while there is no
    // canvas - the Publication tab, say - and the row hides itself.
    property var canvas: null

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
                // "Series colours", not "Graph colours". The old label was the
                // only colour control on this panel, so it read as THE colour
                // setting - and someone looking for the colour map found five
                // colour-vision modes and reasonably concluded the eighty four
                // maps GraphVis 17 had were gone. Two settings, two names.
                text: "Series colours"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
            ComboBox {
                Layout.fillWidth: true
                model: root.app.plotColourVisionNames
                currentIndex: root.app.plotColourVision
                onActivated: root.app.plotColourVision = currentIndex
                ToolTip.visible: hovered
                ToolTip.text: "Palette for the LINES and MARKERS of every graph, on screen and in "
                            + "exports. Chosen so the series stay distinguishable under each colour "
                            + "vision deficiency. Independent of the interface theme, and remembered "
                            + "between sessions."
            }
        }

        // The field colour map: what a heat map, contour, surface or vector
        // field is coloured with. A different question from the one above, and
        // the port had no answer to it at all.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.canvas !== null
            Label {
                text: "Field colours"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
            ColourMapSelector {
                Layout.fillWidth: true
                canvas: root.canvas
                app: root.app
                // Always shown here, unlike on the plot toolbar: this bar sits
                // above the graph CHOOSER, so the map is being picked before an
                // engine that uses it has necessarily been selected.
                autoHide: false
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
            text: root.app.plotColourVisionSummary
        }

        // The figure's background and grid. Beside the colour settings because
        // it is the same question - what the picture looks like - and not the
        // same question as what the interface looks like.
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        FigureStyleBar { app: root.app; Layout.fillWidth: true }
    }
}
