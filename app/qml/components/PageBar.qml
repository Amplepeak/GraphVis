// A strip of pages along the bottom edge, left to right in the order the work
// happens.
//
// DaVinci Resolve's page model, which is the strongest version of the idea that
// an application can be several applications over one document: its seven pages
// are not seven tabs of one layout, they are seven arrangements, and its
// Deliver page is a whole page given over to export. That is exactly what a
// publication panel is, and it has spent this port as the eighth tab of a
// sidebar - the same width and prominence as the colour picker.
//
// Along the BOTTOM rather than the top, as Resolve puts it, because the top of
// the window already has a menu bar and a toolbar and a third row of tabs there
// stops reading as a mode and starts reading as more chrome.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root

    // The panel index each page opens, in ControlSidebar's tab order.
    readonly property var pages: [
        { name: "Data",    panel: 2, icon: "▦", hint: "Import, inspect and clean the table" },
        { name: "Map",     panel: 3, icon: "✛", hint: "Put columns on axes" },
        { name: "Plot",    panel: 0, icon: "◠", hint: "Choose the graph and how it looks" },
        { name: "Analyse", panel: 4, icon: "∿", hint: "Fit, test and derive" },
        { name: "Read",    panel: 6, icon: "❑", hint: "The paper beside the figure" },
        { name: "Publish", panel: 7, icon: "⇱", hint: "Journal profile, and the file that leaves here" }
    ]

    property int currentPage: 2
    signal pageChosen(int panelIndex)

    implicitHeight: 44
    color: Theme.surfaceAlt
    border.color: Theme.border

    RowLayout {
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: root.pages
            delegate: ToolButton {
                id: pageButton
                required property var modelData
                required property int index
                Layout.preferredHeight: 38
                ToolTip.visible: pageButton.hovered
                ToolTip.text: pageButton.modelData.hint
                onClicked: {
                    root.currentPage = pageButton.index
                    root.pageChosen(pageButton.modelData.panel)
                }
                background: Rectangle {
                    color: root.currentPage === pageButton.index ? Theme.background
                         : (pageButton.hovered ? Theme.surface : "transparent")
                    radius: Theme.radius
                }
                contentItem: RowLayout {
                    spacing: 6
                    Label {
                        text: pageButton.modelData.icon
                        color: root.currentPage === pageButton.index ? Theme.accent : Theme.textMuted
                        font.pixelSize: 15
                    }
                    Label {
                        text: pageButton.modelData.name
                        color: root.currentPage === pageButton.index ? Theme.text : Theme.textSecondary
                        font.pixelSize: 12
                        font.bold: root.currentPage === pageButton.index
                    }
                }
            }
        }
    }
}
