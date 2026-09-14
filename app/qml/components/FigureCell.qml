// One figure in the notebook: a canvas, its own controls, its own explanation.
//
// The whole application had exactly one PlotCanvas, so comparing two graphs
// meant drawing one, remembering it, and drawing the other. This is the unit
// that makes several possible - it owns everything a figure needs, so a second
// one is a second instance rather than a second copy of the workspace.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis
import "."

Rectangle {
    id: root

    required property var app
    // Which figure this is, for the header and for the remove button.
    required property int index
    // True for the one whose settings the sidebar is editing. Only one figure
    // is current; the rest keep drawing.
    property bool current: false

    signal selected()
    signal removeRequested()

    // The canvas itself, so the workspace can hand the current one to the
    // sidebar, the menu bar and the Publish panel.
    readonly property alias canvas: plot

    color: Theme.background
    border.color: root.current ? Theme.accent : Theme.border
    border.width: root.current ? 2 : 1
    radius: Theme.radius

    // Clicking anywhere on a figure makes it the current one. A TapHandler
    // rather than a MouseArea so the canvas underneath still gets its drags:
    // selecting a figure and turning it are not exclusive.
    TapHandler {
        onTapped: root.selected()
        // The canvas handles its own presses; this only wants what falls
        // through, plus taps on the chrome.
        gesturePolicy: TapHandler.ReleaseWithinBounds
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: (root.index + 1) + "."
                color: Theme.textMuted
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeSmall
            }
            Label {
                Layout.fillWidth: true
                text: plot.pointCount > 0 ? plot.message : (plot.engine ? plot.engine : "No graph chosen")
                color: root.current ? Theme.text : Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                font.bold: root.current
                elide: Text.ElideRight
            }
            ToolButton {
                id: cameraReset
                visible: plot.view3D
                text: "⟲"
                ToolTip.visible: cameraReset.hovered
                ToolTip.text: "Back to the starting angle"
                onClicked: plot.resetCamera()
            }
            ToolButton {
                id: viewReset
                visible: plot.viewZoomed
                text: "⤢"
                ToolTip.visible: viewReset.hovered
                ToolTip.text: "Fit the axes back to the data"
                onClicked: plot.resetView()
            }
            ToolButton {
                id: removeButton
                text: "✕"
                ToolTip.visible: removeButton.hovered
                ToolTip.text: "Remove this figure"
                onClicked: root.removeRequested()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            PlotCanvas {
                id: plot
                anchors.fill: parent
                arrowPath: root.app.activeArrowPath

                //   0 follow the theme   1 dark   2 light   3 white
                //   4 a colour the person picked, its ink derived from it
                backgroundColor: {
                    switch (root.app.figureTheme) {
                    case 1: return "#111820"
                    case 2: return "#f4f6f9"
                    case 3: return "#ffffff"
                    case 4: return root.app.figureBackground
                    default: return Theme.background
                    }
                }
                foregroundColor: {
                    switch (root.app.figureTheme) {
                    case 1: return "#dbe6f0"
                    case 2: case 3: return "#14181d"
                    case 4: return root.app.figureForeground
                    default: return Theme.text
                    }
                }
                gridColor: {
                    switch (root.app.figureTheme) {
                    case 1: return "#26384f"
                    case 2: return "#d3d9e2"
                    case 3: return "#e2e6ec"
                    case 4: return root.app.figureGridColour
                    default: return Theme.border
                    }
                }
                gridVisible: root.app.plotGridVisible
                gridDensity: root.app.plotGridDensity
                gridDensityY: root.app.plotGridDensityY
                pieLabels: root.app.plotPieLabels
                polarConvention: root.app.plotPolarConvention
                scaleLabelsVisible: root.app.plotScaleLabels
                fieldInterpolation: root.app.plotFieldInterpolation
                colourVision: root.app.plotColourVision
                colourVisionPreview: root.app.plotColourVisionPreview
                colourMap: root.app.plotColourMap
                fullRenderPolicy: root.app.fullRenderPolicy
                fullRenderAskAfterSeconds: root.app.fullRenderAskAfterSeconds
            }

            AnnotationLayer { canvas: plot }

            PreviewReadyNotice {
                canvas: plot
                anchors {
                    right: plot.right
                    bottom: plot.bottom
                    rightMargin: 12; bottomMargin: 12
                }
            }
        }

        PlotNotice {
            Layout.fillWidth: true
            text: plot.notice
            warning: true
        }
    }
}
