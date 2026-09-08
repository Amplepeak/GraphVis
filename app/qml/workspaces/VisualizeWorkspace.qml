import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// PlotCanvas is a C++ type registered into this module by QML_ELEMENT. The
// other types here come from the relative directory import; a C++ type needs
// the module imported by name.
import GraphVis
import "../components"

SplitView {
    id:root
    required property var app
    // The plot itself, so a shell can hand it to the Publish workspace. The
    // canvas lives here and export lives there, and without this the two never
    // met.
    readonly property alias canvas: plot
    // Raised by the Import button in the dataset bar above the graph chooser.
    //
    // This is the whole reason that button did nothing. ActiveDatasetBar
    // emitted it, ControlSidebar forwarded it - and here the chain stopped:
    // nothing in this file listened, so the signal went nowhere and the only
    // way to open a file was the Data tab. A signal with no receiver is
    // indistinguishable, from the outside, from a button that is broken.
    signal importRequested()
    orientation:Qt.Horizontal

    // Opening a .gvfig restores the canvas. The controller imports the figure's
    // datasets first and only then emits this, so the columns the state names
    // exist by the time they are selected.
    Connections {
        target: root.app
        function onFigureLoaded(canvasState) {
            plot.applyFigureState(canvasState)
            root.app.rendererMode = "Qt 2-D"
        }
    }
    // The controls dock can be torn out into its own window, restoring the
    // flexibility GraphVis 17 had. The sidebar keeps its state either way.
    DockPanel {
        id: sidebarDock
        title: "GraphVis Controls"
        // Explicit travel limits on both panes. Without a maximum here and a
        // minimum on the canvas, the handle could not be dragged to the right
        // and panel content was clipped instead of the panel growing.
        SplitView.preferredWidth:400
        SplitView.minimumWidth:280
        SplitView.maximumWidth:Math.max(280, root.width - 360)

        ControlSidebar {
        id:sidebar
        anchors.fill: parent
        app:root.app
        canvas: plot
        onImportRequested: root.importRequested()
        onApplyMapping:(x,y,z,c,size,alpha,invert,voxelBins,smartRender,smartProfile)=>{
            app.applyMapping(x,y,z,c,size,alpha,invert,voxelBins,smartRender,smartProfile)
            // vtkLoader.item is a QObject as far as any static check can tell:
            // GraphVis.VTK is a separate, lazily-loaded module. The guard on
            // the line is the check, so the lint is told so rather than left
            // reporting a member it has no way to know about.
            // qmllint disable missing-property
            if(app.rendererMode==="VTK / PBR" && vtkLoader.item && vtkLoader.item.reload)
                vtkLoader.item.reload()
            // qmllint enable missing-property
            // ALL four mapped roles, not just two.
            //
            // This used to be `plot.yColumns = y ? [y] : []`, which threw Z and
            // Colour away. A multi-column engine reads its inputs as series -
            // a heatmap's x, y and value, a 3-D scatter's x, y and z - so every
            // one of them got a single series, drew a frame with nothing in it,
            // and only Line Chart worked. The canvas composes them according to
            // what the engine actually needs; see PlotCanvas::rebuild.
            plot.xColumn = x
            plot.yColumns = y ? [y] : []
            plot.zColumn = z ? z : ""
            plot.colorColumn = c ? c : ""
        }
        // Applying a catalogue entry switches to the 2-D renderer and draws it.
        onGraphSelected:(entry)=>{
            app.rendererMode = "Qt 2-D"
            plot.engine = entry.engine
            plot.variant = entry.scale ? entry.scale : ""
            plot.title = entry.engine
            // Recorded here rather than in the library, so the list under File
            // is what was actually drawn rather than what was clicked and then
            // abandoned.
            app.noteVisualisation(entry.engine, entry.scale ? entry.scale : "")
        }
        // A Smart Suite recommendation carries its own mapping, so it selects
        // the graph and the axes together - the point of the scan.
        onScanRecommendation:(graph, mappings)=>{
            app.rendererMode = "Qt 2-D"
            plot.engine = graph
            plot.variant = ""
            plot.title = graph
            if (mappings && mappings.x) plot.xColumn = mappings.x
            plot.yColumns = (mappings && mappings.y) ? [mappings.y] : []
            plot.zColumn = (mappings && mappings.z) ? mappings.z : ""
            plot.colorColumn = (mappings && mappings.c) ? mappings.c : ""
            app.noteVisualisation(graph, "")
            app.notify("Applied scan recommendation: " + graph)
        }
        }
    }
    // The canvas is a dock too, so a second monitor can hold the plot while the
    // controls stay on the first. The Rust/WGPU and VTK viewports embed a
    // native window in this item, and reparenting that across QWindows is not
    // something to do silently - the tear-out is offered for the Qt 2-D canvas.
    DockPanel {
        id: canvasDock
        // What is on screen, said in the panel's own title rather than in a bar
        // across the bottom of the figure. The bar was in the way of the
        // buttons beneath it and looked bolted on; a panel saying what it
        // contains is just a panel title.
        title: root.app.rendererMode !== "Qt 2-D"
               ? "Canvas"
               : (plot.pointCount > 0
                  ? "Canvas · " + plot.message
                    + (plot.showingFullRender ? " · full resolution"
                                              : (plot.previewIsExact ? "" : " · preview"))
                  : "Canvas")
        floatable: root.app.rendererMode === "Qt 2-D"
        floatingWidth: 1040
        floatingHeight: 760
        SplitView.fillWidth: true
        SplitView.minimumWidth: 360

        // The figure's controls, in the panel header.
        //
        // These used to float along the bottom edge of the plot, on top of the
        // figure, growing sideways over one another as more of them appeared -
        // and the row was wide enough to sit over the buttons underneath it.
        // A strip that covers the controls you would use to act on it is worse
        // than no strip. Everything here acts on the whole canvas, which is
        // exactly what a panel header is for.
        headerActions: [
            // What is under the pointer, and only while there is something
            // under it. First because it changes constantly and the eye should
            // not have to hunt for it.
            StatusPill {
                visible: plot.cursorOnPlot
                text: plot.cursorText
                textColor: Theme.text
            },
            RenderProgressBadge { canvas: plot },
            UnitSelector { axis: "X"; canvas: plot },
            UnitSelector { axis: "Y"; canvas: plot },
            ColourMapSelector { canvas: plot; app: root.app },
            Button {
                id: cameraReset
                text: "Reset camera"
                visible: plot.view3D
                ToolTip.visible: cameraReset.hovered
                ToolTip.text: "Back to the starting angle. Drag the figure to turn it, "
                            + "wheel or pinch to zoom, double-click to reset."
                onClicked: plot.resetCamera()
            },
            Button {
                id: viewReset
                text: "Reset view"
                visible: plot.viewZoomed
                ToolTip.visible: viewReset.hovered
                ToolTip.text: "Fit the axes back to the data. Double-click or double-tap the figure does the same."
                onClicked: plot.resetView()
            },
            Button {
                id: annotateButton
                text: plot.annotating ? "Stop annotating"
                                      : (plot.annotationCount > 0
                                         ? "Notes (" + plot.annotationCount + ")"
                                         : "Annotate")
                checkable: true
                checked: plot.annotating
                visible: plot.viewInteractive && plot.pointCount > 0
                ToolTip.visible: annotateButton.hovered
                ToolTip.text: "Add a note to the figure. Notes are anchored to a data "
                            + "point, so they stay put through a zoom, and they export "
                            + "into the PDF as real selectable text."
                onClicked: plot.annotating = annotateButton.checked
            },
            Button {
                id: clearNotes
                text: "Clear notes"
                visible: plot.annotationCount > 0
                onClicked: plot.clearAnnotations()
            },
            // Last, hard against the pop-out button, which is where the person
            // asked for it.
            Button {
                id: exportButton
                text: "Export PDF"
                enabled: plot.pointCount > 0
                ToolTip.visible: exportButton.hovered
                ToolTip.text: "True vector PDF with embedded fonts, drawn by the same backend as the screen"
                onClicked: {
                    var target = root.app.exportPath(plot.engine, "pdf")
                    if (plot.exportPdf(target)) root.app.notify("Exported " + target)
                    else root.app.notify("PDF export failed")
                }
            }
        ]

        Rectangle {
            anchors.fill: parent
            color:Theme.background; border.color:Theme.border; radius:Theme.radius
            StackLayout {
                anchors.fill:parent
                currentIndex: root.app.rendererMode==="Qt 2-D" ? 2 : (root.app.rendererMode==="VTK / PBR" ? 1 : 0)
                Item {
                    WindowContainer { anchors.fill:parent; window:root.app.viewportWindow }
                    StatusPill { anchors{right:parent.right;bottom:parent.bottom;margins:12} text:"Rust / WGPU · persistent direct surface" }
                }
                Loader {
                    id:vtkLoader
                    active: root.app.rendererMode === "VTK / PBR"
                    source: active ? "qrc:/qt/qml/GraphVis/lazy/VtkViewport.qml" : ""
                    onLoaded: {
                        item.controller = root.app
                        item.mappingSource = sidebar
                    }
                    onStatusChanged: {
                        if(status === Loader.Error)
                            root.app.notify("VTK/PBR viewport could not load. See the GraphVis startup log for QML/plugin details.")
                    }
                }
                // A column, so the notice below has somewhere to be that is not
                // on top of the figure. What genuinely belongs inside the
                // figure - the annotation editor, the ready notice - still
                // anchors to the plot item.
                ColumnLayout {
                    spacing: 4
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    PlotCanvas {
                        id: plot
                        anchors.fill: parent
                        anchors.margins: 6
                        arrowPath: root.app.activeArrowPath
                        // The figure's own background, which need not be the
                        // interface's. A plot going into a paper is white
                        // whatever the person likes to work in.
                        //   0 follow the theme   1 dark   2 light   3 white
                        backgroundColor: {
                            switch (root.app.figureTheme) {
                            case 1: return "#111820"
                            case 2: return "#f4f6f9"
                            case 3: return "#ffffff"
                            default: return Theme.background
                            }
                        }
                        foregroundColor: {
                            switch (root.app.figureTheme) {
                            case 1: return "#dbe6f0"
                            case 2: case 3: return "#14181d"
                            default: return Theme.text
                            }
                        }
                        gridColor: {
                            switch (root.app.figureTheme) {
                            case 1: return "#26384f"
                            case 2: return "#d3d9e2"
                            case 3: return "#e2e6ec"
                            default: return Theme.border
                            }
                        }
                        gridVisible: root.app.plotGridVisible
                        gridDensity: root.app.plotGridDensity
                        scaleLabelsVisible: root.app.plotScaleLabels
                        // The series palette follows the persisted plot setting,
                        // never the theme - see components/ColourVisionBar.qml.
                        colourVision: root.app.plotColourVision
                        // The field colour map and the full-render behaviour
                        // belong to the person rather than to a figure, so they
                        // are persisted on the controller and bound down here.
                        // Assigned rather than bound in the other direction:
                        // ColourMapSelector writes to app.plotColourMap, which
                        // then reaches the canvas through this binding.
                        colourMap: root.app.plotColourMap
                        fullRenderPolicy: root.app.fullRenderPolicy
                        fullRenderAskAfterSeconds: root.app.fullRenderAskAfterSeconds
                    }
                    // Placing and editing notes. Only the part that has to ask a
                    // person what a note says: the canvas stores and draws them,
                    // so the PDF export and the self-test need none of this.
                    AnnotationLayer { canvas: plot }
                    // Bottom right of the figure. It used to have to sit above
                    // the floating control row to avoid landing on the Export
                    // button; with the controls in the header there is nothing
                    // below it to clear.
                    PreviewReadyNotice {
                        canvas: plot
                        anchors {
                            right: plot.right
                            bottom: plot.bottom
                            rightMargin: 12; bottomMargin: 12
                        }
                    }

                }
                // Underneath, wrapping, dismissible.
                PlotNotice {
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    Layout.bottomMargin: 6
                    text: plot.notice
                    warning: true
                }
                }
            }
        }
    }
}
