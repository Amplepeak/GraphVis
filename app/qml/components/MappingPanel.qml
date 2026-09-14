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

    // The formula, on the seven engines that plot one. Above the column
    // requirement because on those engines there are no columns to require -
    // the formula IS the data.
    GvGroupBox {
        title: "Formula"
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8
        visible: root.canvas && root.canvas.usesExpression
        ColumnLayout {
            anchors.fill: parent
            ExpressionBox { canvas: root.canvas; Layout.fillWidth: true }
        }
    }

    // HOW MANY COLUMNS this engine reads, and how many are mapped.
    //
    // The canvas has computed this since the port began - PlotCanvas
    // columnsRequired, from the engine's own column plan - and no part of the
    // interface ever read it. It is the answer to the single most common
    // confusion in the program: a heat map or a 3-D scatter drawn from two
    // columns produces a correct, empty, unexplained frame, and the reason is
    // that the engine wanted a third. Saying so beside the mapping controls is
    // where a person is already looking when it happens.
    Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        // Never on the seven formula engines. `columnsRequired` reports a
        // column plan for every engine including those, so Function Plot came
        // out as "reads 2 columns and 0 are mapped ... this will be an empty
        // frame", printed directly beneath a correctly drawn curve. A warning
        // that contradicts what the user can see is worse than no warning: it
        // teaches them to stop believing the panel. On these engines the
        // formula IS the data and no column is read.
        visible: root.canvas && root.canvas.columnsRequired > 0
                 && !root.canvas.usesExpression
        implicitHeight: needLabel.implicitHeight + 14
        radius: Theme.radius
        readonly property int mapped: {
            if (!root.canvas) return 0
            var n = 0
            if (String(root.canvas.xColumn || "") !== "") ++n
            if (root.canvas.yColumns && root.canvas.yColumns.length > 0) n += root.canvas.yColumns.length
            if (String(root.canvas.zColumn || "") !== "") ++n
            if (String(root.canvas.colorColumn || "") !== "") ++n
            return n
        }
        readonly property bool short_: root.canvas && mapped < root.canvas.columnsRequired
        color: short_ ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.14)
                      : Qt.rgba(Theme.positive.r, Theme.positive.g, Theme.positive.b, 0.10)
        border.color: short_ ? Theme.warning : Theme.positive

        Label {
            id: needLabel
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 9 }
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: Theme.text
            text: {
                if (!root.canvas) return ""
                var need = root.canvas.columnsRequired
                var have = parent.mapped
                if (have < need)
                    return "“" + root.canvas.engine + "” reads " + need + " columns and "
                         + have + (have === 1 ? " is" : " are") + " mapped. Until the rest are "
                         + "chosen the figure will be an empty frame — that is the missing "
                         + "column, not a broken graph."
                return "“" + root.canvas.engine + "” reads " + need
                     + (need === 1 ? " column" : " columns") + ", and has them."
            }
        }
    }

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
            // The formula engines read no columns, so these four selectors do
            // nothing at all there - and they keep showing whichever columns
            // were last mapped from a real dataset, which reads as though the
            // curve were being drawn from them. A control that cannot affect
            // the figure should not be offered for it.
            visible: !(root.canvas && root.canvas.usesExpression)
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

    // How much of each column the figure shows, and what range the colours run
    // over. Directly under the scale, because "log or linear" and "from where
    // to where" are the same question about the same axis - and because a
    // person hunting for a cap looks where the axis controls already are.
    RangePanel {
        canvas: root.canvas
        Layout.fillWidth: true
        Layout.margins: 8
    }

    // The camera, for the engines drawn into a projected cube.
    //
    // Turning one was a drag on the figure and nothing else: no control, no
    // readout, no hint that the gesture existed. So a figure that came up at an
    // unhelpful angle looked like a figure that could not be turned, and a drag
    // that did nothing - because the pointer was outside the figure, or because
    // the engine is not one of the projected ones - was indistinguishable from
    // rotation being broken.
    //
    // These are the same two numbers the drag writes, so they are also a
    // readout: turn the figure with the mouse and the sliders follow it. If
    // they do not move, the drag is not reaching the canvas, which is a
    // different fault from the camera not working and now looks different too.
    GvGroupBox {
        title:"View angle"
        visible:root.canvas && root.canvas.view3D
        Layout.fillWidth:true;Layout.margins:8
        ColumnLayout {
            anchors.fill:parent
            spacing:6
            Label{
                text:"Drag the figure to turn it, wheel to zoom, double-click to reset."
                color:Theme.textMuted
                font.pixelSize:11
                wrapMode:Text.WordWrap
                Layout.fillWidth:true
            }
            // Each slider writes to the canvas and then REBINDS to it. Without
            // the rebinding a slider stops following the figure the moment it
            // is touched once, and the two drift apart for the rest of the
            // session - which is worse than having no slider, because it then
            // shows an angle the figure is not at.
            RowLayout{Layout.fillWidth:true;spacing:8
                Label{text:"Turn";Layout.preferredWidth:64;elide:Text.ElideRight;color:Theme.textSecondary}
                Slider{
                    id:azimuthSlider
                    from:-180;to:180
                    value:root.canvas ? root.canvas.azimuth : 0
                    Layout.fillWidth:true;Layout.minimumWidth:60
                    onMoved:{
                        root.canvas.azimuth=value
                        value=Qt.binding(function(){ return root.canvas.azimuth })
                    }
                }
                Label{text:(root.canvas?Math.round(root.canvas.azimuth):0)+"°"
                      Layout.preferredWidth:34;color:Theme.textSecondary}
            }
            RowLayout{Layout.fillWidth:true;spacing:8
                Label{text:"Tilt";Layout.preferredWidth:64;elide:Text.ElideRight;color:Theme.textSecondary}
                Slider{
                    id:elevationSlider
                    from:-89;to:89
                    value:root.canvas ? root.canvas.elevation : 0
                    Layout.fillWidth:true;Layout.minimumWidth:60
                    onMoved:{
                        root.canvas.elevation=value
                        value=Qt.binding(function(){ return root.canvas.elevation })
                    }
                }
                Label{text:(root.canvas?Math.round(root.canvas.elevation):0)+"°"
                      Layout.preferredWidth:34;color:Theme.textSecondary}
            }
            RowLayout{Layout.fillWidth:true;spacing:8
                Label{text:"Zoom";Layout.preferredWidth:64;elide:Text.ElideRight;color:Theme.textSecondary}
                Slider{
                    id:zoomSlider
                    from:0.35;to:4.0
                    value:root.canvas ? root.canvas.cameraZoom : 1
                    Layout.fillWidth:true;Layout.minimumWidth:60
                    onMoved:{
                        root.canvas.cameraZoom=value
                        value=Qt.binding(function(){ return root.canvas.cameraZoom })
                    }
                }
                Label{text:(root.canvas?root.canvas.cameraZoom.toFixed(2):"1.00")+"×"
                      Layout.preferredWidth:34;color:Theme.textSecondary}
            }
            Button{
                id:resetCameraButton
                text:"Straight on"
                onClicked:root.canvas.resetCamera()
                ToolTip.visible:resetCameraButton.hovered
                ToolTip.text:"Back to the angle the figure opens at."
            }
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
