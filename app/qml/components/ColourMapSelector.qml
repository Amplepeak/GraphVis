pragma ComponentBehavior: Bound
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

    // Wide enough for the longest map name and not a pixel more.
    //
    // It was a flat 190, which is wider than every name in the list - "Twilight
    // Shifted" is the longest - so the control sat there padded out with empty
    // space on a toolbar where width is the scarce thing. Measured rather than
    // guessed, because the answer depends on the font the theme is using and a
    // number typed in here would be wrong on the next theme.
    FontMetrics { id: nameMetrics; font: root.font }
    // THE HEADINGS COUNT TOO. This measured the map names alone, which was
    // right while every category was one or two words - "Diverging",
    // "Categorical". The colour-vision shortlists are headed "Colour-blind ·
    // Deuteranopia", which is wider than any map name in the catalogue, so the
    // popup sized itself to the names and cut the heading that explains them.
    readonly property real widestName: {
        var w = 0
        for (var i = 0; i < root.entries.length; ++i) {
            w = Math.max(w, nameMetrics.advanceWidth(root.entries[i].name))
            if (root.entries[i].first)
                w = Math.max(w, nameMetrics.advanceWidth(root.entries[i].category))
        }
        return w
    }
    // 8 left margin + 34 preview + 6 gap + the name + 30 for the arrow. The
    // arrow's own width is deliberately NOT read: the indicator is anchored to
    // this control, so measuring it here would be a binding loop.
    implicitWidth: 8 + 34 + 6 + Math.ceil(root.widestName) + 30

    // A flat model of every map, with its category carried alongside so the
    // delegate can draw a heading on the first entry of each group. Built once:
    // the list never changes at run time.
    property var entries: {
        var out = []
        // Read so the binding DEPENDS on it. colourMapCategories() is a
        // function call, and a binding that only calls a function re-evaluates
        // when nothing in particular happens - so the list went on showing
        // every map after the colour-vision mode changed, which is exactly the
        // "this setting does nothing" report. Touching the property makes the
        // list rebuild the moment the mode does.
        var vision = root.canvas ? root.canvas.colourVision : 0
        void vision
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

    // The list, grouped and folded.
    //
    // Eighty four maps in eight categories, listed flat with headings, is a
    // scroll rather than a choice: everything past Perceptually Uniform is
    // below the fold, and the eight groups the maps are organised into are
    // invisible until you have scrolled past them. So the popup opens on the
    // eight headings, one group opens at a time, and the maps in it are the
    // only ones drawn. The same shape as the theme picker, which had the same
    // problem with a hundred and eight themes.
    //
    // Starts with everything closed, including the group holding the current
    // map. Opening that one is a guess about what the person came to do - most
    // often they came to change the map, not to admire the one they have - and
    // it makes the list start half unrolled. The header of that group says
    // "current" instead, which is the information without the scrolling.
    property string openGroup: ""
    function toggleGroup(name){
        root.openGroup = (root.openGroup === name) ? "" : name
    }
    readonly property var rows: {
        var out = []
        // Same dependency as `entries` above, for the same reason.
        var vision = root.canvas ? root.canvas.colourVision : 0
        void vision
        var cats = root.canvas.colourMapCategories()
        for (var c = 0; c < cats.length; ++c) {
            var name = cats[c].name
            var maps = cats[c].maps
            var open = (root.openGroup === name)
            var holdsCurrent = false
            for (var k = 0; k < maps.length; ++k)
                if (maps[k] === root.currentMap) holdsCurrent = true
            out.push({ header: true, label: name, count: maps.length,
                       open: open, current: holdsCurrent })
            if (!open) continue
            for (var m = 0; m < maps.length; ++m)
                out.push({ header: false, label: maps[m], count: 0,
                           open: false, current: maps[m] === root.currentMap })
        }
        return out
    }

    // Choosing a map, from wherever the choice was made.
    function chooseMap(name){
        // The canvas stores "" for the default rather than "Viridis", so a
        // figure saved before the map was a choice reopens looking the same.
        var value = (name === "Viridis") ? "" : name
        if (root.app) root.app.plotColourMap = value   // binding carries it to the canvas
        else root.canvas.colourMap = value
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

    onActivated: (index) => root.chooseMap(root.entries[index].name)

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

    // The rows the popup draws: a heading per category, and the maps of
    // whichever heading is open. Not the ComboBox's own delegate model, because
    // that one is the flat list of eighty four and this one folds.
    Component {
        id: rowDelegate
        ItemDelegate {
            id: row
            required property var modelData
            width: ListView.view ? ListView.view.width : implicitWidth
            highlighted: !row.modelData.header && row.modelData.current
            // WHY A MAP IS GREYED OUT, measured from the map's own table
            // against the colour-vision mode in force - see
            // PlotCanvas::colourMapWarning. Twenty-nine of the eighty-four
            // lose most of their range for at least one reader.
            //
            // Disabled rather than hidden: a list that silently shortens when
            // a setting changes reads as the setting having broken something.
            // The tooltip is the whole point of the control being visible.
            readonly property string cvWarning:
                row.modelData.header ? ""
                                     : root.canvas.colourMapWarning(row.modelData.label)
            enabled: row.modelData.header || row.cvWarning === ""
            ToolTip.visible: row.hovered && row.cvWarning !== ""
            ToolTip.text: row.cvWarning

            contentItem: RowLayout {
                spacing: 8
                // A heading: caret, name, how many are in it, and whether the
                // map in use is one of them.
                Label {
                    visible: row.modelData.header
                    text: row.modelData.open ? "▾" : "▸"
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
                Image {
                    visible: !row.modelData.header
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 12
                    fillMode: Image.Stretch
                    source: row.modelData.header
                            ? "" : root.canvas.colourMapPreview(row.modelData.label, 104, 24)
                }
                Label {
                    text: row.modelData.header ? row.modelData.label.toUpperCase()
                                               : row.modelData.label
                    color: row.modelData.header ? Theme.textSecondary
                         : (row.cvWarning !== "" ? Theme.textMuted : Theme.text)
                    font.bold: row.modelData.header
                    font.pixelSize: row.modelData.header ? 10 : Theme.fontSizeBody
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    visible: row.modelData.header
                    text: row.modelData.current ? row.modelData.count + " · current"
                                                : String(row.modelData.count)
                    color: row.modelData.current ? Theme.accent : Theme.textMuted
                    font.pixelSize: 10
                }
            }

            // A heading opens its group and leaves the popup where it is; a map
            // is the choice, so it applies and closes. Closing on a heading
            // would make the control take three clicks to use.
            onClicked: {
                if (row.modelData.header) root.toggleGroup(row.modelData.label)
                else { root.chooseMap(row.modelData.label); root.popup.close() }
            }
        }
    }

    popup: Popup {
        y: root.height
        // Every group closes again each time this is opened, so the list always
        // looks the same when it appears rather than remembering a state from
        // an earlier visit the person has forgotten about.
        onAboutToShow: root.openGroup = ""
        width: Math.max(root.width, 240)
        // Folded, the list is eight headings; opening one adds at most a
        // dozen rows. A third of the window is still the ceiling.
        id: popupRoot
        implicitHeight: Math.min(popupRoot.contentItem.implicitHeight + 2, 420)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.rows
            delegate: rowDelegate
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.borderStrong
            radius: Theme.radius
        }
    }
}
