import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id:root
    required property var app
    signal importRequested()
    signal literatureRequested()
    // Which workspaces are loaded on demand. Visualize is not one of them: it
    // owns the PlotCanvas, and the Publish workspace exists to export that
    // canvas. A Loader that swapped whole workspaces destroyed the canvas on
    // the way out, so PublicationPanel was constructed with no canvas at all
    // and every control in it - the profile list, Export figure, Quick PDF,
    // Quick SVG, Show the script - sat permanently disabled behind "Draw a
    // graph first", with a graph on screen. Visualize is hidden, not destroyed.
    readonly property bool visualiseMode:
        ["Literature","Home","Data","Analysis","Publish"].indexOf(root.app.workspaceMode) < 0

    VisualizeWorkspace {
        id: visualise
        anchors.fill: parent
        app: root.app
        visible: root.visualiseMode
    }
    Loader {
        anchors.fill:parent
        visible: !root.visualiseMode
        active: !root.visualiseMode
        sourceComponent: app.workspaceMode==="Literature" ? litComp : app.workspaceMode==="Home" ? homeComp : app.workspaceMode==="Data" ? dataComp : app.workspaceMode==="Analysis" ? analysisComp : publishComp
    }
    Component { id:litComp; LiteratureWorkspace{app:root.app;onOpenRequested:root.literatureRequested()} }
    Component { id:homeComp; HomeWorkspace{app:root.app;onImportRequested:root.importRequested();onLiteratureRequested:root.literatureRequested()} }
    Component { id:dataComp; DataWorkspace{app:root.app} }
    Component { id:analysisComp; AnalysisPanel{app:root.app} }
    Component { id:publishComp; PublicationPanel{app:root.app; canvas: visualise.canvas} }
}
