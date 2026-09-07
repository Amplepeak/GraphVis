import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis
Rectangle {
    id: root
    required property var app
    height:30;color:Theme.surface;border.color:Theme.border
    RowLayout{anchors.fill:parent;anchors.leftMargin:10;anchors.rightMargin:10
        Label{text:root.app.status;color:Theme.textSecondary;elide:Text.ElideRight;Layout.fillWidth:true}
        Label{text:root.app.rendererMode;color:Theme.text}
        Label{text:"Arrow · Rust · Qt/QML";color:Theme.textMuted}
    }
}
