// How each axis's values are scaled before they are drawn.
//
// A log toggle was the whole of this before, and a log axis is one answer to
// one shape of problem. A column that is 97% zeros cannot be logged at all -
// every one of those rows is dropped - and two columns in different units
// cannot be compared on any axis until they are standardised. Those are the
// two cases that come up most and neither had a control.
//
// Independent per axis, because a skewed x against a standardised y is an
// ordinary thing to want. Linked when it is not.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ColumnLayout {
    id: root
    // The live PlotCanvas.
    required property var canvas
    spacing: 6

    readonly property var names: root.canvas ? root.canvas.axisTransformNames() : []
    // What each one is FOR, said where the person is choosing between them
    // rather than in documentation nobody opens.
    readonly property var why: [
        "The values as measured.",
        "Decade ticks, and the numbers on the axis stay in the original units. "
      + "Values of zero or less cannot be drawn and are dropped.",
        "log10(1 + x). The log axis for a column that legitimately reaches zero — "
      + "counts, concentrations, failures — where a plain log axis has to throw those rows away.",
        "(value − mean) ÷ standard deviation. Puts two columns of different units on "
      + "the same axis, measured in standard deviations from their own mean.",
        "Each value replaced by its position in the sorted sample, 0 to 1. Spreads a "
      + "heavily skewed column out evenly, so a cluster near zero stops being one pixel."
    ]

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label {
            text: "X scale"
            color: Theme.textSecondary
            Layout.preferredWidth: 58
            elide: Text.ElideRight
        }
        ComboBox {
            id: xBox
            Layout.fillWidth: true
            enabled: root.canvas !== null
            model: root.names
            currentIndex: root.canvas ? root.canvas.xTransform : 0
            onActivated: if (root.canvas) root.canvas.xTransform = xBox.currentIndex
            ToolTip.visible: xBox.hovered
            ToolTip.text: root.why[Math.max(0, Math.min(xBox.currentIndex, root.why.length - 1))]
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label {
            text: "Y scale"
            color: Theme.textSecondary
            Layout.preferredWidth: 58
            elide: Text.ElideRight
        }
        ComboBox {
            id: yBox
            Layout.fillWidth: true
            // Disabled rather than hidden while linked: a control that vanishes
            // leaves you wondering where it went, where a greyed one with the
            // right value in it says plainly that it is following X.
            enabled: root.canvas !== null && !linkBox.checked
            model: root.names
            currentIndex: root.canvas ? root.canvas.yTransform : 0
            onActivated: if (root.canvas) root.canvas.yTransform = yBox.currentIndex
            ToolTip.visible: yBox.hovered
            ToolTip.text: root.why[Math.max(0, Math.min(yBox.currentIndex, root.why.length - 1))]
        }
    }

    CheckBox {
        id: linkBox
        enabled: root.canvas !== null
        checked: root.canvas ? root.canvas.linkAxisTransforms : false
        onToggled: if (root.canvas) root.canvas.linkAxisTransforms = checked
        // The Basic style draws its label in a colour that is unreadable on
        // this panel, so the label is styled explicitly.
        contentItem: Text {
            text: "Both axes the same"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            verticalAlignment: Text.AlignVCenter
            leftPadding: linkBox.indicator.width + 6
        }
    }
}
