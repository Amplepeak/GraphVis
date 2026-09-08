// The window's shape, chosen from the toolbar.
//
// Six layouts came out of the design comparison and there was no reason to keep
// only one: they differ in where the controls live, not in what they are, so
// all six are the same sidebar and the same canvas arranged differently. This
// is the control that switches between them, in the toolbar because it is a
// thing you try a few times and then leave alone - and in the View menu too,
// because that is where a person looks for it the first time.
import QtQuick
import QtQuick.Controls
import GraphVis

ComboBox {
    id: root
    required property var app

    model: root.app.uiLayoutNames
    currentIndex: root.app.uiLayout
    onActivated: root.app.uiLayout = root.currentIndex

    ToolTip.visible: root.hovered
    ToolTip.text: {
        var d = root.app.uiLayoutDescriptions
        var i = Math.max(0, Math.min(root.currentIndex, d.length - 1))
        return root.app.uiLayoutNames[i] + " — " + d[i]
    }

    // Each entry carries what it is for. Six names on their own say nothing
    // about which to pick, and picking is the entire purpose of the control.
    delegate: ItemDelegate {
        id: item
        required property int index
        required property string modelData
        width: root.width
        highlighted: root.highlightedIndex === item.index
        contentItem: Column {
            spacing: 1
            Label {
                text: item.modelData
                color: Theme.text
                font.bold: root.app.uiLayout === item.index
            }
            Label {
                width: item.width - 24
                text: root.app.uiLayoutDescriptions[item.index]
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }
    }
}
