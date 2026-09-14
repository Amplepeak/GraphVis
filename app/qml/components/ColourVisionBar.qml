// The colour-vision strip: OPTIONAL, and off until someone asks for it.
//
// This was a permanent panel in the sidebar, above the graph library. It was
// reported as taking valuable space for a setting that applies to very few
// people, which is right - so the setting now lives in the View menu, and this
// is the strip for the people who do change it often. View ▸ Colour vision ▸
// Show the colour-vision toolbar turns it on, and the choice is remembered.
//
// Two separate things get stated here, because conflating them is exactly the
// mistake to avoid:
//
//   - whether the *interface theme* is one of the colour vision themes;
//   - which palette the *graphs* use, which is its own persisted setting.
//
// The second is the one that matters for a figure that leaves the application,
// and it deliberately does not follow the theme: someone who needs
// deuteranopia-safe series needs them whichever theme they happen to like.
//
// The figure's background, grid and field colour map are NOT here. They are
// settings nearly everyone touches, so they stayed in the sidebar where they
// always were - see FigureAppearanceBar.qml.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var app
    // The live PlotCanvas, for the note about what the setting did to the
    // colour map. Null on a layout with no canvas, and the note hides itself.
    property var canvas: null

    implicitHeight: content.implicitHeight + 10
    color: root.app.plotColourVision > 0 || Theme.cvd !== "" ? Theme.surfaceAlt : Theme.surface
    border.color: root.app.plotColourVision > 0 || Theme.cvd !== "" ? Theme.accent : Theme.border
    radius: Theme.radius

    ColumnLayout {
        id: content
        anchors.left: parent.left; anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10; anchors.rightMargin: 8
        spacing: 3

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: Theme.cvd !== "" ? "◉" : "○"
                color: Theme.cvd !== "" ? Theme.accent : Theme.textMuted
                // "INTERFACE theme", said explicitly.
                //
                // This marker is about the panels, menus and buttons; the combo
                // beside it is about the graph. Reading "Standard theme — no
                // colour vision adjustment" next to a box saying "Deuteranopia
                // (green-blind)" was reported as the setting not working, which
                // is exactly the conflation the note at the top of this file
                // says to avoid: two sentences, each true about a different
                // thing, and neither saying which.
                ToolTip.visible: themeHelp.hovered
                ToolTip.text: Theme.cvd !== ""
                              ? "Interface theme — colourblind, " + Theme.cvdLabelCurrent
                              : "Interface theme — standard. The graph colours here are "
                              + "set separately."
                HoverHandler { id: themeHelp }
            }

            Label {
                // "Series colours", not "Graph colours". The old label was the
                // only colour control on the panel, so it read as THE colour
                // setting - and someone looking for the colour map found five
                // colour-vision modes and reasonably concluded the eighty four
                // maps GraphVis 17 had were gone. Two settings, two names.
                text: "Series colours"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                ToolTip.visible: seriesHelp.hovered
                ToolTip.text: "The LINES and MARKERS of a graph. A heat map, a contour "
                            + "or a 3-D surface is coloured by the field map instead - "
                            + "that one follows this setting too, but only when the map "
                            + "it is using fails for the vision chosen."
                HoverHandler { id: seriesHelp }
            }

            ComboBox {
                Layout.fillWidth: true
                Layout.maximumWidth: 280
                model: root.app.plotColourVisionNames
                currentIndex: root.app.plotColourVision
                onActivated: root.app.plotColourVision = currentIndex
                ToolTip.visible: hovered
                ToolTip.text: "Palette for every graph, on screen and in exports. Chosen so "
                            + "the series stay distinguishable under each colour vision "
                            + "deficiency. Independent of the interface theme, and "
                            + "remembered between sessions."
            }

            // The setting turned into something you can look at. Disabled on
            // Standard, where there is no deficiency to simulate.
            CheckBox {
                id: previewBox
                enabled: root.app.plotColourVision > 0
                checked: root.app.plotColourVisionPreview
                onToggled: root.app.plotColourVisionPreview = previewBox.checked
                contentItem: Text {
                    text: "Preview as they see it"
                    color: previewBox.enabled ? Theme.textSecondary : Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: previewBox.indicator.width + 6
                }
                ToolTip.visible: previewBox.hovered
                ToolTip.text: "Draw the figure as a reader with this deficiency receives "
                            + "it. A view only — what you export is the real figure."
            }

            Item { Layout.fillWidth: true }

            // Off again, without going back to the menu to find the switch that
            // turned it on.
            ToolButton {
                text: "✕"
                font.pixelSize: Theme.fontSizeSmall
                onClicked: root.app.colourVisionToolbarVisible = false
                ToolTip.visible: hovered
                ToolTip.text: "Hide this strip. View ▸ Colour vision brings it back."
            }
        }

        // What the setting did to the figure on screen - INCLUDING when the
        // answer is that it left the colour map alone because that map already
        // passes. Saying nothing in that case is what made a Monochrome
        // selection on a Plasma surface look like a broken setting.
        Label {
            Layout.fillWidth: true
            visible: root.canvas !== null && root.canvas.simulatingColourVision
            wrapMode: Text.WordWrap
            font.pixelSize: 9
            color: Theme.accent
            // Said every time, because a simulated figure that is not labelled
            // as one is a picture of the data that is not the data.
            text: "Simulated view — this is not the figure you will export."
        }

        Label {
            Layout.fillWidth: true
            visible: text !== ""
            wrapMode: Text.WordWrap
            font.pixelSize: 9
            color: Theme.textMuted
            text: root.canvas && root.canvas.colourVisionNote !== ""
                  ? root.canvas.colourVisionNote
                  : root.app.plotColourVisionSummary
        }
    }
}
