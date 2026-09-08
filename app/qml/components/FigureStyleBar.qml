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

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
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
            text: "Lines"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            enabled: gridOn.checked
        }
        SpinBox {
            id: density
            enabled: gridOn.checked
            from: 0
            to: 25
            value: root.app.plotGridDensity
            editable: true
            Layout.fillWidth: true
            onValueChanged: root.app.plotGridDensity = density.value
            // 0 is not "no grid" - the checkbox is - it is "whatever the
            // renderer would have chosen", which is 7 across and 6 up.
            textFromValue: function(v) { return v === 0 ? "Auto" : String(v) }
            valueFromText: function(t) { return t.toLowerCase() === "auto" ? 0 : parseInt(t) || 0 }
            ToolTip.visible: density.hovered
            ToolTip.text: "Roughly how many labelled ticks each axis aims for, and so how many "
                        + "grid lines. Auto is 7 across and 6 up. The values are still rounded "
                        + "to readable numbers, so this is a target rather than a count."
        }
    }
}
