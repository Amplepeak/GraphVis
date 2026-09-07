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
        TabBar {
            id: tabs; Layout.fillWidth:true
            TabButton{text:"Graphs"} TabButton{text:"Project"} TabButton{text:"Data"} TabButton{text:"Map"}
            TabButton{text:"Analysis"} TabButton{text:"Literature"} TabButton{text:"Publish"}
        }
        // Colour vision, stated above the graph chooser rather than buried in
        // settings: it changes what every graph below will look like.
        ColourVisionBar {
            app: root.app
            Layout.fillWidth: true
            Layout.margins: 8
            visible: tabs.currentIndex === 0
        }
        StackLayout {
            Layout.fillWidth:true; Layout.fillHeight:true; currentIndex:tabs.currentIndex
            // 318 catalogue entries browsed through a 400 px sidebar. Torn out,
            // the library gets a window of its own and the canvas keeps the
            // width; the grid, the search text and the staged entry survive the
            // trip because the item is reparented, not rebuilt.
            DockPanel {
                title: "Graph Library"
                floatingWidth: 560
                floatingHeight: 780
                GraphLibrary {
                    anchors.fill: parent
                    app: root.app
                    onApplyRequested: (entry) => root.graphSelected(entry)
                    onScanApplied: (g, m) => root.scanRecommendation(g, m)
                }
            }
            ProjectPanel { app:root.app }
            DataWorkspace { app:root.app }
            MappingPanel { id:mapping; app:root.app; onApplyRequested:root.applyMapping(xValue,yValue,zValue,colorValue,pointSize,pointOpacity,invertOpacity,voxelBins,smartRender,smartProfile) }
            AnalysisPanel { app:root.app }
            LiteraturePanel { app:root.app }
            PublicationPanel { app:root.app; canvas:root.canvas }
        }
    }
}
