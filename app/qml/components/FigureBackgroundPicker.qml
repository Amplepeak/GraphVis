// Any colour at all for the figure's background.
//
// The four presets - follow the theme, Dark, Light, White - cover the common
// cases and cannot cover a house style, a poster printed on coloured board, or
// a slide deck whose background is already decided. This is the rest of the
// range: a hex field for a colour someone has been given, and the system
// picker for everything else.
//
// THERE WAS A GRID OF TWENTY-FOUR SWATCHES HERE and it has been removed. It
// held two rows of permanent panel height to offer a fixed set of grounds -
// six near-whites, six greys, six darks, six tinted papers - most of which are
// hard to tell apart at 20 pixels square, and every one of which the picker
// or the preset drop-down above already reaches. Panel height is the scarce
// thing in this sidebar; a quicker route to a choice nobody was making is not
// worth two rows of it.
//
// The axis text and grid are NOT picked here. They are derived from whichever
// background is chosen - see AppController::figureForeground - because a
// background picker that leaves the figure with unreadable axes has not
// finished the job, and nobody wants to choose a matching ink for every colour
// they try.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GraphVis

ColumnLayout {
    id: root
    required property var app
    spacing: 6

    // True while the figure is actually drawn in the custom colour. The
    // read-out, the hex field's contents, the way back to White and the note
    // about derived ink all hang off it, because on a preset every one of them
    // would be describing a colour the figure is not using.
    readonly property bool custom: root.app.figureTheme === 4

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label {
            text: "Background colour"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
        }
        Item { Layout.fillWidth: true }
        // What the figure is actually drawn on, whichever way it was chosen -
        // a preset or a swatch. Reading it off the canvas would be better still
        // but the canvas does not exist on every layout this panel appears in.
        Rectangle {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 16
            radius: 3
            color: root.custom ? root.app.figureBackground : "transparent"
            visible: root.custom
            border.color: Theme.border
        }
        Label {
            visible: root.custom
            text: String(root.app.figureBackground).toUpperCase()
            color: Theme.textMuted
            font.pixelSize: 9
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        TextField {
            id: hex
            Layout.fillWidth: true
            placeholderText: "#rrggbb"
            font.pixelSize: Theme.fontSizeSmall
            // Not bound to the property. A field that rewrites itself from the
            // model on every keystroke cannot be typed into: "#1" is not a
            // colour, the binding would replace it, and the second character
            // would never arrive.
            text: root.custom ? String(root.app.figureBackground) : ""
            // Six hex digits with the hash optional, so a colour pasted from a
            // style guide in either form is accepted.
            validator: RegularExpressionValidator {
                regularExpression: /#?[0-9A-Fa-f]{6}/
            }
            onAccepted: {
                var v = hex.text.trim()
                if (v.length === 6) v = "#" + v
                if (v.length === 7) root.app.figureBackground = v
            }
            ToolTip.visible: hex.hovered
            ToolTip.text: "A colour as six hex digits, with or without the leading #. "
                        + "Press Enter to apply it."
        }

        Button {
            text: "Pick…"
            font.pixelSize: Theme.fontSizeSmall
            onClicked: {
                picker.selectedColor = root.custom ? root.app.figureBackground : "#ffffff"
                picker.open()
            }
            ToolTip.visible: hovered
            ToolTip.text: "The full colour picker."
        }

        // Back to a preset without having to find the combo above. Choosing a
        // colour switches the figure to Custom, so there has to be a way back.
        Button {
            text: "White"
            font.pixelSize: Theme.fontSizeSmall
            visible: root.custom
            onClicked: root.app.figureTheme = 3
            ToolTip.visible: hovered
            ToolTip.text: "Back to the plain white preset."
        }
    }

    // What the choice implies for the rest of the figure, said rather than left
    // to be discovered. Only while a custom colour is in force: on a preset it
    // would be noise.
    Label {
        Layout.fillWidth: true
        visible: root.custom
        wrapMode: Text.WordWrap
        font.pixelSize: 9
        color: Theme.textMuted
        text: "Axis text and grid lines are chosen to stay readable on this "
            + "background. The series and field colours are set separately."
    }

    ColorDialog {
        id: picker
        title: "Figure background"
        onAccepted: root.app.figureBackground = picker.selectedColor
    }
}
