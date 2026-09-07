import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ScrollView {
    id:root
    // The content column binds to availableWidth, so nothing should ever
    // need horizontal scrolling; showing the bar only hid clipped text.
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    clip: true
    required property var app
    ColumnLayout {
        width:root.availableWidth;spacing:10
        Label{text:"Analysis & Experiment Design";font.pixelSize:17;font.bold:true;color:Theme.text;Layout.margins:12}
        Label{text:"Native DataFusion handles filtering, joins, aggregation and SQL. Advanced fitting/statistics/DOE remain optional Arrow-speaking science services so Python/Julia/R never own the desktop runtime.";wrapMode:Text.WordWrap;color:Theme.textSecondary;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12}
        GvGroupBox {title:"Native analytics";Layout.fillWidth:true;Layout.margins:8;ColumnLayout{anchors.fill:parent
            Button{text:"Open DataFusion SQL workspace";Layout.fillWidth:true;onClicked:app.workspaceMode="Data"}
            Button{text:"Pareto / confidence / threshold analysis";Layout.fillWidth:true;enabled:false;ToolTip.visible:hovered;ToolTip.text:"Requires an installed science-service operation and a selected numeric response"}
            Button{text:"DOE: LHS / Sobol / factorial / CCD";Layout.fillWidth:true;enabled:false;ToolTip.visible:hovered;ToolTip.text:"Available through the optional science-service API; native UI wiring is intentionally disabled until an operation contract is selected"}
            Button{text:"Reliability && uncertainty";Layout.fillWidth:true;enabled:false;ToolTip.visible:hovered;ToolTip.text:"Available through the optional science-service API"}
        }}
        GvGroupBox {title:"Scientific services";Layout.fillWidth:true;Layout.margins:8;ColumnLayout{anchors.fill:parent
            Label{text:"Python · Julia · R are plugins, not the application host.";wrapMode:Text.WordWrap;color:Theme.textSecondary}
            Label{text:"Python: "+app.pluginServicePath("python");wrapMode:Text.WrapAnywhere;color:Theme.textMuted}
            Label{text:"Julia: "+app.pluginServicePath("julia");wrapMode:Text.WrapAnywhere;color:Theme.textMuted}
            Label{text:"R: "+app.pluginServicePath("r");wrapMode:Text.WrapAnywhere;color:Theme.textMuted}
        }}
    }
}
