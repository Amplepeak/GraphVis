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
    orientation:Qt.Horizontal
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
        onApplyMapping:(x,y,z,c,size,alpha,invert,voxelBins,smartRender,smartProfile)=>{
            app.applyMapping(x,y,z,c,size,alpha,invert,voxelBins,smartRender,smartProfile)
            if(app.rendererMode==="VTK / PBR" && vtkLoader.item && vtkLoader.item.reload)
                vtkLoader.item.reload()
            plot.xColumn = x
            plot.yColumns = y ? [y] : []
        }
        // Applying a catalogue entry switches to the 2-D renderer and draws it.
        onGraphSelected:(entry)=>{
            app.rendererMode = "Qt 2-D"
            plot.engine = entry.engine
            plot.variant = entry.scale ? entry.scale : ""
            plot.title = entry.engine
        }
        // A Smart Suite recommendation carries its own mapping, so it selects
        // the graph and the axes together - the point of the scan.
        onScanRecommendation:(graph, mappings)=>{
            app.rendererMode = "Qt 2-D"
            plot.engine = graph
            plot.variant = ""
            plot.title = graph
            if (mappings && mappings.x) plot.xColumn = mappings.x
            var ys = []
            if (mappings && mappings.y) ys.push(mappings.y)
            if (mappings && mappings.z) ys.push(mappings.z)
            plot.yColumns = ys
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
        title: "Canvas"
        floatable: root.app.rendererMode === "Qt 2-D"
        floatingWidth: 1040
        floatingHeight: 760
        SplitView.fillWidth: true
        SplitView.minimumWidth: 360

        Rectangle {
            anchors.fill: parent
            color:Theme.background; border.color:Theme.border; radius:Theme.radius
            StackLayout {
                anchors.fill:parent
                currentIndex: app.rendererMode==="Qt 2-D" ? 2 : (app.rendererMode==="VTK / PBR" ? 1 : 0)
                Item {
                    WindowContainer { anchors.fill:parent; window:app.viewportWindow }
                    StatusPill { anchors{right:parent.right;bottom:parent.bottom;margins:12} text:"Rust / WGPU · persistent direct surface" }
                }
                Loader {
                    id:vtkLoader
                    active: app.rendererMode === "VTK / PBR"
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
                Item {
                    PlotCanvas {
                        id: plot
                        anchors.fill: parent
                        anchors.margins: 6
                        arrowPath: root.app.activeArrowPath
                        backgroundColor: Theme.background
                        foregroundColor: Theme.text
                        gridColor: Theme.border
                        // The series palette follows the persisted plot setting,
                        // never the theme - see components/ColourVisionBar.qml.
                        colourVision: root.app.plotColourVision
                    }
                    // Ready notice, sitting ABOVE the overlay row rather than in the
                    // same corner. Both used to anchor to the bottom right with
                    // near-identical margins - plot is inset only 6 px - so the
                    // "Full-resolution render ready / Show it" card landed on top
                    // of the Export PDF button and the status pill and made them
                    // unclickable for as long as it was up.
                    PreviewReadyNotice {
                        canvas: plot
                        anchors {
                            right: plot.right
                            bottom: overlayRow.top
                            rightMargin: 8; bottomMargin: 8
                        }
                    }

                    RowLayout {
                        id: overlayRow
                        anchors{right:parent.right;bottom:parent.bottom;margins:12}
                        spacing: 8
                        // Progress, outside the preview. Only appears when the full
                        // render is long enough to be worth mentioning.
                        RenderProgressBadge { canvas: plot }
                        Button {
                            text: "Export PDF"
                            enabled: plot.pointCount > 0
                            ToolTip.visible: hovered
                            ToolTip.text: "True vector PDF with embedded fonts, drawn by the same backend as the screen"
                            onClicked: {
                                var target = root.app.exportPath(plot.engine, "pdf")
                                if (plot.exportPdf(target)) root.app.notify("Exported " + target)
                                else root.app.notify("PDF export failed")
                            }
                        }
                        StatusPill {
                            text: plot.engineSupported ? ("Qt 2-D · " + plot.message)
                                                       : plot.message
                            textColor: plot.engineSupported ? Theme.textSecondary : Theme.warning
                        }
                    }
                }
            }
        }
    }
}
