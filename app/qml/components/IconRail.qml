// A strip of symbols down one edge that chooses which single panel is showing.
//
// The application has eight panels and showed them as eight text tabs across
// the top of a 400 px sidebar, where "Literature" became "Literat..." and
// Publish was cut in half. A rail solves the same problem the other way round:
// vertical space is what a sidebar has, symbols are what fits in a narrow
// strip, and clicking the ACTIVE symbol shuts the panel - so the rail is both
// the switch and the collapse, which is the trick VS Code, JupyterLab,
// Obsidian and Figma all arrived at independently.
//
// Symbols alone are not enough and never were: every one carries its name in a
// tooltip and the active one is named in the panel header beside it.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root

    // Which panel is showing, as an index into the same order ControlSidebar's
    // tab bar uses - so the rail and the tabs are two faces of one selection
    // and cannot disagree.
    property int currentIndex: 0
    // Shut, with the rail still on screen. Only meaningful when the layout says
    // the rail persists; otherwise the panel is always open.
    property bool collapsed: false
    property bool allowCollapse: false

    signal panelChosen(int index)
    signal collapseToggled()

    implicitWidth: 42
    color: Theme.surfaceAlt
    border.color: Theme.border

    // Order and names match ControlSidebar's tabs exactly. The symbols are
    // chosen to be distinguishable at 16 px rather than to be clever: a graph
    // curve, a folder, a table, a pair of axes, a distribution, a gear, a book
    // and an outward arrow.
    readonly property var panels: [
        { icon: "◠", name: "Graphs",     hint: "The graph catalogue and what suits this data" },
        { icon: "▤", name: "Project",    hint: "Datasets, figures and saved work" },
        { icon: "▦", name: "Data",       hint: "The table itself, and what is in each column" },
        { icon: "✛", name: "Map",        hint: "Which column goes on which axis" },
        { icon: "∿", name: "Analysis",   hint: "Fits, statistics and derived columns" },
        { icon: "⚙", name: "Solver",     hint: "Models, optimisation and sensitivity" },
        { icon: "❑", name: "Literature", hint: "Papers, and what they say about this data" },
        { icon: "⇱", name: "Publish",    hint: "Journal profiles and export" }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 4
        spacing: 2

        Repeater {
            model: root.panels
            delegate: ToolButton {
                id: railButton
                required property var modelData
                required property int index
                Layout.preferredWidth: 38
                Layout.preferredHeight: 34
                Layout.alignment: Qt.AlignHCenter
                // Active AND open. A rail whose current icon still looks
                // selected while its panel is shut says the panel is there when
                // it is not.
                checkable: false
                ToolTip.visible: railButton.hovered
                ToolTip.text: railButton.modelData.name + " — " + railButton.modelData.hint
                             + (root.allowCollapse && root.currentIndex === railButton.index
                                ? "\n(click again to close the panel)" : "")
                onClicked: {
                    if (root.allowCollapse && root.currentIndex === railButton.index) {
                        root.collapseToggled()
                        return
                    }
                    root.collapsed = false
                    root.currentIndex = railButton.index
                    root.panelChosen(railButton.index)
                }
                background: Rectangle {
                    color: (root.currentIndex === railButton.index && !root.collapsed)
                           ? Theme.background
                           : (railButton.hovered ? Theme.surface : "transparent")
                    // The selected marker runs down the outer edge, as every
                    // rail in every application that has one does.
                    Rectangle {
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                        width: 2
                        visible: root.currentIndex === railButton.index
                        color: root.collapsed ? Theme.textMuted : Theme.accent
                    }
                }
                contentItem: Label {
                    text: railButton.modelData.icon
                    color: (root.currentIndex === railButton.index && !root.collapsed)
                           ? Theme.text : Theme.textSecondary
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Item { Layout.fillHeight: true }

        // Bottom of the rail, where an application's settings and search live.
        ToolButton {
            id: paletteButton
            Layout.preferredWidth: 38
            Layout.preferredHeight: 34
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 4
            ToolTip.visible: paletteButton.hovered
            ToolTip.text: "Find a graph, a column, a layout or an action  (Ctrl+K)"
            onClicked: root.commandRequested()
            contentItem: Label {
                text: "⌕"
                color: Theme.textSecondary
                font.pixelSize: 17
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    signal commandRequested()
}
