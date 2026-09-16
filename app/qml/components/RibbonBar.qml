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
    signal exportRequested()

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

    // The band SCROLLS rather than running off the end of the window.
    //
    // A RowLayout does not shrink its children below their minimum widths - it
    // lays them out past its own right edge - so on a narrower window the last
    // group or two were simply painted outside the canvas, over whatever was
    // beside it, with nothing to say they were there. Same idiom as the panel
    // header's action row: clip, flick sideways, and put an arrow at the edge
    // when there is more.
    Flickable {
        id: ribbonScroll
        anchors.fill: parent
        clip: true
        contentWidth: ribbonRow.width + 20
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        readonly property bool overflowing: contentWidth > width + 1

    RowLayout {
        id: ribbonRow
        x: 10
        y: 4
        // At least the width of the band, so the stretching spacer below still
        // pushes OUTPUT to the right-hand end when everything fits, and exactly
        // what it needs once it does not.
        width: Math.max(ribbonScroll.width - 20, implicitWidth)
        height: ribbonScroll.height - 8
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
                // Opens the real dialog. It used to tell the person to go
                // and use a menu instead, which is a button describing its own
                // replacement.
                onClicked: root.exportRequested()
                ToolTip.visible: exportButton.hovered
                ToolTip.text: "PDF, SVG, PNG, the script that redraws it, or the numbers behind it"
            }
        }
    }
    }

    // Nothing may be hidden silently: an arrow appears only when there is
    // something past that edge, and points at where it is.
    Label {
        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 2 }
        visible: ribbonScroll.overflowing && ribbonScroll.contentX > 1
        text: "‹"
        color: Theme.textSecondary
    }
    Label {
        anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 2 }
        visible: ribbonScroll.overflowing
                 && ribbonScroll.contentX < ribbonScroll.contentWidth - ribbonScroll.width - 1
        text: "›"
        color: Theme.textSecondary
    }
}
