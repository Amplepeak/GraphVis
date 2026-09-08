// The axis mapping as permanent chrome above the figure, rather than a panel
// you go and visit.
//
// This is Tableau's shelf model and it is the single best idea in the survey
// of how other applications lay themselves out. There, the columns you have
// loaded sit on the left and the Columns and Rows SHELVES sit in a strip above
// the view; you drag a field onto a shelf and the chart follows from what you
// put where. The mapping is never hidden, so the question "what is this figure
// actually plotting?" is answered by looking at it rather than by opening the
// Map tab and reading four combo boxes.
//
// The shelves offered depend on the engine. A line chart has two; a 3-D
// scatter has three; a bubble chart has a size shelf and a 4D/5D scatter has a
// colour one - and a shelf for an axis the chosen engine does not have would be
// a control that does nothing, which is worse than no control.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Rectangle {
    id: root
    required property var app
    // The live PlotCanvas, written to directly - the shelf IS the mapping,
    // there is nothing to apply.
    property var canvas: null

    implicitHeight: shelves.implicitHeight + 12
    color: Theme.surfaceAlt
    border.color: Theme.border

    readonly property var columns: root.app.activeColumns || []
    // Whether the engine takes a third column at all, and what it is called
    // there. The canvas answers, so this cannot disagree with what the backend
    // does with that column.
    readonly property string thirdRole: root.canvas ? root.canvas.thirdAxisRole : ""

    function assign(role, name) {
        if (!root.canvas) return
        if (role === "x") root.canvas.xColumn = name
        else if (role === "y") root.canvas.yColumns = name ? [name] : []
        else if (role === "z") root.canvas.zColumn = name
        else if (role === "c") root.canvas.colorColumn = name
    }

    RowLayout {
        id: shelves
        anchors.fill: parent
        anchors.margins: 6
        spacing: 10

        // One shelf. A label saying which axis, and the column sitting on it.
        component Shelf: RowLayout {
            id: shelf
            property string role: "x"
            property string caption: "X"
            property string current: ""
            spacing: 4
            Label {
                text: shelf.caption
                color: Theme.textMuted
                font.pixelSize: 10
                font.bold: true
            }
            ComboBox {
                Layout.preferredWidth: 150
                enabled: root.canvas !== null && root.columns.length > 0
                model: root.columns
                currentIndex: Math.max(0, root.columns.indexOf(shelf.current))
                onActivated: root.assign(shelf.role, root.columns[currentIndex])
            }
        }

        Shelf {
            role: "x"; caption: "X"
            current: root.canvas ? root.canvas.xColumn : ""
        }
        Shelf {
            role: "y"; caption: "Y"
            current: (root.canvas && root.canvas.yColumns.length > 0)
                     ? root.canvas.yColumns[0] : ""
        }
        // Named from the engine: "Z scale" and "Colour scale" become Z and
        // Colour here, so the shelf and the scale control agree about what the
        // third column is.
        Shelf {
            visible: root.thirdRole !== ""
            role: root.thirdRole.indexOf("Colour") === 0 ? "c" : "z"
            caption: root.thirdRole.indexOf("Colour") === 0 ? "COLOUR" : "Z"
            current: root.canvas
                     ? (root.thirdRole.indexOf("Colour") === 0
                        ? root.canvas.colorColumn : root.canvas.zColumn)
                     : ""
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }

        // What is being drawn, said where the mapping is - because on this
        // layout the graph type is a consequence of the mapping rather than a
        // choice made before it.
        Label {
            Layout.fillWidth: true
            text: root.canvas && root.canvas.engine ? root.canvas.engine : "No graph chosen"
            color: root.canvas && root.canvas.engine ? Theme.text : Theme.textMuted
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        Button {
            id: swapButton
            text: "⇄"
            flat: true
            enabled: root.canvas !== null
            ToolTip.visible: swapButton.hovered
            ToolTip.text: "Swap X and Y"
            onClicked: {
                if (!root.canvas) return
                var x = root.canvas.xColumn
                var y = root.canvas.yColumns.length > 0 ? root.canvas.yColumns[0] : ""
                root.canvas.xColumn = y
                root.canvas.yColumns = x ? [x] : []
            }
        }
    }
}
