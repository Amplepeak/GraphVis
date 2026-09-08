// Several figures at once, stacked down a document.
//
// One canvas was the shape of the whole application: comparing two graphs meant
// drawing one, remembering what it looked like, and drawing the other. This
// keeps as many as are wanted, each with its own engine, mapping, camera and
// zoom, all live at the same time.
//
// The sidebar edits ONE of them - the current figure, outlined - because a
// mapping panel that edited all of them would make it impossible to have two
// different graphs, which is the entire point. Add, remove and reorder happen
// here; everything else is the same controls acting on whichever figure is
// current.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis
import "../components"

Item {
    id: root
    required property var app

    // The figure the controls act on. Null only before the first one exists.
    property var currentCanvas: null
    readonly property int figureCount: figures.count
    property int currentIndex: 0

    // How the figures are arranged. One column reads as a document and is what
    // a stack of figures wants; two side by side is for comparing a pair.
    property int columns: 1

    function addFigure(engine, variant) {
        figures.append({
            "figureEngine": engine ? engine : "",
            "figureVariant": variant ? variant : ""
        })
        root.currentIndex = figures.count - 1
    }
    function removeFigure(i) {
        // Never leave the notebook empty: an empty document has nothing to
        // select, so the sidebar would be editing nothing with no way back.
        if (figures.count <= 1) return
        figures.remove(i)
        root.currentIndex = Math.max(0, Math.min(root.currentIndex, figures.count - 1))
    }

    ListModel {
        id: figures
        ListElement { figureEngine: ""; figureVariant: "" }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The notebook's own bar: what this view does that a single canvas
        // cannot. Everything that acts on ONE figure is in that figure's own
        // header, so the two never compete for the same space.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 36
            color: Theme.surfaceAlt
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 8

                Label {
                    text: root.figureCount === 1 ? "1 figure"
                                                 : root.figureCount + " figures"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                }
                Label {
                    text: "· editing figure " + (root.currentIndex + 1)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: addButton
                    text: "+ Figure"
                    ToolTip.visible: addButton.hovered
                    ToolTip.text: "Add another figure, starting from the current one's graph. "
                                + "Each figure keeps its own axes, zoom and camera."
                    onClicked: root.addFigure(
                        root.currentCanvas ? root.currentCanvas.engine : "",
                        root.currentCanvas ? root.currentCanvas.variant : "")
                }
                ToolButton {
                    id: oneCol
                    text: "▤"
                    checkable: true
                    checked: root.columns === 1
                    ToolTip.visible: oneCol.hovered
                    ToolTip.text: "One column — reads as a document"
                    onClicked: root.columns = 1
                }
                ToolButton {
                    id: twoCol
                    text: "▥"
                    checkable: true
                    checked: root.columns === 2
                    ToolTip.visible: twoCol.hovered
                    ToolTip.text: "Two across — for comparing a pair side by side"
                    onClicked: root.columns = 2
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            contentWidth: availableWidth

            GridLayout {
                id: grid
                width: root.width - 20
                columns: root.columns
                columnSpacing: 8
                rowSpacing: 8

                Repeater {
                    model: figures

                    delegate: FigureCell {
                        id: cell
                        required property int index
                        required property string figureEngine
                        required property string figureVariant

                        app: root.app
                        index: cell.index
                        current: root.currentIndex === cell.index

                        Layout.fillWidth: true
                        // Tall enough to be a figure rather than a strip, and
                        // sized so two fit on an ordinary screen at one column.
                        Layout.preferredHeight: Math.max(300, root.height * 0.62)

                        onSelected: root.currentIndex = cell.index
                        onRemoveRequested: root.removeFigure(cell.index)

                        Component.onCompleted: {
                            // The engine comes from the model rather than a
                            // binding, because after this the person changes it
                            // through the sidebar and a binding would fight
                            // them for it.
                            if (cell.figureEngine !== "") {
                                cell.canvas.engine = cell.figureEngine
                                cell.canvas.variant = cell.figureVariant
                                cell.canvas.title = cell.figureEngine
                            }
                            if (cell.current) root.currentCanvas = cell.canvas
                        }
                        onCurrentChanged: if (cell.current) root.currentCanvas = cell.canvas
                    }
                }
            }
        }
    }
}
