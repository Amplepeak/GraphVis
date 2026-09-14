// What the FIGURE looks like: its field colour map, its background, its grid.
//
// This is the half of the old ColourVisionBar that stayed in the sidebar. The
// colour-vision setting left for the View menu and an optional strip, because
// it applies to very few people and was taking permanent space from the graph
// library; these three do not - a background and a grid are settings almost
// every figure gets adjusted, and the field colour map is the reading of any
// heat map, contour or surface.
//
// All three are about the picture rather than about the interface, which is why
// they are together and why none of them follows the interface theme.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ColumnLayout {
    id: root
    required property var app
    // The live PlotCanvas. Null on a layout without one - the Publication tab,
    // say - and the colour-map row hides itself.
    property var canvas: null
    spacing: 6

    // The field colour map: what a heat map, contour, surface or vector field
    // is coloured with. A different question from the series palette, and the
    // port had no answer to it at all until the eighty four maps were restored.
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        visible: root.canvas !== null
        Label {
            text: "Field colours"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            ToolTip.visible: fieldHelp.hovered
            ToolTip.text: "The colour a heat map, contour, surface or vector field runs "
                        + "over its values. The lines and markers of an ordinary graph "
                        + "take their colours from the colour-vision setting instead - "
                        + "View ▸ Colour vision."
            HoverHandler { id: fieldHelp }
        }
        ColourMapSelector {
            // Sized to the longest map name by the selector itself, so it is
            // not stretched across the panel with nothing in the extra space.
            canvas: root.canvas
            app: root.app
            // Always shown here, unlike on the plot toolbar: this panel sits
            // above the graph CHOOSER, so the map is being picked before an
            // engine that uses it has necessarily been selected.
            autoHide: false
        }
        Item { Layout.fillWidth: true }
    }

    // The map the figure is actually being PAINTED in, when a colour-vision
    // mode has replaced the chosen one. Silence here is what made the setting
    // look broken, so the substitution is stated where the map is chosen.
    Label {
        Layout.fillWidth: true
        visible: root.canvas !== null
                 && root.canvas.effectiveColourMap !== root.canvas.colourMap
        wrapMode: Text.WordWrap
        font.pixelSize: 9
        color: Theme.accent
        text: root.canvas
              ? "Drawn in " + root.canvas.effectiveColourMap + " for the colour-vision "
                + "setting. Your choice is kept."
              : ""
    }

    // How a pie or donut names its slices. Shown only on those two engines:
    // everywhere else it is a control that does nothing, which is worse than
    // no control because it is one more thing to try before believing it.
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        visible: root.canvas !== null
                 && (root.canvas.engine === "Pie" || root.canvas.engine === "Donut")
        Label {
            text: "Slice names"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
        }
        ComboBox {
            id: pieLabelBox
            Layout.fillWidth: true
            Layout.maximumWidth: 220
            model: root.app.plotPieLabelNames
            currentIndex: root.app.plotPieLabels
            onActivated: root.app.plotPieLabels = pieLabelBox.currentIndex
            ToolTip.visible: pieLabelBox.hovered
            ToolTip.text: "A pie has no axis to read names off, so it says which slice "
                        + "is which here. Labels sit on the slices and read well when "
                        + "there are few with short names; a legend keeps the circle "
                        + "clean and copes with many. A slice too thin to hold its "
                        + "label is left to the legend."
        }
    }

    // Which way round a polar figure is measured. Shown only on the engines
    // drawn in a circle, for the same reason the slice-names row is shown only
    // on a pie.
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        visible: root.canvas !== null && root.canvas.polarEngine
        Label {
            text: "Angles"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
        }
        ComboBox {
            id: polarBox
            Layout.fillWidth: true
            Layout.maximumWidth: 260
            model: root.app.plotPolarConventionNames
            currentIndex: root.app.plotPolarConvention
            onActivated: root.app.plotPolarConvention = polarBox.currentIndex
            ToolTip.visible: polarBox.hovered
            ToolTip.text: "A bearing is read from north, clockwise — a wind rose, a "
                        + "compass, a stereonet. A polar line or scatter is "
                        + "mathematical: zero to the right, increasing anticlockwise. "
                        + "Neither is right for every figure, so this is yours to set."
        }
    }

    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

    // The figure's background and grid.
    FigureStyleBar { app: root.app; Layout.fillWidth: true }
}
