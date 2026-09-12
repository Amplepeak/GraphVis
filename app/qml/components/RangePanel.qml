// What part of the data the figure shows, and what part of the ramp its
// colours run over.
//
// Two groups rather than one, because they answer different questions and are
// changed at different moments. The axis limits decide WHICH POINTS are drawn -
// capping VHPR at ten is a statement about what you are looking at. The colour
// range decides how the points that are drawn are SHADED - it moves no data and
// hides none, it decides whether the body of a distribution gets the whole ramp
// or the bottom eighth of it. Reported together, so they are offered together
// and kept apart.
//
// A row per axis the engine actually reads. A line chart shows two, a 3-D
// scatter three, a pie chart none - the canvas says which, so this cannot offer
// a control for an axis the figure has not got.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ColumnLayout {
    id: root
    required property var canvas
    spacing: 8

    readonly property var ranges: root.canvas ? root.canvas.axisRanges : []
    readonly property var colour: root.canvas ? root.canvas.colourRange : ({})
    readonly property bool anyCapped: {
        var r = root.ranges
        for (var i = 0; i < r.length; ++i)
            if (r[i].used && (!r[i].autoMin || !r[i].autoMax)) return true
        return false
    }

    GvGroupBox {
        title: "Axis range"
        Layout.fillWidth: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                visible: !root.canvas || root.ranges.length === 0
                         || !(root.ranges[0] && root.ranges[0].used)
                text: "This figure has no axes to cap — a pie, a treemap and a "
                    + "network are drawn from their values rather than against a scale."
                color: Theme.textMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: root.ranges
                delegate: LimitRow {
                    required property var modelData
                    Layout.fillWidth: true
                    visible: modelData.used
                    entry: modelData
                    heading: modelData.name
                    onLimitsRequested: (lo, hi) => root.canvas.setAxisLimits(modelData.role, lo, hi)
                    onAutoRequested: root.canvas.clearAxisLimits(modelData.role)
                }
            }

            Button {
                id: clearAll
                Layout.fillWidth: true
                text: "Show everything again"
                enabled: root.anyCapped
                ToolTip.visible: clearAll.hovered
                ToolTip.text: "Every axis back to the full column, and the colour "
                            + "scale back to the data. The camera and any notes are left alone."
                onClicked: root.canvas.clearAllLimits()
            }
        }
    }

    GvGroupBox {
        title: "Colour scale"
        Layout.fillWidth: true
        visible: root.colour && root.colour.used === true

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            LimitRow {
                Layout.fillWidth: true
                visible: root.colour.known === true
                entry: root.colour
                heading: "Colour"
                onLimitsRequested: (lo, hi) => root.canvas.setColourLimits(lo, hi)
                onAutoRequested: root.canvas.clearColourLimits()
            }

            // What happens to a value outside that range. Beside the range
            // rather than under the colour map, because it is a statement
            // about the range and means nothing without one.
            CheckBox {
                id: dropBox
                Layout.fillWidth: true
                visible: root.colour.known === true
                enabled: root.colour.autoMin !== true || root.colour.autoMax !== true
                checked: root.colour.dropOutOfRange === true
                text: "Hide values outside this range"
                font.pixelSize: 10
                // Assigned, not called. setColourOutOfRangeDropped is a
                // property WRITE accessor and not a slot, so QML cannot invoke
                // it as a method - "Property ... is not a function" at run
                // time, on a control that looks perfectly fine in the source.
                // Nothing caught it because `canvas` is a var, so qmllint has
                // no type to check members against.
                onToggled: root.canvas.colourOutOfRangeDropped = dropBox.checked
                ToolTip.visible: dropBox.hovered
                ToolTip.text: dropBox.enabled
                              ? "Off, a value past either end is drawn in the colour at that "
                              + "end — every point stays on screen and the tail saturates. On, "
                              + "it is not drawn at all, so the figure shows only the band you "
                              + "asked for. Say which you did in the caption."
                              : "Set a colour range first — there is nothing outside an automatic one."
            }

            Label {
                Layout.fillWidth: true
                visible: root.colour.known !== true
                text: "This engine colours by something it works out itself — the "
                    + "speed of a vector field, the area of a cell — so there is no "
                    + "column to set a range against."
                color: Theme.textMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            // How many colours, which is the other half of "I want more and
            // less". A continuous ramp is a photograph and reads as smooth; a
            // handful of bands is a contour map and lets a reader say which
            // band a region is in. The colour bar is drawn through the same
            // function, so the key bands with the figure.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: "Steps"
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: root.colour.levels > 0
                          ? root.colour.levels + " bands"
                          : "continuous"
                    color: root.colour.levels > 0 ? Theme.accent : Theme.textMuted
                    font.pixelSize: 10
                }
                ToolButton {
                    id: smoothButton
                    text: "Smooth"
                    flat: true
                    font.pixelSize: 10
                    implicitHeight: 20
                    enabled: root.colour.levels > 0
                    ToolTip.visible: smoothButton.hovered
                    ToolTip.text: "Back to a continuous ramp"
                    onClicked: root.canvas.setColourLevels(0)
                }
            }
            Slider {
                id: levels
                Layout.fillWidth: true
                from: 0
                to: 24
                stepSize: 1
                snapMode: Slider.SnapAlways
                // 0 is the continuous ramp and 1 is a figure painted one
                // colour, which nobody means - so the bar steps from 0 to 2.
                value: root.colour.levels
                onMoved: root.canvas.setColourLevels(levels.value < 2 ? 0 : levels.value)
                ToolTip.visible: levels.hovered
                ToolTip.text: "Left for a continuous ramp, right for that many discrete bands"
            }
        }
    }

    // Complete control, for the figure none of the eighty-four maps suits.
    //
    // Its own group and shown for EVERY engine, not only the ones with a ramp:
    // a pie chart has no colour scale to set a range on and its sectors are
    // exactly the thing somebody wants to colour by hand.
    GvGroupBox {
        title: "Pick the colours yourself"
        Layout.fillWidth: true
        // Folded to start with. Most figures use a standard map and this is the
        // longest group in the panel - open, it pushes the range controls above
        // it off the top of a short sidebar.
        collapsed: true

        ColumnLayout {
            anchors.fill: parent
            ColourStops { canvas: root.canvas; Layout.fillWidth: true }
        }
    }
}
