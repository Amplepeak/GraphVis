// Qt 6 delegate scoping. Without this, an id from the enclosing file is not
// legally visible inside a delegate or an inline Component - it resolves only
// because the old unbound context lookup walks out of the component, which is
// slower at every evaluation and silently breaks the moment a model role
// shares a name. Bound resolves ids at compile time; delegates declare what
// they take from the model with `required property`.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis
import "../components"
import "../workspaces"

Rectangle {
    id:root
    required property var app
    signal importRequested()
    signal literatureRequested()
    readonly property bool visualiseMode:
        ["Literature","Home","Data","Analysis","Publish"].indexOf(root.app.workspaceMode) < 0

    // One list, so the palette's search has something real to filter and the
    // click handler is not a chain of index comparisons.
    readonly property var commandActions: [
        {id:"literature", label:"Read Literature"},
        {id:"import",     label:"Import dataset"},
        {id:"visualize",  label:"Visualize active dataset"},
        {id:"sql",        label:"Run DataFusion SQL"},
        {id:"analysis",   label:"Open Analysis"},
        {id:"publish",    label:"Publication Studio"}
    ]
    function runCommand(id){
        // "Read literature" in the command palette goes to the workspace. The
        // palette entry that opens a file browser is a different command, and
        // making one entry do both means it can only ever be wrong for one of
        // the two things people press it for.
        if(id==="literature") root.app.workspaceMode="Literature"
        else if(id==="import") root.importRequested()
        else if(id==="visualize") root.app.workspaceMode="Visualize"
        else if(id==="sql") root.app.workspaceMode="Data"
        else if(id==="analysis") root.app.workspaceMode="Analysis"
        else if(id==="publish") root.app.workspaceMode="Publish"
    }
    color:Theme.background
    Rectangle {
        anchors.fill:parent
        gradient:Gradient { GradientStop{position:0;color:Theme.surface} GradientStop{position:.55;color:Theme.background} GradientStop{position:1;color:Theme.background} }
    }
    RowLayout {
        anchors.fill:parent; anchors.margins:10; spacing:10
        Rectangle {
            Layout.preferredWidth:78; Layout.fillHeight:true; radius:16; color:Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, Theme.overlayAlpha); border.color:Theme.border
            ColumnLayout {
                anchors.fill:parent; anchors.margins:9; spacing:8
                Repeater {
                    model:[{icon:"⌂",mode:"Home",tip:"Home"},{icon:"▣",mode:"Literature",tip:"Read Literature"},{icon:"◈",mode:"Visualize",tip:"Visualize"},{icon:"▤",mode:"Data",tip:"Data"},{icon:"∑",mode:"Analysis",tip:"Analysis"},{icon:"↗",mode:"Publish",tip:"Publish"}]
                    delegate:ToolButton {
                        required property var modelData
                        Layout.fillWidth:true; Layout.preferredHeight:54; text:modelData.icon; font.pixelSize:22
                        highlighted:root.app.workspaceMode===modelData.mode; ToolTip.visible:hovered; ToolTip.text:modelData.tip
                        // Switches to the tab and nothing else.
                        //
                        // This used to open a file browser as well, whenever no
                        // paper happened to be loaded - so pressing the
                        // Literature icon to LOOK at the workspace threw a modal
                        // file dialog over it, and the only way to see the tab
                        // was to cancel a dialog you had not asked for. A tab
                        // deciding what you meant by pressing it is not a
                        // shortcut. The "Open literature" button inside the
                        // workspace is where that belongs, and it is already
                        // there. TopBar.qml was fixed this way; the same three
                        // sites in this shell were missed.
                        onClicked: root.app.workspaceMode = modelData.mode
                    }
                }
                Item{Layout.fillHeight:true}
                Rectangle{Layout.fillWidth:true;Layout.preferredHeight:1;color:Theme.border}
                Label{text:"18";Layout.alignment:Qt.AlignHCenter;color:Theme.textMuted}
            }
        }
        Rectangle {
            Layout.fillWidth:true; Layout.fillHeight:true; radius:16; color:Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.66); border.color:Theme.border
            // Visualize is hidden rather than destroyed: it owns the
            // PlotCanvas that the Publish workspace exists to export, and a
            // Loader that swapped it out left PublicationPanel with no canvas
            // and every export control permanently disabled.
            VisualizeWorkspace {
                id: visualise
                anchors.fill:parent; anchors.margins:4
                app: root.app
                visible: root.visualiseMode
            }
            Loader {
                anchors.fill:parent; anchors.margins:root.app.workspaceMode==="Literature"?0:4
                visible: !root.visualiseMode
                active: !root.visualiseMode
                sourceComponent:root.app.workspaceMode==="Literature"?litComp:root.app.workspaceMode==="Home"?homeComp:root.app.workspaceMode==="Data"?dataComp:root.app.workspaceMode==="Analysis"?analysisComp:publishComp
            }
        }
    }
    Component{id:homeComp;HomeWorkspace{app:root.app;onImportRequested:root.importRequested();onLiteratureRequested:root.literatureRequested()}}
    Component{id:litComp;LiteratureWorkspace{app:root.app;onOpenRequested:root.literatureRequested()}}
    Component{id:dataComp;DataWorkspace{app:root.app}}
    Component{id:analysisComp;AnalysisPanel{app:root.app}}
    Component{id:publishComp;PublicationPanel{app:root.app; canvas: visualise.canvas}}

    Popup {
        id:commandPalette;anchors.centerIn:parent;width:560;height:360;modal:true;focus:true
        background:Rectangle{radius:14;color:Theme.surfaceAlt;border.color:Theme.borderStrong}
        ColumnLayout{anchors.fill:parent;anchors.margins:14
            // The field used to filter nothing at all - the ListView held a
            // hard-coded six-entry model and ignored whatever was typed.
            TextField{
                id:commandSearch
                placeholderText:"Command palette — search actions…"
                Layout.fillWidth:true
                focus:true
                // itemAtIndex returns null for an item the view has not
                // realised yet, which is exactly the case when Enter is pressed
                // straight after typing. Run the action from the model instead
                // of reaching into a delegate that may not exist.
                onAccepted: {
                    var first = commandList.model[0]
                    if (first) { root.runCommand(first.id); commandPalette.close() }
                }
            }
            ListView{
                id:commandList
                Layout.fillWidth:true;Layout.fillHeight:true;clip:true
                model:root.commandActions.filter(function(a){
                    var q=commandSearch.text.trim().toLowerCase()
                    return q==="" || a.label.toLowerCase().indexOf(q)>=0
                })
                delegate:ItemDelegate{
                    required property var modelData
                    width:ListView.view.width
                    text:modelData.label
                    onClicked:{root.runCommand(modelData.id);commandPalette.close()}
                }
            }
            Label{
                visible:commandList.count===0
                text:"No action matches “"+commandSearch.text+"”"
                color:Theme.textMuted
                Layout.fillWidth:true
            }
        }
    }
    Shortcut{sequence:"Ctrl+K";onActivated:{commandSearch.text="";commandPalette.open();commandSearch.forceActiveFocus()}}
    Shortcut{sequence:"Ctrl+L";onActivated:root.app.workspaceMode="Literature"}
}
