// The figure's own appearance: its background, and its grid.
//
// Both used to follow the interface theme and nothing else, which makes one
// common thing impossible - working in a dark interface while producing a white
// figure for a paper. They are different questions and the answer to one is not
// the answer to the other.
//
// The grid is here for the same reason. Some figures are read off the grid and
// want it fine; a figure going into a publication usually wants it gone.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ColumnLayout {
    id: root
    required property var app
    spacing: 5

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label {
            text: "Figure background"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
        }
        ComboBox {
            id: bg
            Layout.fillWidth: true
            model: root.app.figureThemeNames
            currentIndex: root.app.figureTheme
            onActivated: root.app.figureTheme = bg.currentIndex
            ToolTip.visible: bg.hovered
            ToolTip.text: "The background of the PLOT, independent of the interface theme. "
                        + "White is what most journals want; the interface can stay dark."
        }
    }

    // The rest of the range. The combo above is four presets; this is every
    // other colour, and choosing one from here is what selects "Custom colour"
    // in the combo.
    FigureBackgroundPicker { app: root.app; Layout.fillWidth: true }

    // The grid: on or off, and how many lines each way.
    //
    // This was one box stretched across the whole panel for a number that is
    // never more than two digits, and it drove BOTH axes - the vertical derived
    // as "one fewer". A wide time series wants many ticks across and few up, a
    // tall profile wants the reverse, and neither could be asked for. Two small
    // boxes, one per axis, in the width the old single box wasted.
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        CheckBox {
            id: gridOn
            checked: root.app.plotGridVisible
            onToggled: root.app.plotGridVisible = gridOn.checked
            contentItem: Text {
                text: "Grid"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                verticalAlignment: Text.AlignVCenter
                leftPadding: gridOn.indicator.width + 6
            }
        }
        Label {
            text: "across"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            enabled: gridOn.checked
        }
        SpinBox {
            id: acrossBox
            enabled: gridOn.checked
            from: 0
            to: 25
            value: root.app.plotGridDensity
            editable: true
            // Wide enough for "Auto" and the arrows, and no wider. Filling the
            // row is what made this look like the panel's main control.
            Layout.preferredWidth: 84
            onValueChanged: root.app.plotGridDensity = acrossBox.value
            // 0 is not "no grid" - the checkbox is - it is "whatever the
            // renderer would have chosen", which is 7 across and 6 up.
            textFromValue: function(v) { return v === 0 ? "Auto" : String(v) }
            valueFromText: function(t) { return t.toLowerCase() === "auto" ? 0 : parseInt(t) || 0 }
            ToolTip.visible: acrossBox.hovered
            ToolTip.text: "Roughly how many labelled ticks the x axis aims for, and so how "
                        + "many vertical grid lines. Auto is 7. The values are still rounded "
                        + "to readable numbers, so this is a target rather than a count."
        }
        Label {
            text: "up"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            enabled: gridOn.checked
        }
        SpinBox {
            id: upBox
            enabled: gridOn.checked
            from: 0
            to: 25
            value: root.app.plotGridDensityY
            editable: true
            Layout.preferredWidth: 84
            onValueChanged: root.app.plotGridDensityY = upBox.value
            // Auto here means "follow across, one fewer", which is the ratio a
            // single number used to force - so a figure that never touches this
            // box looks exactly as it did.
            textFromValue: function(v) { return v === 0 ? "Auto" : String(v) }
            valueFromText: function(t) { return t.toLowerCase() === "auto" ? 0 : parseInt(t) || 0 }
            ToolTip.visible: upBox.hovered
            ToolTip.text: "Roughly how many labelled ticks the y axis aims for, and so how "
                        + "many horizontal grid lines. Auto follows the count across, one "
                        + "fewer — the 7 and 6 the figure has always used."
        }
        Item { Layout.fillWidth: true }
    }
}
