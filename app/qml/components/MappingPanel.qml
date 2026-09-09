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
    // The live PlotCanvas, for the axis scales, which act on the figure rather
    // than on the staged mapping.
    property var canvas: null
    signal applyRequested()

    // The mapping ControlSidebar hands to the renderer and VtkViewport reads.
    //
    // Applied as it is changed, with no Apply button anywhere. There were NINE
    // of them - one per axis row, one on point size, one on opacity, one on
    // density, four in the PBR group - and every one committed the whole
    // mapping, so which one was pressed never mattered. A button that is
    // mandatory, identical everywhere and has no alternative is not a choice
    // being offered, it is a step being demanded; the render is fast enough
    // that there is nothing to defer.
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
        root.apply()
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

    // Sliders and editable spin boxes change continuously - a drag is dozens of
    // values a second, and each one would re-map the dataset and start a
    // full-resolution render. Settling for a moment first turns a drag into one
    // apply at the end of it, which is what the Apply buttons used to do by
    // accident and is the only thing they were good for.
    //
    // Discrete controls do not go through here: a combo box has already
    // finished changing by the time it emits, so waiting would just make it
    // feel slow.
    Timer { id: settle; interval: 140; repeat: false; onTriggered: root.apply() }
    function applySoon(){ settle.restart() }
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
    Label{text:"Changes apply as you make them. Smart Render then chooses clarity and performance parameters automatically.";wrapMode:Text.WordWrap;color:Theme.textSecondary;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12}

    GvGroupBox {
        title:"Intelligent Auto-Optimization";Layout.fillWidth:true;Layout.margins:8
        ColumnLayout { anchors.fill:parent;spacing:6
            Switch{id:smart;text:"Smart Render";checked:true;font.bold:true;onToggled:root.apply()}
            RowLayout {Layout.fillWidth:true
                ComboBox{id:profile;model:["Balanced","Clarity","Performance"];enabled:smart.checked;Layout.fillWidth:true;onActivated:root.apply()}
                Button{id:reanalyze;text:"Re-analyze";enabled:smart.checked;onClicked:root.apply();ToolTip.visible:reanalyze.hovered;ToolTip.text:"Run the Smart Render analysis again. The mapping itself is already applied."}
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
            }
        }
    }

    // How those columns are scaled, immediately below the columns themselves,
    // because "which column" and "on what scale" are one question asked twice.
    GvGroupBox {
        title: "Axis scale"
        Layout.fillWidth: true
        Layout.margins: 8
        ColumnLayout {
            anchors.fill: parent
            AxisScaleBar { canvas: root.canvas; Layout.fillWidth: true }
        }
    }

    // Constants the engine needs that are not columns - a decay's parent mass,
    // and whatever the next engine of that kind declares.
    //
    // Built from what the engine declares (QtPlotBackend::engineParameters), so
    // nothing here names a parameter or knows what one means: an engine that
    // gains a constant gains a control without a line changing in this file.
    // Directly under the column rows, because "which columns" and "what
    // constants" are the same question about the same figure, and hidden
    // entirely on the engines that declare none, which is nearly all of them.
    GvGroupBox {
        title:"Engine settings"
        visible:root.canvas && root.canvas.engineParameters.length>0
        Layout.fillWidth:true;Layout.margins:8
        ColumnLayout {
            anchors.fill:parent
            spacing:6
            Repeater {
                model: root.canvas ? root.canvas.engineParameters : []
                delegate: RowLayout {
                    id: paramRow
                    required property var modelData
                    Layout.fillWidth:true
                    spacing:8
                    Label{
                        text:paramRow.modelData.label
                        Layout.preferredWidth:96
                        elide:Text.ElideRight
                        color:Theme.textSecondary
                        ToolTip.visible:paramHover.hovered
                        ToolTip.text:paramRow.modelData.help
                        HoverHandler{ id:paramHover }
                    }
                    // A text field rather than a slider or a spin box: these are
                    // measured quantities that people paste in to six figures,
                    // and dragging to 1.86484 is not a thing anyone can do.
                    TextField {
                        id: paramField
                        Layout.fillWidth:true
                        Layout.minimumWidth:70
                        // Bound to the canvas, so the box follows a reset, a
                        // reopened figure or a change of engine. Editing does
                        // not break the binding, because what is typed is sent
                        // to the canvas and comes back from it.
                        text: Number(paramRow.modelData.value)
                                  .toFixed(paramRow.modelData.decimals)
                        horizontalAlignment: TextInput.AlignRight
                        validator: DoubleValidator {
                            bottom: paramRow.modelData.minimum
                            top: paramRow.modelData.maximum
                            decimals: paramRow.modelData.decimals
                            notation: DoubleValidator.StandardNotation
                        }
                        // On finishing, not on every keystroke: half of "1.86"
                        // is "1.8", which is a different figure and would be
                        // rendered on the way to the one being typed.
                        onEditingFinished:
                            root.canvas.setEngineParameter(paramRow.modelData.key,
                                                           Number(text))
                        Keys.onEscapePressed: paramField.text=Qt.binding(function(){
                            return Number(paramRow.modelData.value)
                                       .toFixed(paramRow.modelData.decimals) })
                    }
                    Label{
                        text:paramRow.modelData.unit
                        visible:paramRow.modelData.unit.length>0
                        color:Theme.textMuted
                        Layout.preferredWidth:34
                        elide:Text.ElideRight
                    }
                }
            }
            RowLayout {
                Layout.fillWidth:true
                Label{
                    text:"Not columns - these travel with the figure and are saved in it."
                    color:Theme.textMuted
                    font.pixelSize:11
                    wrapMode:Text.WordWrap
                    Layout.fillWidth:true
                }
                Button{
                    id:resetParams
                    text:"Defaults"
                    onClicked:root.canvas.resetEngineParameters()
                    ToolTip.visible:resetParams.hovered
                    ToolTip.text:"Put every setting in this group back to what the engine declares."
                }
            }
        }
    }

    GvGroupBox {
        title:smart.checked?"5th axis / point cloud · AUTO":"5th axis / point cloud · MANUAL";Layout.fillWidth:true;Layout.margins:8
        ColumnLayout {anchors.fill:parent
            Label{visible:smart.checked;text:"Smart Render is controlling point size, base opacity and LOD.";color:Theme.textSecondary;wrapMode:Text.WordWrap;Layout.fillWidth:true}
            // The switch that owns these fields lives at the top of the panel,
            // several groups up and usually scrolled off. So the message here
            // said "turn Smart Render off" and pointed at a control that was
            // not on screen - which reads as the greyed-out fields being
            // broken. This is the same switch, offered where the fields it
            // disables actually are.
            //
            // The binding is restored explicitly after the click. A CheckBox
            // breaks its own `checked` binding the moment a person toggles it,
            // so without the Qt.binding below this box would agree with the
            // switch once and then drift from it for the rest of the session.
            CheckBox{
                id:manualPoints
                text:"Set these by hand"
                checked:!smart.checked
                onToggled:{
                    smart.checked=!checked
                    checked=Qt.binding(function(){ return !smart.checked })
                    root.apply()
                }
                ToolTip.visible:manualPoints.hovered
                ToolTip.text:"Turns Smart Render off for the whole figure, not just this group - it is one setting, and these fields are what it was choosing for you."
            }
            RowLayout{Layout.fillWidth:true;spacing:8;Label{text:"Point size";Layout.preferredWidth:96;elide:Text.ElideRight}SpinBox{id:size;from:1;to:30;value:5;editable:true;enabled:!smart.checked;Layout.fillWidth:true;onValueChanged:root.applySoon()}}
            RowLayout{Layout.fillWidth:true;spacing:8;Label{text:"Opacity %";Layout.preferredWidth:96;elide:Text.ElideRight}SpinBox{id:alpha;from:2;to:100;value:85;editable:true;enabled:!smart.checked;Layout.fillWidth:true;onValueChanged:root.applySoon()}}
            // Was calling app.setPointStyle() directly with the MANUAL spin
            // box values - the ones disabled just above because Smart
            // Render owns them - so ticking this while Smart Render was on
            // silently overwrote the auto-chosen point size and opacity.
            // It goes through Apply like every other control now.
            CheckBox{id:invert;text:"Invert Opacity";onToggled:root.apply()}
            RowLayout{Layout.fillWidth:true;spacing:8;Label{text:"Density mode";Layout.preferredWidth:96;elide:Text.ElideRight}ComboBox{id:densityMode;model:["Raw points","Voxel aggregation"];enabled:!smart.checked;Layout.fillWidth:true;onActivated:root.apply()}SpinBox{id:bins;from:8;to:96;value:32;editable:true;enabled:!smart.checked&&densityMode.currentIndex===1;onValueChanged:root.applySoon()}}
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
            // The LABEL used to be the one filling the width and each slider was
            // pinned at 150. In a narrow sidebar that pushes the slider off the
            // right edge - and it happened to the LONGEST label first, so
            // Roughness lost its slider entirely while Metallic and Specular
            // kept theirs, which looks exactly like one broken control rather
            // than like a layout that does not fit. The label is fixed and
            // elided now and the slider takes what is left, so every row
            // narrows together and none of them disappears.
            //
            // The value is printed beside each one as well. A slider with no
            // number cannot be set to anything in particular, and these three
            // are material constants people copy between figures.
            RowLayout{Layout.fillWidth:true;spacing:8
                Label{text:"Mode";Layout.preferredWidth:76;elide:Text.ElideRight}
                ComboBox{id:vtkModeBox;model:["Surface","Points"];Layout.fillWidth:true;onActivated:root.apply()}
            }
            RowLayout{Layout.fillWidth:true;spacing:8
                Label{text:"Roughness";Layout.preferredWidth:76;elide:Text.ElideRight}
                Slider{id:rough;from:0;to:1;value:.35;Layout.fillWidth:true;Layout.minimumWidth:60;onMoved:root.applySoon()}
                Label{text:rough.value.toFixed(2);Layout.preferredWidth:30;color:Theme.textSecondary}
            }
            RowLayout{Layout.fillWidth:true;spacing:8
                Label{text:"Metallic";Layout.preferredWidth:76;elide:Text.ElideRight}
                Slider{id:metal;from:0;to:1;value:0;Layout.fillWidth:true;Layout.minimumWidth:60;onMoved:root.applySoon()}
                Label{text:metal.value.toFixed(2);Layout.preferredWidth:30;color:Theme.textSecondary}
            }
            RowLayout{Layout.fillWidth:true;spacing:8
                Label{text:"Specular";Layout.preferredWidth:76;elide:Text.ElideRight}
                Slider{id:spec;from:0;to:1;value:.45;Layout.fillWidth:true;Layout.minimumWidth:60;onMoved:root.applySoon()}
                Label{text:spec.value.toFixed(2);Layout.preferredWidth:30;color:Theme.textSecondary}
            }
        }
    }
}
