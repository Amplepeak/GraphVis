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
//
// THREE axes, not two. Every 3-D engine has a height and every heat map,
// contour and surface has a quantity the colour runs over, and neither had a
// scale of its own - so a 3-D figure whose z column spans four decades could
// not be logged, and the y control silently transformed the z column as well.
// The third row appears only when the chosen engine actually has a third
// mapped column, and it is named after what that column IS on that engine.
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

    // The explanation, once, under all three rows.
    //
    // It used to be a hover tooltip on each combo box, and a tooltip opens
    // BELOW its control - so the X one opened directly over the Y row and the Y
    // one over the "Both axes the same" tick, which is why that tick could not
    // be read or reliably clicked. One label in a place of its own cannot
    // cover anything, and it is readable without hovering at all.
    property int explaining: 0
    function explain(i) { root.explaining = Math.max(0, Math.min(i, root.why.length - 1)) }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label {
            text: "X scale"
            color: Theme.textSecondary
            Layout.preferredWidth: 74
            elide: Text.ElideRight
        }
        ComboBox {
            id: xBox
            Layout.fillWidth: true
            enabled: root.canvas !== null
            model: root.names
            currentIndex: root.canvas ? root.canvas.xTransform : 0
            onActivated: if (root.canvas) root.canvas.xTransform = xBox.currentIndex
            onHoveredChanged: if (xBox.hovered) root.explain(xBox.currentIndex)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label {
            text: "Y scale"
            color: Theme.textSecondary
            Layout.preferredWidth: 74
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
            onHoveredChanged: if (yBox.hovered) root.explain(yBox.currentIndex)
        }
    }

    // Z on a 3-D engine, Colour on a field engine, absent on the engines that
    // take two columns. The name comes from the canvas rather than from a list
    // here, so it cannot disagree with what the backend does with that column.
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        visible: root.canvas !== null && root.canvas.thirdAxisRole !== ""
        Label {
            text: root.canvas ? root.canvas.thirdAxisRole : ""
            color: Theme.textSecondary
            Layout.preferredWidth: 74
            elide: Text.ElideRight
        }
        ComboBox {
            id: zBox
            Layout.fillWidth: true
            enabled: root.canvas !== null
            model: root.names
            currentIndex: root.canvas ? root.canvas.zTransform : 0
            onActivated: if (root.canvas) root.canvas.zTransform = zBox.currentIndex
            onHoveredChanged: if (zBox.hovered) root.explain(zBox.currentIndex)
        }
    }

    CheckBox {
        id: linkBox
        enabled: root.canvas !== null
        checked: root.canvas ? root.canvas.linkAxisTransforms : false
        onToggled: if (root.canvas) root.canvas.linkAxisTransforms = linkBox.checked
        // The Basic style draws its label in a colour that is unreadable on
        // this panel, so the label is styled explicitly.
        contentItem: Text {
            // Says which two. With a third row above it, "both axes" had
            // stopped being unambiguous.
            text: "X and Y the same"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            verticalAlignment: Text.AlignVCenter
            leftPadding: linkBox.indicator.width + 6
        }
    }

    Label {
        Layout.fillWidth: true
        text: root.why[root.explaining]
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }
}
