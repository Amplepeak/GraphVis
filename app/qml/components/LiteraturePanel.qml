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
        Label{text:"Literature Intelligence";font.pixelSize:17;font.bold:true;color:Theme.text;Layout.margins:12}
        Label{text:"PDF/VLM/chart de-rendering is an optional scientific service. Extracted numerical data returns to the native workspace as Arrow IPC, so literature data is never baked into GraphVis itself.";wrapMode:Text.WordWrap;color:Theme.textSecondary;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12}
        GvGroupBox {title:"Pipeline";Layout.fillWidth:true;Layout.margins:8;ColumnLayout{anchors.fill:parent
            CheckBox{text:"Extract numeric tables/series";checked:true}
            CheckBox{text:"Find datasets inside literature";checked:true}
            CheckBox{text:"Reconstruct source plots";checked:true}
            CheckBox{text:"Link methods/equations/units";checked:true}
            Button{text:"Import literature…"}
            Button{text:"Paste literature text…"}
        }}
    }
}
