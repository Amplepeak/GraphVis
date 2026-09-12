// Reading the numbers back off a published figure.
//
// This is the half of the literature workflow that was written, tested and
// unreachable. The de-renderer, the calibration model and the tracing code all
// existed in the science service; the three buttons that would have called them
// were `enabled: false` with tooltips describing a selection the interface had
// no way to make. There was no figure list, no figure on screen, and no way to
// say where the axes are.
//
// What a de-render needs is four things, and only one of them can be guessed:
//
//   the figure      chosen from the ones found in the paper
//   the plot area   in pixels - where the axes box actually is in the image
//   what it spans   the numbers at the four edges, read off the printed axes
//   the colour      of the curve to follow
//
// A vision model supplies the middle two when there is one. Without a model
// they are typed and clicked, which is the same information and needs no key,
// no network and no account - and is why this works offline, which is the whole
// premise of the feature.
//
// The gestures are deliberately the ones the task suggests: drag a box round
// the plot, click the line you want. Typing four pixel coordinates for a corner
// you can see is the sort of thing that makes a person give up on a feature
// that would otherwise have saved them an afternoon with a ruler.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Item {
    id: root
    required property var app

    readonly property var figures: root.app.literatureFigures
    property int selected: -1
    readonly property var figure: (root.selected >= 0 && root.selected < root.figures.length)
                                  ? root.figures[root.selected] : null
    readonly property string imagePath: root.figure ? String(root.figure.path) : ""

    // The plot area, in the ORIGINAL image's pixels. Held here rather than on
    // the overlay so that switching figure and coming back does not lose it.
    property rect plotBox: Qt.rect(0, 0, 0, 0)
    readonly property bool boxDrawn: root.plotBox.width > 4 && root.plotBox.height > 4
    property color traceColour: "#00000000"
    readonly property bool colourPicked: root.traceColour.a > 0

    // What the printed axes say. Strings, because a half-typed "1e" is not a
    // number and a field that rejects it mid-keystroke cannot be typed into.
    property string xMinText: ""
    property string xMaxText: ""
    property string yMinText: ""
    property string yMaxText: ""
    property bool xLog: false
    property bool yLog: false
    property real tolerance: 45

    // A grid needs only the box: there is no curve to follow and no axis range
    // to map one onto.
    readonly property bool calibrated: root.asGrid
                                       ? root.boxDrawn
                                       : (root.boxDrawn && root.colourPicked
                                          && isFinite(parseFloat(root.xMinText))
                                          && isFinite(parseFloat(root.xMaxText))
                                          && isFinite(parseFloat(root.yMinText))
                                          && isFinite(parseFloat(root.yMaxText)))

    // When the reader has already looked at this figure, its answer fills the
    // fields in rather than being displayed beside them for the person to copy.
    // A model that has read the axes and then makes you type them out has
    // saved nobody anything.
    onFigureChanged: {
        root.plotBox = Qt.rect(0, 0, 0, 0)
        root.traceColour = "#00000000"
        if (!root.figure) return
        root.xLog = String(root.figure.x_scale || "") === "log"
        root.yLog = String(root.figure.y_scale || "") === "log"
    }

    // WHAT KIND of reading. A line traced across the plot, or the whole panel
    // read as a field of values - literature.heatmap, which existed in the
    // service and had no caller anywhere. The box is calibrated the same way;
    // a grid needs no colour, because every colour in it IS a value.
    property bool asGrid: false

    function readPanel() {
        root.app.derenderHeatmap({
            "image_path": root.imagePath,
            "bbox": [root.plotBox.x, root.plotBox.y,
                     root.plotBox.x + root.plotBox.width,
                     root.plotBox.y + root.plotBox.height],
            "width": 160, "height": 120, "grayscale": true
        })
    }

    function trace() {
        root.app.derenderFigure({
            "image_path": root.imagePath,
            "rgb": [Math.round(root.traceColour.r * 255),
                    Math.round(root.traceColour.g * 255),
                    Math.round(root.traceColour.b * 255)],
            "bbox": [root.plotBox.x, root.plotBox.y,
                     root.plotBox.x + root.plotBox.width,
                     root.plotBox.y + root.plotBox.height],
            "x_range": [parseFloat(root.xMinText), parseFloat(root.xMaxText)],
            "y_range": [parseFloat(root.yMinText), parseFloat(root.yMaxText)],
            "x_scale": root.xLog ? "log" : "linear",
            "y_scale": root.yLog ? "log" : "linear",
            "plot_type": root.figure && root.figure.plot_type ? root.figure.plot_type : "line",
            "tolerance": root.tolerance,
            "caption": root.figure ? String(root.figure.caption || "") : "",
            "page": root.figure ? root.figure.page : 1
        })
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // The figures found in the paper. A strip rather than a list, because
        // a figure is recognised by looking at it and named by nothing useful.
        Label {
            text: root.figures.length > 0
                  ? root.figures.length + " figures found in this paper"
                  : "No figures yet — run “Extract figures and tables”."
            color: root.figures.length > 0 ? Theme.textSecondary : Theme.textMuted
            font.pixelSize: 11
        }

        ListView {
            id: strip
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            visible: root.figures.length > 0
            orientation: ListView.Horizontal
            spacing: 6
            clip: true
            model: root.figures
            ScrollBar.horizontal: ScrollBar {}
            delegate: Rectangle {
                id: thumb
                required property var modelData
                required property int index
                width: 120
                height: 88
                color: Theme.surfaceAlt
                border.color: root.selected === thumb.index ? Theme.accent : Theme.border
                border.width: root.selected === thumb.index ? 2 : 1
                radius: Theme.radius
                Image {
                    anchors.fill: parent
                    anchors.margins: 4
                    source: "file:///" + String(thumb.modelData.path)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                Label {
                    anchors { left: parent.left; bottom: parent.bottom; margins: 3 }
                    text: "p" + thumb.modelData.page
                    color: Theme.textMuted
                    font.pixelSize: 9
                }
                TapHandler { onTapped: root.selected = thumb.index }
                // A Rectangle has no `hovered` of its own - the handler is
                // where that lives, so the tooltip asks it rather than the item.
                HoverHandler { id: thumbHover; cursorShape: Qt.PointingHandCursor }
                ToolTip.visible: thumbHover.hovered
                ToolTip.text: String(thumb.modelData.caption || "Figure on page " + thumb.modelData.page)
            }
        }

        // The figure itself, with the calibration drawn over it.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceAlt
            border.color: Theme.border
            clip: true

            Label {
                anchors.centerIn: parent
                visible: root.imagePath === ""
                width: parent.width * 0.7
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.figures.length > 0
                      ? "Choose a figure above."
                      : "Open a paper and extract it. Figures come out of the PDF "
                      + "with no model and no network — a reading model, if you "
                      + "turn one on, only fills in the axes for you."
                color: Theme.textMuted
            }

            Image {
                id: sheet
                anchors.fill: parent
                anchors.margins: 8
                source: root.imagePath === "" ? "" : "file:///" + root.imagePath
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true

                // Where the image is actually painted inside the item, so a
                // click can be turned back into a pixel of the original. With
                // PreserveAspectFit there is letterboxing on one axis, and
                // ignoring it puts every calibration out by the margin.
                readonly property real drawScale: (sheet.sourceSize.width > 0 && sheet.sourceSize.height > 0)
                    ? Math.min(sheet.width / sheet.sourceSize.width,
                               sheet.height / sheet.sourceSize.height) : 1
                readonly property real drawW: sheet.sourceSize.width * sheet.drawScale
                readonly property real drawH: sheet.sourceSize.height * sheet.drawScale
                readonly property real offX: (sheet.width - sheet.drawW) / 2
                readonly property real offY: (sheet.height - sheet.drawH) / 2

                function toImage(px, py) {
                    return Qt.point((px - sheet.offX) / Math.max(1e-6, sheet.drawScale),
                                    (py - sheet.offY) / Math.max(1e-6, sheet.drawScale))
                }
                function toView(ix, iy) {
                    return Qt.point(sheet.offX + ix * sheet.drawScale,
                                    sheet.offY + iy * sheet.drawScale)
                }

                // Drag draws the plot area; a plain click samples the colour
                // under the pointer. Two gestures on one surface, told apart by
                // whether the pointer moved - which is how every image editor
                // distinguishes a marquee from a pick.
                MouseArea {
                    id: picker
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.CrossCursor
                    property point startAt: Qt.point(0, 0)
                    property bool dragging: false

                    onPressed: (mouse) => {
                        picker.startAt = Qt.point(mouse.x, mouse.y)
                        picker.dragging = false
                    }
                    onPositionChanged: (mouse) => {
                        if (!picker.pressed) return
                        const dx = mouse.x - picker.startAt.x
                        const dy = mouse.y - picker.startAt.y
                        if (!picker.dragging && Math.abs(dx) + Math.abs(dy) < 6) return
                        picker.dragging = true
                        const a = sheet.toImage(Math.min(picker.startAt.x, mouse.x),
                                                Math.min(picker.startAt.y, mouse.y))
                        const b = sheet.toImage(Math.max(picker.startAt.x, mouse.x),
                                                Math.max(picker.startAt.y, mouse.y))
                        root.plotBox = Qt.rect(Math.round(a.x), Math.round(a.y),
                                               Math.round(b.x - a.x), Math.round(b.y - a.y))
                    }
                    onReleased: (mouse) => {
                        if (picker.dragging) { picker.dragging = false; return }
                        // A pick, not a drag. Sampled from the ORIGINAL image
                        // rather than from what is on screen: the preview is
                        // scaled and smoothed, so the pixel under the pointer
                        // there is a blend of its neighbours and the colour it
                        // reports is one the figure does not contain.
                        const at = sheet.toImage(mouse.x, mouse.y)
                        const c = root.app.figurePixel(root.imagePath,
                                                       Math.round(at.x), Math.round(at.y))
                        if (String(c) !== "#00000000") root.traceColour = c
                    }
                }

                // The plot area, drawn where it was dragged.
                Rectangle {
                    visible: root.boxDrawn && sheet.status === Image.Ready
                    color: "transparent"
                    border.color: Theme.accent
                    border.width: 2
                    x: sheet.toView(root.plotBox.x, root.plotBox.y).x
                    y: sheet.toView(root.plotBox.x, root.plotBox.y).y
                    width: root.plotBox.width * sheet.drawScale
                    height: root.plotBox.height * sheet.drawScale
                    // The four numbers, against the four edges they belong to,
                    // so the person is reading the printed axis and typing what
                    // it says in the place it says it.
                    Label {
                        anchors { right: parent.left; verticalCenter: parent.top; rightMargin: 4 }
                        text: root.yMaxText === "" ? "y max?" : root.yMaxText
                        color: root.yMaxText === "" ? Theme.warning : Theme.accent
                        font.pixelSize: 10; font.bold: true
                    }
                    Label {
                        anchors { right: parent.left; verticalCenter: parent.bottom; rightMargin: 4 }
                        text: root.yMinText === "" ? "y min?" : root.yMinText
                        color: root.yMinText === "" ? Theme.warning : Theme.accent
                        font.pixelSize: 10; font.bold: true
                    }
                    Label {
                        anchors { horizontalCenter: parent.left; top: parent.bottom; topMargin: 3 }
                        text: root.xMinText === "" ? "x min?" : root.xMinText
                        color: root.xMinText === "" ? Theme.warning : Theme.accent
                        font.pixelSize: 10; font.bold: true
                    }
                    Label {
                        anchors { horizontalCenter: parent.right; top: parent.bottom; topMargin: 3 }
                        text: root.xMaxText === "" ? "x max?" : root.xMaxText
                        color: root.xMaxText === "" ? Theme.warning : Theme.accent
                        font.pixelSize: 10; font.bold: true
                    }
                }
            }

            // What to do next, in order, and only the step that is next. A
            // panel that lists every requirement at once reads as a form to
            // fill in; one that says the next thing reads as a procedure.
            Rectangle {
                anchors { left: parent.left; bottom: parent.bottom; margins: 10 }
                visible: root.imagePath !== "" && !root.calibrated
                width: hint.implicitWidth + 16
                height: hint.implicitHeight + 10
                radius: Theme.radius
                color: Qt.rgba(0, 0, 0, 0.62)
                Label {
                    id: hint
                    anchors.centerIn: parent
                    color: Theme.text
                    font.pixelSize: 11
                    text: !root.boxDrawn
                          ? "Drag a box round the plot area — the axes box, not the whole figure"
                          : (root.asGrid
                             ? "Ready — this panel will be read as a grid of values"
                             : (!root.colourPicked
                                ? "Now click the line you want to read"
                                : "Now type what the axes run from and to"))
                }
            }
        }

        // The calibration, under the figure rather than in a panel across the
        // window: every field here is a number read off the picture directly
        // above it, and making the eye travel between the two is how a decimal
        // point ends up in the wrong place.
        GridLayout {
            Layout.fillWidth: true
            visible: root.imagePath !== "" && !root.asGrid
            columns: 6
            columnSpacing: 6
            rowSpacing: 4

            Label { text: "X axis"; color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
            TextField {
                Layout.fillWidth: true
                placeholderText: "from"
                font.pixelSize: 11
                text: root.xMinText
                onTextEdited: root.xMinText = text
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: "to"
                font.pixelSize: 11
                text: root.xMaxText
                onTextEdited: root.xMaxText = text
            }
            CheckBox {
                id: xLogBox
                text: "log"
                font.pixelSize: 10
                checked: root.xLog
                onToggled: root.xLog = xLogBox.checked
            }
            Label { text: "Colour"; color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
            Rectangle {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 22
                radius: 3
                color: root.colourPicked ? root.traceColour : "transparent"
                border.color: Theme.border
                Label {
                    anchors.centerIn: parent
                    visible: !root.colourPicked
                    text: "click"
                    color: Theme.textMuted
                    font.pixelSize: 9
                }
            }

            Label { text: "Y axis"; color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
            TextField {
                Layout.fillWidth: true
                placeholderText: "from"
                font.pixelSize: 11
                text: root.yMinText
                onTextEdited: root.yMinText = text
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: "to"
                font.pixelSize: 11
                text: root.yMaxText
                onTextEdited: root.yMaxText = text
            }
            CheckBox {
                id: yLogBox
                text: "log"
                font.pixelSize: 10
                checked: root.yLog
                onToggled: root.yLog = yLogBox.checked
            }
            // How near a pixel has to be to the chosen colour to count as part
            // of the line. Antialiasing, JPEG artefacts and a semi-transparent
            // grid under the curve all mean the line is not one exact colour,
            // so this is the control that decides whether a trace finds
            // nothing, finds the line, or finds the line and the grid too.
            Label { text: "Tolerance"; color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
            Slider {
                id: tol
                Layout.fillWidth: true
                Layout.columnSpan: 1
                from: 5; to: 140
                value: root.tolerance
                onMoved: root.tolerance = tol.value
                ToolTip.visible: tol.hovered
                ToolTip.text: "How close a pixel must be to the colour you clicked. "
                            + "Too low finds nothing on an antialiased line; too high "
                            + "picks up the grid and the neighbouring series."
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.imagePath !== ""
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: {
                    const last = root.app.lastReconstruction
                    if (last && last.rows > 0) {
                        if (String(last.kind || "") === "grid") {
                            const shape = last.shape || []
                            return "Read as a "
                                 + (shape.length > 1 ? shape[1] + " × " + shape[0] : "grid")
                                 + " field of values."
                        }
                        return last.rows + " points traced — written beside the figure as a "
                             + "CSV and a script that redraws it"
                    }
                    return root.calibrated ? "Ready." : "Calibrate the figure to read it."
                }
                color: root.app.lastReconstruction && root.app.lastReconstruction.rows > 0
                       ? Theme.positive : Theme.textMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            ComboBox {
                id: modeBox
                Layout.preferredWidth: 200
                model: ["Follow one line", "Read the panel as a grid"]
                currentIndex: root.asGrid ? 1 : 0
                onActivated: root.asGrid = (modeBox.currentIndex === 1)
                ToolTip.visible: modeBox.hovered
                ToolTip.text: "A line gives x and y for one series. A grid gives the whole "
                            + "panel as a field of values, which is what a heat map or an "
                            + "image panel actually is."
            }
            Button {
                id: traceButton
                text: root.asGrid ? "Read this panel" : "Read this figure"
                enabled: root.calibrated && !root.app.busy
                highlighted: true
                onClicked: root.asGrid ? root.readPanel() : root.trace()
                ToolTip.visible: traceButton.hovered
                ToolTip.text: root.calibrated
                              ? "Follow that colour across the plot area and turn it back "
                              + "into numbers, using the ranges you typed"
                              : "Draw the plot area, click the line, and type both axis ranges first"
            }
        }
    }
}
