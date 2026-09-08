// A contextual band across the top of the figure.
//
// The Ribbon layout, for people who live in Excel, Origin or MATLAB and expect
// the controls for the current job to be visible as labelled buttons rather
// than filed under a tab. It costs vertical height, which is the scarcer
// dimension for a plot - that is the trade, and it is why this is one layout of
// six rather than the only one.
//
// Everything here already exists elsewhere; nothing is a second implementation.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var app
    property var canvas: null

    signal graphSearchRequested()

    implicitHeight: 62
    color: Theme.surfaceAlt
    border.color: Theme.border

    component Group: ColumnLayout {
        id: group
        property string title: ""
        spacing: 2
        Label {
            text: group.title
            color: Theme.textMuted
            font.pixelSize: 9
            font.letterSpacing: 0.6
            Layout.alignment: Qt.AlignHCenter
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        spacing: 10

        Group {
            title: "GRAPH"
            RowLayout {
                spacing: 4
                Button {
                    id: findGraph
                    text: "◈ Find a graph"
                    onClicked: root.graphSearchRequested()
                    ToolTip.visible: findGraph.hovered
                    ToolTip.text: "Search all " + root.app.graphEntryCount
                                + " catalogue entries (Ctrl+K)"
                }
                Button {
                    id: bestFor
                    text: "★ Best for this data"
                    enabled: root.app.activeArrowPath !== ""
                    onClicked: root.app.workspaceMode = "Visualize"
                    ToolTip.visible: bestFor.hovered
                    ToolTip.text: "Ranks the graphs that suit the loaded data"
                }
            }
        }

        ToolSeparator { Layout.fillHeight: true }

        Group {
            title: "AXIS SCALE"
            RowLayout {
                spacing: 4
                ComboBox {
                    id: xScale
                    implicitWidth: 116
                    enabled: root.canvas !== null
                    model: root.canvas ? root.canvas.axisTransformNames() : []
                    currentIndex: root.canvas ? root.canvas.xTransform : 0
                    onActivated: if (root.canvas) root.canvas.xTransform = xScale.currentIndex
                    ToolTip.visible: xScale.hovered
                    ToolTip.text: "How the x values are scaled"
                }
                ComboBox {
                    id: yScale
                    implicitWidth: 116
                    enabled: root.canvas !== null
                    model: root.canvas ? root.canvas.axisTransformNames() : []
                    currentIndex: root.canvas ? root.canvas.yTransform : 0
                    onActivated: if (root.canvas) root.canvas.yTransform = yScale.currentIndex
                    ToolTip.visible: yScale.hovered
                    ToolTip.text: "How the y values are scaled"
                }
            }
        }

        ToolSeparator { Layout.fillHeight: true }

        Group {
            title: "FIGURE"
            RowLayout {
                spacing: 4
                ColourMapSelector { canvas: root.canvas; app: root.app; autoHide: false }
                Button {
                    id: gridButton
                    text: root.app.plotGridVisible ? "Grid on" : "Grid off"
                    checkable: true
                    checked: root.app.plotGridVisible
                    onClicked: root.app.plotGridVisible = checked
                    ToolTip.visible: gridButton.hovered
                    ToolTip.text: "Show or hide the grid"
                }
            }
        }

        Item { Layout.fillWidth: true }

        Group {
            title: "OUTPUT"
            Button {
                id: exportButton
                text: "Save figure…"
                enabled: root.canvas !== null && root.canvas.pointCount > 0
                onClicked: root.app.notify("Use File ▸ Save figure as…")
                ToolTip.visible: exportButton.hovered
                ToolTip.text: "PDF, SVG, PNG, the script that redraws it, or the numbers behind it"
            }
        }
    }
}
