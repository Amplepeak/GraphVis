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

PanelScroll {
    id:root
    contentSpacing: 8
    required property var app
    signal applyRequested()

    // The committed mapping - what ControlSidebar hands to the renderer and
    // what VtkViewport reads. Deliberately NOT bound to the combo boxes: this
    // panel's contract, stated in its own subtitle, is that a selection is
    // staged until Apply commits it.
    property string xValue: ""
    property string yValue: ""
    property string zValue: ""
    property string colorValue: ""

    // The staged selection, keyed by axis. There used to be a second, hidden
    // set of combo boxes holding the committed values, and only each row's own
    // small Apply button copied its row into them - so the prominent
    // "Analyze && Apply" button, and every Apply in the point-size and PBR
    // groups, committed nothing and re-ran the mapping that was already in
    // force. Change X, press Analyze && Apply, get the previous X. One source
    // of truth now, and every Apply commits all of it.
    property var staged: ({})

    function stagedFor(key){ return root.staged[key] !== undefined ? root.staged[key] : "" }
    function stage(key,value){
        var next = {}
        for(var k in root.staged) next[k] = root.staged[k]
        next[key] = value
        root.staged = next
    }
    function commit(){
        root.xValue = root.stagedFor("x")
        root.yValue = root.stagedFor("y")
        root.zValue = root.stagedFor("z")
        root.colorValue = root.stagedFor("c")
    }
    // A new dataset has different columns, so the staged mapping starts again
    // from the first four rather than pointing at columns that no longer exist.
    function resetStaging(){
        var cols = root.app.activeColumns || []
        root.staged = {
            "x": cols.length > 0 ? cols[0] : "",
            "y": cols.length > 1 ? cols[1] : (cols.length > 0 ? cols[0] : ""),
            "z": cols.length > 2 ? cols[2] : "",
            "c": cols.length > 3 ? cols[3] : ""
        }
        root.commit()
    }
    Component.onCompleted: root.resetStaging()
    Connections {
        target: root.app
        function onActiveDatasetChanged(){ root.resetStaging() }
    }
    property real pointSize:size.value
    property real pointOpacity:alpha.value/100.0
    property bool invertOpacity:invert.checked
    property int voxelBins:densityMode.currentIndex===1 ? bins.value : 0
    property bool smartRender:smart.checked
    property int smartProfile:profile.currentIndex+1
    property string vtkMode:vtkModeBox.currentText.toLowerCase()
    property real roughness:rough.value
    property real metallic:metal.value
    property real specular:spec.value
    function apply(){ root.commit(); root.applyRequested() }
    function planText(key,fallback){return app.smartRenderPlan && app.smartRenderPlan[key] !== undefined ? String(app.smartRenderPlan[key]) : fallback}
    function planNumber(key,digits){
        if(!app.smartRenderPlan || app.smartRenderPlan[key] === undefined) return "—"
        var v = Number(app.smartRenderPlan[key])
        return isFinite(v) ? v.toPrecision(digits) : "—"
    }
    function planPercent(key){
        if(!app.smartRenderPlan || app.smartRenderPlan[key] === undefined) return "—"
        var v = Number(app.smartRenderPlan[key])
        return isFinite(v) ? (100*v).toFixed(0)+"%" : "—"
    }
    Label{text:"Variable Mapping";font.pixelSize:17;font.bold:true;color:Theme.text;Layout.margins:12}
    Label{text:"Selections are staged. Apply commits the mapped variables; Smart Render then chooses clarity/performance parameters automatically.";wrapMode:Text.WordWrap;color:Theme.textSecondary;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12}

    GvGroupBox {
        title:"Intelligent Auto-Optimization";Layout.fillWidth:true;Layout.margins:8
        ColumnLayout { anchors.fill:parent;spacing:6
            Switch{id:smart;text:"Smart Render";checked:true;font.bold:true}
            RowLayout {Layout.fillWidth:true
                ComboBox{id:profile;model:["Balanced","Clarity","Performance"];enabled:smart.checked;Layout.fillWidth:true}
                Button{text:"Analyze && Apply";enabled:smart.checked;onClicked:root.apply()}
            }
            Label {visible:smart.checked;text:"Automatically controls robust clipping, colour contrast, interpolation policy, point LOD, density alpha and anomaly emphasis. Source data is never deleted.";wrapMode:Text.WordWrap;color:Theme.textSecondary;Layout.fillWidth:true}
            Rectangle {visible:smart.checked && root.app.smartRenderPlan && Object.keys(root.app.smartRenderPlan).length>0;Layout.fillWidth:true;implicitHeight:smartSummary.implicitHeight+18;radius:7;color:Theme.surface;border.color:Theme.border
                ColumnLayout {id:smartSummary;anchors{fill:parent;margins:9} spacing:3
                    Label{text:"SMART RENDER PLAN";font.bold:true;color:Theme.accent}
                    Label{text:"Contrast: "+root.planText("color_transfer","—")+" · Palette: "+root.planText("colormap","—")+" · Interpolation: "+root.planText("interpolation","—");color:Theme.text;wrapMode:Text.WordWrap;Layout.fillWidth:true}
                    // These three used to bypass planText() and go straight
                    // through Number(), and the enclosing box is shown as
                    // soon as the plan has ANY key - so a plan without them
                    // printed "NaN → NaN" and "NaN%".
                    Label{text:"Robust display range: "+root.planNumber("display_min",5)+" → "+root.planNumber("display_max",5);color:Theme.textSecondary}
                    Label{text:"LOD target: "+root.planText("target_points","—")+" · Confidence: "+root.planPercent("confidence");color:Theme.textSecondary}
                    Repeater {
                        model: root.app.smartRenderPlan.reasons || []
                        delegate: Label {
                            required property var modelData
                            text: "\u2022 " + modelData
                            color: Theme.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }

    Repeater {
        model:[{label:"X axis",key:"x"},{label:"Y axis",key:"y"},{label:"Z axis",key:"z"},{label:"Colour / response",key:"c"}]
        delegate:ColumnLayout {
            id: axis
            required property var modelData
            Layout.fillWidth:true;Layout.leftMargin:10;Layout.rightMargin:10
            Label{text:axis.modelData.label;color:Theme.textSecondary}
            RowLayout {
                Layout.fillWidth:true
                ComboBox {
                    id: combo
                    Layout.fillWidth: true
                    model: root.app.activeColumns
                    // Bound to the staged value, so the box always shows
                    // what Apply would commit - including after a reset.
                    currentIndex: (root.app.activeColumns || []).indexOf(root.stagedFor(axis.modelData.key))
                    onActivated: root.stage(axis.modelData.key, currentText)
                }
                Button{ text:"Apply"; onClicked: root.apply() }
            }
        }
    }

    GvGroupBox {
        title:smart.checked?"5th axis / point cloud · AUTO":"5th axis / point cloud · MANUAL";Layout.fillWidth:true;Layout.margins:8
        ColumnLayout {anchors.fill:parent
            Label{visible:smart.checked;text:"Smart Render is controlling point size, base opacity and LOD. Turn Smart Render off for strict manual values.";color:Theme.textSecondary;wrapMode:Text.WordWrap;Layout.fillWidth:true}
            RowLayout{Layout.fillWidth:true;spacing:8;Label{text:"Point size";Layout.preferredWidth:96;elide:Text.ElideRight}SpinBox{id:size;from:1;to:30;value:5;editable:true;enabled:!smart.checked;Layout.fillWidth:true}Button{text:"Apply";enabled:!smart.checked;onClicked:root.apply()}}
            RowLayout{Layout.fillWidth:true;spacing:8;Label{text:"Opacity %";Layout.preferredWidth:96;elide:Text.ElideRight}SpinBox{id:alpha;from:2;to:100;value:85;editable:true;enabled:!smart.checked;Layout.fillWidth:true}Button{text:"Apply";enabled:!smart.checked;onClicked:root.apply()}}
            // Was calling app.setPointStyle() directly with the MANUAL spin
            // box values - the ones disabled just above because Smart
            // Render owns them - so ticking this while Smart Render was on
            // silently overwrote the auto-chosen point size and opacity.
            // It goes through Apply like every other control now.
            CheckBox{id:invert;text:"Invert Opacity";onToggled:root.apply()}
            RowLayout{Layout.fillWidth:true;spacing:8;Label{text:"Density mode";Layout.preferredWidth:96;elide:Text.ElideRight}ComboBox{id:densityMode;model:["Raw points","Voxel aggregation"];enabled:!smart.checked;Layout.fillWidth:true}SpinBox{id:bins;from:8;to:96;value:32;editable:true;enabled:!smart.checked&&densityMode.currentIndex===1}Button{text:"Apply";enabled:!smart.checked;onClicked:root.apply()}}
        }
    }

    GvGroupBox {
        title:"Heatmap / surface auto-clarity policy";visible:smart.checked;Layout.fillWidth:true;Layout.margins:8
        ColumnLayout{anchors.fill:parent
            Label{text:"Interpolation: "+root.planText("interpolation","analyzed after Apply");color:Theme.textSecondary}
            Label{text:"Contrast mapping: "+root.planText("color_transfer","analyzed after Apply");color:Theme.textSecondary}
            Label{text:"Extreme tails are clipped only for display scaling; the original Arrow dataset remains unchanged.";wrapMode:Text.WordWrap;color:Theme.textMuted;Layout.fillWidth:true}
        }
    }

    GvGroupBox {
        title:"VTK C++ / PBR";Layout.fillWidth:true;Layout.margins:8
        ColumnLayout{anchors.fill:parent
            RowLayout{Label{text:"Mode";Layout.fillWidth:true}ComboBox{id:vtkModeBox;model:["Surface","Points"]}Button{text:"Apply";onClicked:root.apply()}}
            RowLayout{Label{text:"Roughness";Layout.fillWidth:true}Slider{id:rough;from:0;to:1;value:.35;Layout.preferredWidth:150}Button{text:"Apply";onClicked:root.apply()}}
            RowLayout{Label{text:"Metallic";Layout.fillWidth:true}Slider{id:metal;from:0;to:1;value:0;Layout.preferredWidth:150}Button{text:"Apply";onClicked:root.apply()}}
            RowLayout{Label{text:"Specular";Layout.fillWidth:true}Slider{id:spec;from:0;to:1;value:.45;Layout.preferredWidth:150}Button{text:"Apply";onClicked:root.apply()}}
        }
    }
}
