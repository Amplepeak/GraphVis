// One axis's range: what it is, two boxes to type it in, and a bar to drag it.
//
// The figure could always be zoomed by dragging it, which answers "a bit
// closer" and not "VHPR above ten is not what I am looking at" - and on the
// 3-D family, which has no drag-zoom at all, there was no way to say either.
// Reported as "I can't find where to cap the axis".
//
// Three controls for one range, deliberately:
//
//   the boxes   an exact number, which is what a figure for a paper needs and
//               what a slider can never quite land on
//   the bar     the range as a PROPORTION of the data, which is what you want
//               while you are still deciding - and which shows at a glance how
//               much of the column you are looking at
//   Auto        back to the whole column, per axis, without having to remember
//               what the whole column was
//
// The bar's travel is the column's own span, which is why the canvas reports
// dataMin and dataMax: a slider that does not know what "all the way left"
// means is a slider that cannot be drawn.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ColumnLayout {
    id: root

    // One entry of PlotCanvas.axisRanges, or the same shape from colourRange.
    required property var entry
    // "Z", "Y", "Colour" - what to call this row.
    property string heading: root.entry && root.entry.name ? root.entry.name : ""
    // Emitted when the person has settled on a range. NaN means "fit this end".
    signal limitsRequested(real lo, real hi)
    signal autoRequested()

    readonly property bool auto: root.entry ? (root.entry.autoMin && root.entry.autoMax) : true
    readonly property real dataLo: root.entry ? Number(root.entry.dataMin) : 0
    readonly property real dataHi: root.entry ? Number(root.entry.dataMax) : 1
    readonly property real curLo: root.entry ? Number(root.entry.min) : 0
    readonly property real curHi: root.entry ? Number(root.entry.max) : 1

    // The bar's travel. Normally the column, widened when a typed limit sits
    // outside it - a number you typed must be somewhere on the bar, or the
    // handle parks at the end and the two controls disagree about the figure.
    readonly property real travelLo: Math.min(root.dataLo, root.curLo)
    readonly property real travelHi: Math.max(root.dataHi, root.curHi)

    // What to show in a box: enough figures to be exact without being noise.
    // Four significant figures of the SPAN, so a range of 0..160 reads 10 and a
    // range of 0..0.002 still reads 0.0012.
    function fmt(v) {
        if (!isFinite(v)) return ""
        var span = Math.abs(root.travelHi - root.travelLo)
        if (!(span > 0)) return String(v)
        var decimals = Math.max(0, Math.min(9, 3 - Math.floor(Math.log(span) / Math.LN10)))
        return Number(v).toFixed(decimals)
    }

    spacing: 3

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Label {
            text: root.heading
            color: Theme.textSecondary
            font.pixelSize: 10
            font.bold: true
        }
        Label {
            // The column this axis is drawn from, so a row of three says which
            // is which without anybody having to remember the mapping.
            text: root.entry && root.entry.label ? root.entry.label : ""
            color: Theme.textMuted
            font.pixelSize: 10
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Label {
            visible: !root.auto
            text: "capped"
            color: Theme.accent
            font.pixelSize: 9
            font.bold: true
        }
        ToolButton {
            id: autoButton
            text: "Auto"
            flat: true
            font.pixelSize: 10
            enabled: !root.auto
            implicitHeight: 20
            ToolTip.visible: autoButton.hovered
            ToolTip.text: "Fit this axis back to the whole column"
            onClicked: root.autoRequested()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4

        // A box that does not fight you while you are typing in it.
        //
        // Binding the text straight to the canvas would rewrite the field on
        // every frame of a slider drag - including the frame in the middle of
        // typing "10" where the value is still 1. The text follows the canvas
        // only while the box is not focused.
        TextField {
            id: minField
            Layout.fillWidth: true
            Layout.preferredWidth: 10
            font.pixelSize: 11
            horizontalAlignment: Text.AlignRight
            validator: DoubleValidator {}
            placeholderText: "min"
            text: root.fmt(root.curLo)
            onActiveFocusChanged: if (!activeFocus) text = root.fmt(root.curLo)
            onAccepted: root.commitFields()
            onEditingFinished: root.commitFields()
        }
        Label { text: "…"; color: Theme.textMuted; font.pixelSize: 11 }
        TextField {
            id: maxField
            Layout.fillWidth: true
            Layout.preferredWidth: 10
            font.pixelSize: 11
            horizontalAlignment: Text.AlignRight
            validator: DoubleValidator {}
            placeholderText: "max"
            text: root.fmt(root.curHi)
            onActiveFocusChanged: if (!activeFocus) text = root.fmt(root.curHi)
            onAccepted: root.commitFields()
            onEditingFinished: root.commitFields()
        }
    }

    function commitFields() {
        var lo = parseFloat(minField.text)
        var hi = parseFloat(maxField.text)
        // An empty box is "fit this end", which is how one end alone is
        // released without having to release both.
        if (minField.text.trim().length === 0) lo = Number.NaN
        if (maxField.text.trim().length === 0) hi = Number.NaN
        // A pair the wrong way round is put back rather than silently swapped:
        // swapping what somebody typed hides the typo instead of showing it.
        if (isFinite(lo) && isFinite(hi) && !(hi > lo)) {
            minField.text = root.fmt(root.curLo)
            maxField.text = root.fmt(root.curHi)
            return
        }
        root.limitsRequested(lo, hi)
    }

    RangeSlider {
        id: bar
        Layout.fillWidth: true
        Layout.preferredHeight: 22
        from: root.travelLo
        to: root.travelHi
        // Live, because the whole point of a bar over a box is watching the
        // figure change as you move it. The canvas draws in draft while a
        // gesture is in flight, the same as it does for a pan or a pinch.
        first.value: root.curLo
        second.value: root.curHi
        first.onMoved: root.limitsRequested(bar.first.value, bar.second.value)
        second.onMoved: root.limitsRequested(bar.first.value, bar.second.value)
        ToolTip.visible: bar.hovered
        ToolTip.text: "Drag either end. The bar spans " + root.fmt(root.travelLo)
                      + " to " + root.fmt(root.travelHi) + ", which is what this column holds."
    }
}
