// The figure's colours, one swatch at a time.
//
// Eighty-four maps and five measured palettes cover what most figures need, and
// none of them covers the figure that has to match a poster, a journal's house
// style or the other four plots in the same paper. This is the escape hatch:
// pick every colour by hand, and nothing is imposed.
//
// It edits ONE list, which the renderer reads as a ramp on a field engine and
// as a cycle on a set of series or pie sectors - see PlotStyle::customColours.
// So the same row of swatches means "the bands of the scale" on a heat map and
// "the sectors" on a pie, which is the same sentence a person would use for
// both.
//
// It starts from what is ALREADY ON SCREEN. Clicking "Choose my own" samples
// the map in use and fills the row with it, so the figure does not change at
// all at the moment it becomes editable - the only difference is that the
// colours can now be clicked. An editor that starts empty, or starts from
// black, makes the person rebuild what they already had before they can adjust
// it.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import GraphVis

ColumnLayout {
    id: root
    required property var canvas
    spacing: 8

    readonly property var colours: root.canvas ? root.canvas.customColours : []
    readonly property bool mine: root.canvas ? root.canvas.usingCustomColours : false
    // Which swatch the dialog is currently editing. -1 means it is adding one.
    property int editing: -1

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Label {
            Layout.fillWidth: true
            text: root.mine
                  ? "Your colours are in use."
                  : (root.canvas && root.canvas.usesColourMap
                     ? "The figure is using the colour map above."
                     : "The figure is using the colour-vision palette.")
            color: root.mine ? Theme.text : Theme.textMuted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
        ToolButton {
            id: seedButton
            text: root.mine ? "Reset to map" : "Choose my own"
            flat: true
            font.pixelSize: 10
            implicitHeight: 22
            ToolTip.visible: seedButton.hovered
            ToolTip.text: root.mine
                          ? "Refill the swatches from the colour map, discarding your edits"
                          : "Start from the colours on screen now, then change any of them"
            onClicked: root.canvas.seedCustomColours(root.canvas.suggestedColourCount())
        }
        ToolButton {
            id: dropButton
            text: "Use the map"
            flat: true
            font.pixelSize: 10
            implicitHeight: 22
            visible: root.mine
            ToolTip.visible: dropButton.hovered
            ToolTip.text: "Back to the named colour map and the measured palette"
            onClicked: root.canvas.clearCustomColours()
        }
    }

    // The swatches. A Flow rather than a Row: sixteen bands in a 300 px sidebar
    // is two lines, and a row would push the last of them off the edge where
    // they cannot be clicked.
    Flow {
        Layout.fillWidth: true
        visible: root.mine
        spacing: 4

        Repeater {
            model: root.colours
            delegate: Rectangle {
                id: swatch
                required property var modelData
                required property int index
                width: 30
                height: 22
                radius: 3
                color: swatch.modelData
                border.color: swatchHover.hovered ? Theme.text : Theme.border
                border.width: swatchHover.hovered ? 2 : 1

                // The position in the ramp, so a row of eight is readable as an
                // order rather than as a set.
                Label {
                    anchors.centerIn: parent
                    text: swatch.index + 1
                    font.pixelSize: 9
                    font.bold: true
                    // Legible on whatever was picked. Luminance, not a guess:
                    // white on yellow and black on navy are both unreadable,
                    // and a person WILL pick both.
                    color: (0.299 * swatch.modelData.r + 0.587 * swatch.modelData.g
                            + 0.114 * swatch.modelData.b) > 0.55 ? "#000000" : "#ffffff"
                    opacity: 0.75
                }

                HoverHandler { id: swatchHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        root.editing = swatch.index
                        picker.selectedColor = swatch.modelData
                        picker.open()
                    }
                }
                // Right-click removes it, which is the one destructive action
                // here and so is not on the same button as the useful one.
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: root.canvas.removeCustomColour(swatch.index)
                }
                ToolTip.visible: swatchHover.hovered
                ToolTip.text: String(swatch.modelData) + " — click to change, right-click to remove"
            }
        }

        ToolButton {
            id: addButton
            width: 30
            height: 22
            text: "+"
            font.pixelSize: 12
            ToolTip.visible: addButton.hovered
            ToolTip.text: "Add another colour to the end"
            onClicked: {
                root.editing = -1
                picker.selectedColor = root.colours.length > 0
                                       ? root.colours[root.colours.length - 1] : "#4f9df7"
                picker.open()
            }
        }
    }

    // Said where the choice is made, not in a document nobody opens.
    //
    // The five built-in palettes were each simulated through a dichromat
    // projection and accepted only when their worst-separated pair still
    // cleared a comfortable margin - see ColourVision.h. A hand-picked set has
    // had none of that done to it, and the most common way to produce a figure
    // a colour-blind reader cannot read is to pick pretty colours by eye.
    Label {
        Layout.fillWidth: true
        visible: root.mine
        text: "Your own colours are not checked for colour-vision safety. The "
            + "built-in palettes are measured; a hand-picked set is not."
        color: Theme.textMuted
        font.pixelSize: 9
        wrapMode: Text.WordWrap
    }

    ColorDialog {
        id: picker
        title: root.editing >= 0 ? "Colour " + (root.editing + 1) : "New colour"
        onAccepted: {
            if (root.editing >= 0) root.canvas.setCustomColour(root.editing, picker.selectedColor)
            else root.canvas.addCustomColour(picker.selectedColor)
        }
    }
}
