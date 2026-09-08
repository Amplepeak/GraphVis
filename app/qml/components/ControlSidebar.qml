import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var app
    // The live PlotCanvas, handed down so the Publication Studio can export the
    // figure that is actually on screen rather than rebuilding one.
    property var canvas: null
    // Inspector layout: no tab bar, everything for the current figure in one
    // scrolling column. The eight tabs are what makes the graph chooser, the
    // axis mapping and the data three clicks apart, so the layout that removes
    // them removes the tab bar rather than merely renaming it.
    property bool inspectorMode: false
    // Raised by the dataset bar's Import button, so the shell opens the file
    // dialog it already owns instead of this panel growing one of its own.
    signal importRequested()
    signal applyMapping(string x,string y,string z,string color,real size,real alpha,bool invert,int voxelBins,bool smartRender,int smartProfile)
    // Emitted when a catalogue entry is staged and applied from the Graph Library.
    signal graphSelected(var entry)
    // Smart Suite recommendation: graph plus the mapping the scanner chose.
    signal scanRecommendation(string graph, var mappings)
    property alias mappingX: mapping.xValue
    property alias mappingY: mapping.yValue
    property alias mappingZ: mapping.zValue
    property alias mappingColor: mapping.colorValue
    property alias vtkMode: mapping.vtkMode
    property alias roughness: mapping.roughness
    property alias metallic: mapping.metallic
    property alias specular: mapping.specular
    color:Theme.background; border.color:Theme.border
    ColumnLayout {
        anchors.fill:parent; spacing:0
        // The tab bar scrolls rather than clipping its last tabs.
        //
        // A plain TabBar distributes its buttons across the width it is given
        // and elides the labels when there is not enough; at 400 px "Literature"
        // became "Literat..." and Publish was cut in half. A row of tabs you
        // cannot read is a row of tabs you have to click to identify.
        ScrollView {
            Layout.fillWidth: true
            visible: !root.inspectorMode
            Layout.preferredHeight: root.inspectorMode ? 0 : tabs.implicitHeight
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            ScrollBar.horizontal.policy: ScrollBar.AsNeeded
            clip: true
            TabBar {
                id: tabs
                TabButton{text:"Graphs"; width:implicitWidth} TabButton{text:"Project"; width:implicitWidth}
                TabButton{text:"Data"; width:implicitWidth} TabButton{text:"Map"; width:implicitWidth}
                TabButton{text:"Analysis"; width:implicitWidth} TabButton{text:"Solver"; width:implicitWidth}
                TabButton{text:"Literature"; width:implicitWidth} TabButton{text:"Publish"; width:implicitWidth}
            }
        }
        // Which dataset the graphs below draw from. Above the chooser because
        // that is the order the questions are asked in - what data, then what
        // picture of it - and because switching used to mean leaving this tab
        // for the Data tab and coming back.
        // Inspector: one column, sections in the order the work happens.
        PanelScroll {
            visible: root.inspectorMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentSpacing: 10
            ActiveDatasetBar {
                app: root.app
                Layout.fillWidth: true
                onImportRequested: root.importRequested()
            }
            ColourVisionBar { app: root.app; canvas: root.canvas; Layout.fillWidth: true }
            GvGroupBox {
                title: "Axes"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    AxisScaleBar { canvas: root.canvas; Layout.fillWidth: true }
                }
            }
            GvGroupBox {
                title: "Graph"
                Layout.fillWidth: true
                Layout.preferredHeight: 460
                GraphLibrary {
                    anchors.fill: parent
                    app: root.app
                    onApplyRequested: (entry) => root.graphSelected(entry)
                    onScanApplied: (g, m) => root.scanRecommendation(g, m)
                }
            }
        }

        StackLayout {
            visible: !root.inspectorMode
            Layout.fillWidth:true; Layout.fillHeight:true; currentIndex:tabs.currentIndex
            // The Graphs tab, split so the person decides how the height is
            // shared.
            //
            // The settings above the chooser have a fixed appetite and the
            // library has none - it holds 433 entries - so a fixed division
            // squeezed the list down to three visible rows and clipped the
            // first of them. A SplitView gives it a handle: drag the settings
            // shut when picking a graph, drag them open when setting one up.
            // Both halves scroll, so neither can clip its content whatever the
            // handle is doing.
            SplitView {
                orientation: Qt.Vertical

                ColumnLayout {
                    SplitView.preferredHeight: 250
                    SplitView.minimumHeight: 34
                    spacing: 0
                    PanelScroll {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentSpacing: 8
                        // Which dataset the graphs below draw from. Above the
                        // chooser because that is the order the questions are
                        // asked in - what data, then what picture of it.
                        ActiveDatasetBar {
                            app: root.app
                            Layout.fillWidth: true
                            onImportRequested: root.importRequested()
                        }
                        // Colour vision, the field colour map and the figure's
                        // own background: all three change what every graph
                        // below will look like.
                        ColourVisionBar {
                            app: root.app
                            canvas: root.canvas
                            Layout.fillWidth: true
                        }
                    }
                }

                // 433 catalogue entries browsed through a 400 px sidebar. Torn
                // out, the library gets a window of its own and the canvas keeps
                // the width; the grid, the search text and the staged entry
                // survive the trip because the item is reparented, not rebuilt.
                DockPanel {
                    title: "Graph Library"
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 140
                    floatingWidth: 560
                    floatingHeight: 780
                    GraphLibrary {
                        anchors.fill: parent
                        app: root.app
                        onApplyRequested: (entry) => root.graphSelected(entry)
                        onScanApplied: (g, m) => root.scanRecommendation(g, m)
                    }
                }
            }
            ProjectPanel { app:root.app }
            DataWorkspace { app:root.app }
            MappingPanel { id:mapping; app:root.app; canvas:root.canvas; onApplyRequested:root.applyMapping(xValue,yValue,zValue,colorValue,pointSize,pointOpacity,invertOpacity,voxelBins,smartRender,smartProfile) }
            AnalysisPanel { app:root.app }
            SolverPanel { app:root.app }
            LiteraturePanel { app:root.app }
            PublicationPanel { app:root.app; canvas:root.canvas }
        }
    }
}
