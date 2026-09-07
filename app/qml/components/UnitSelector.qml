// A one-axis display-unit chooser.
//
// GraphVis 17 read the unit out of a column label and offered the conversions
// that unit has. This is that, on the plot toolbar: pick Pa for a column
// labelled "Pressure [kPa]" and the axis is rescaled and relabelled. The data
// on disk is not touched, and nothing is re-imported.
//
// It hides itself when the axis column carries no convertible unit, which for
// most datasets is both axes - so the toolbar stays as it was unless the unit
// is actually there to use.
import QtQuick
import QtQuick.Controls
import GraphVis

ComboBox {
    id: root

    // "X" or "Y". Which canvas property this selector drives.
    required property string axis
    required property var canvas

    readonly property string sourceUnit: root.axis === "X" ? root.canvas.xSourceUnit
                                                           : root.canvas.ySourceUnit
    readonly property var targets: root.sourceUnit
        ? root.canvas.unitOptions(root.axis === "X" ? root.canvas.xColumn
                                                    : (root.canvas.yColumns.length > 0
                                                       ? root.canvas.yColumns[0] : ""))
        : []

    visible: root.targets.length > 0
    // A hidden ComboBox must not reserve toolbar width.
    width: visible ? implicitWidth : 0

    // The first entry restores the file's own unit, so the conversion is always
    // reversible without knowing what it was.
    model: root.sourceUnit ? [root.sourceUnit].concat(root.targets) : []

    ToolTip.visible: hovered
    ToolTip.text: root.axis + " axis unit. The data is unchanged; only the axis is rescaled."

    onActivated: (index) => {
        var unit = index === 0 ? "" : root.model[index]
        if (root.axis === "X") root.canvas.xUnit = unit
        else root.canvas.yUnit = unit
    }
}
