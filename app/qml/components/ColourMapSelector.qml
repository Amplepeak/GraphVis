// The colour map for engines that colour a FIELD rather than a set of series.
//
// A heat map, a contour, a surface, a vector field: the colour map is not
// decoration on these, it IS the reading. GraphVis 17 offered eighty four maps
// in eight categories; the port shipped one, viridis, hard-coded in the
// renderer with no way to change it.
//
// The list and the categories come from PlotCanvas rather than being written
// out here, so QML cannot offer a name the renderer does not understand, and
// each entry shows a strip of the actual map — nobody knows what "Gist Ncar"
// looks like from the words, and eighty four words is a list to scroll rather
// than a choice to make.
//
// It hides itself on the engines that do not read the map, which is most of
// them. A control that does nothing is worse than no control: it is one more
// thing to try before believing it does nothing.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ComboBox {
    id: root

    required property var canvas
    // The controller, when the choice should PERSIST rather than belong to this
    // figure. Both places that show this selector pass it; it is optional only
    // so the component still works against a bare canvas.
    property var app: null
    // Set false to keep it on screen regardless of engine — the sidebar wants
    // that, the plot toolbar does not.
    property bool autoHide: true

    readonly property string currentMap: root.canvas.colourMap === ""
                                         ? "Viridis" : root.canvas.colourMap

    visible: !root.autoHide || root.canvas.usesColourMap
    width: root.visible ? root.implicitWidth : 0
    implicitWidth: 190

    // A flat model of every map, with its category carried alongside so the
    // delegate can draw a heading on the first entry of each group. Built once:
    // the list never changes at run time.
    property var entries: {
        var out = []
        var cats = root.canvas.colourMapCategories()
        for (var c = 0; c < cats.length; ++c) {
            var maps = cats[c].maps
            for (var m = 0; m < maps.length; ++m)
                out.push({ name: maps[m],
                           category: cats[c].name,
                           first: m === 0 })
        }
        return out
    }

    model: root.entries
    textRole: "name"
    valueRole: "name"

    currentIndex: {
        for (var i = 0; i < root.entries.length; ++i)
            if (root.entries[i].name === root.currentMap) return i
        return 0
    }

    displayText: root.currentMap

    ToolTip.visible: root.hovered
    ToolTip.text: "Colour map for fields — heat maps, contours, surfaces and vector fields. "
                + "Perceptually uniform maps keep equal steps in value looking like equal steps "
                + "in colour; diverging maps are for a field with a meaningful zero."

    onActivated: (index) => {
        var chosen = root.entries[index].name
        // The canvas stores "" for the default rather than "Viridis", so a
        // figure saved before the map was a choice reopens looking the same.
        var value = (chosen === "Viridis") ? "" : chosen
        if (root.app) root.app.plotColourMap = value    // binding carries it to the canvas
        else root.canvas.colourMap = value
    }

    // A strip of the chosen map in the closed box, so the current choice is
    // visible without opening it.
    contentItem: RowLayout {
        spacing: 6
        Image {
            Layout.leftMargin: 8
            Layout.preferredWidth: 34
            Layout.preferredHeight: 11
            fillMode: Image.Stretch
            source: root.canvas.colourMapPreview(root.currentMap, 68, 22)
        }
        Label {
            text: root.displayText
            color: Theme.text
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    delegate: ItemDelegate {
        id: entry
        required property int index
        required property var modelData
        width: ListView.view ? ListView.view.width : entry.implicitWidth
        highlighted: root.highlightedIndex === entry.index

        contentItem: ColumnLayout {
            spacing: 2
            // The category heading, drawn on the first map of each group.
            Label {
                visible: entry.modelData.first
                Layout.topMargin: entry.index === 0 ? 0 : 6
                text: entry.modelData.category.toUpperCase()
                color: Theme.textMuted
                font.pixelSize: 10
                font.bold: true
            }
            RowLayout {
                spacing: 8
                Image {
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 12
                    fillMode: Image.Stretch
                    source: root.canvas.colourMapPreview(entry.modelData.name, 104, 24)
                }
                Label {
                    text: entry.modelData.name
                    color: Theme.text
                    Layout.fillWidth: true
                }
            }
        }
    }

    popup: Popup {
        y: root.height
        width: Math.max(root.width, 240)
        // Eighty four entries with headings is a long list; a third of the
        // window is as much as it should ever take.
        id: popupRoot
        implicitHeight: Math.min(popupRoot.contentItem.implicitHeight + 2, 420)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.borderStrong
            radius: Theme.radius
        }
    }
}
