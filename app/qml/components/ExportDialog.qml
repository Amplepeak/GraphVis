pragma ComponentBehavior: Bound
// Everything about writing the figure out, in one place.
//
// There was one button that said "Export PDF" and wrote a 6 x 4 inch, 600 dpi
// PDF with no way to say otherwise - while PlotCanvas had three export
// functions with eight parameters between them, and QML called one of them
// with none. So the program could already write an SVG, a 300 dpi plate, a
// slide-sized PNG and a figure at a journal's exact column width, and none of
// that was reachable.
//
// Laid out as three questions in the order they are actually decided: what
// file, how big, how good. The summary at the bottom says what will be written
// before it is written, because "6 x 4 inches at 600 dpi" is not a thing most
// people can convert to pixels in their head, and the difference between a
// 2 MB figure and a 60 MB one is worth knowing beforehand.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GraphVis

Popup {
    id: root
    required property var app
    required property var canvas

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: 0

    // Vector formats have no pixels and no quality dial; raster ones have no
    // physical size until a dpi is chosen. One flag decides most of the layout.
    readonly property bool vector: formats.currentText === "pdf"
                                || formats.currentText === "svg"
    readonly property bool lossless: formats.currentText === "png"
                                  || formats.currentText === "tif"
                                  || formats.currentText === "tiff"
                                  || formats.currentText === "bmp"
    // The folder an export defaults to - the last one used, or
    // ~/Documents/GraphVis/exports until there has been one. Held here rather
    // than read from the controller on every binding so that Save as… can
    // change it for this export without committing to it until it is written.
    //
    // DELIBERATELY NOT a binding to app.exportDirectory, and that is worth a
    // line because it looks like one that has been forgotten. A binding here
    // would be destroyed by the first Save as… anyway - that is what assigning
    // to a bound property does - so declaring one would mean the property was
    // live until the user touched it and dead afterwards, which is the worst
    // of both and impossible to reason about from the declaration. Instead it
    // is plain state with exactly one rule: set from the controller whenever
    // the dialog opens, below.
    property string folder: ""
    readonly property string defaultStem:
        (canvas && canvas.engine ? canvas.engine : "figure")
            .replace(/[^A-Za-z0-9._-]+/g, "_")
    readonly property string targetPath:
        root.folder + "/" + (nameField.text.length > 0 ? nameField.text : root.defaultStem)
        + "." + formats.currentText
    readonly property bool willReplace: app.fileExists(root.targetPath)
    readonly property bool canBeTransparent: formats.currentText === "png"
                                          || formats.currentText === "webp"
                                          || formats.currentText === "tif"
                                          || formats.currentText === "tiff"

    readonly property real widthIn: widthField.text.length > 0
                                    ? Math.max(0.5, parseFloat(widthField.text) || 6.0) : 6.0
    readonly property real heightIn: heightField.text.length > 0
                                     ? Math.max(0.5, parseFloat(heightField.text) || 4.0) : 4.0
    readonly property int dpi: [96, 150, 300, 600, 1200][dpiBox.currentIndex]
    readonly property int pixelsWide: Math.round(root.widthIn * root.dpi)
    readonly property int pixelsHigh: Math.round(root.heightIn * root.dpi)

    // Roughly what lands on disk. Deliberately ROUGH and labelled as such: a
    // real figure compresses far better than a photograph, and a number
    // presented to three significant figures would be read as a promise.
    readonly property real estimateMb: root.vector
        ? Math.max(0.02, root.canvas.pointCount * 0.00002 + 0.05)
        : root.pixelsWide * root.pixelsHigh * (root.lossless ? 0.6 : 0.12)
          * (qualitySlider.value / 100.0 + 0.25) / 1048576.0

    background: Rectangle {
        color: Theme.background
        border.color: Theme.borderStrong
        radius: Theme.radius
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------------------------------------------------- header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Theme.surface
            radius: Theme.radius
            // Only the top corners are round; the square bottom butts against
            // the body instead of leaving two notches of background showing.
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: Theme.radius
                color: Theme.surface
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 10
                spacing: 10
                ColumnLayout {
                    spacing: 0
                    Label {
                        text: "Export figure"
                        color: Theme.text
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Label {
                        text: root.canvas.engine + "  ·  " + root.canvas.pointCount + " points"
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "✕"
                    flat: true
                    implicitWidth: 30
                    onClicked: root.close()
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 14

            // ------------------------------------------------------ format
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Label { text: "FORMAT"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ComboBox {
                        id: formats
                        // Asked, not assumed. The raster formats come from
                        // plugins that may not have been deployed, and a list
                        // that offers TIFF and then writes nothing is worse
                        // than one that never offered it.
                        model: root.canvas.exportFormats()
                        Layout.preferredWidth: 130
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        text: formats.currentText === "pdf"
                              ? "True vector with embedded fonts. What a journal wants."
                              : formats.currentText === "svg"
                                ? "Vector, and editable afterwards in Illustrator or Inkscape."
                                : root.lossless
                                  ? "Pixels, exactly as drawn. Larger files, no artefacts."
                                  : "Pixels, lossy. Small files; avoid for anything with text in it."
                    }
                }
            }

            // -------------------------------------------------------- size
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Label { text: "SIZE"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true }
                // The presets people actually need, in the units the thing they
                // are submitting to is specified in. A journal's column width
                // is the commonest reason to export at a size at all, and
                // 89 mm is not a number anyone remembers in inches.
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [
                            { name: "Nature 1-col",  w: 3.50, h: 2.60 },
                            { name: "Nature 2-col",  w: 7.20, h: 4.50 },
                            { name: "Elsevier 1-col",w: 3.54, h: 2.70 },
                            { name: "Slide 16:9",    w: 10.0, h: 5.63 },
                            { name: "Square",        w: 6.00, h: 6.00 },
                            { name: "Poster",        w: 16.0, h: 10.0 }
                        ]
                        delegate: Button {
                            required property var modelData
                            text: modelData.name
                            flat: true
                            implicitHeight: 24
                            font.pixelSize: 10
                            // Lit when the fields already hold this preset, so
                            // the row says which one is in force rather than
                            // only being a way to set one.
                            highlighted: Math.abs(root.widthIn - modelData.w) < 0.02
                                      && Math.abs(root.heightIn - modelData.h) < 0.02
                            onClicked: {
                                widthField.text = modelData.w.toFixed(2)
                                heightField.text = modelData.h.toFixed(2)
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label { text: "Width"; color: Theme.textSecondary; font.pixelSize: 11 }
                    TextField {
                        id: widthField
                        text: "6.00"
                        implicitWidth: 62
                        horizontalAlignment: Text.AlignRight
                        validator: DoubleValidator { bottom: 0.5; top: 100.0; decimals: 2 }
                    }
                    Label { text: "×"; color: Theme.textMuted }
                    TextField {
                        id: heightField
                        text: "4.00"
                        implicitWidth: 62
                        horizontalAlignment: Text.AlignRight
                        validator: DoubleValidator { bottom: 0.5; top: 100.0; decimals: 2 }
                    }
                    Label { text: "in"; color: Theme.textMuted; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: (root.widthIn * 25.4).toFixed(0) + " × "
                              + (root.heightIn * 25.4).toFixed(0) + " mm"
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }
                }
            }

            // ----------------------------------------------------- quality
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Label { text: "QUALITY"; color: Theme.textMuted; font.pixelSize: 9; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: "Resolution"; color: Theme.textSecondary; font.pixelSize: 11 }
                    ComboBox {
                        id: dpiBox
                        model: ["96 dpi  screen", "150 dpi  draft",
                                "300 dpi  print", "600 dpi  plate", "1200 dpi  archival"]
                        currentIndex: 3
                        Layout.preferredWidth: 168
                        // An SVG has no resolution at all - it is resolved when
                        // it is drawn. Offering a dpi for one would be a
                        // control that changes nothing.
                        enabled: formats.currentText !== "svg"
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        visible: !root.vector
                        text: root.pixelsWide + " × " + root.pixelsHigh + " px"
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: !root.vector
                    Label {
                        text: root.lossless ? "Compression" : "Image quality"
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                    Slider {
                        id: qualitySlider
                        from: 10; to: 100; stepSize: 5
                        value: 90
                        Layout.fillWidth: true
                    }
                    Label {
                        // Two different dials behind one slider, and they run
                        // opposite ways: for a lossless format the number is
                        // how hard to squeeze a file that will decode
                        // identically either way, and for a lossy one it is how
                        // much of the picture to keep. Saying which is which
                        // beats a bare percentage.
                        text: root.lossless
                              ? (qualitySlider.value >= 90 ? "smallest file"
                                 : qualitySlider.value >= 60 ? "balanced" : "fastest to write")
                              : qualitySlider.value + "%"
                        color: Theme.textMuted
                        font.pixelSize: 10
                        Layout.preferredWidth: 88
                    }
                }
                CheckBox {
                    id: transparentBox
                    text: "Transparent background"
                    visible: root.canBeTransparent
                    font.pixelSize: 11
                    ToolTip.visible: hovered
                    ToolTip.text: "Drops the background only. The ink, the grid and the "
                                + "axis labels are all still drawn, so the figure can sit "
                                + "on a slide that is not white."
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            // --------------------------------------------------- where it goes
            //
            // WHICH WAS NEVER ASKED, AND NEVER SHOWN. Every export went to a
            // hard-coded ~/Documents/GraphVis/exports under a name made from
            // the engine, and silently replaced the previous file of the same
            // engine and extension. "I exported it and I do not know where it
            // went" is the correct reaction to a dialog that asks how many dots
            // per inch and not where to put them.
            //
            // The folder is shown, it defaults to the last one used, and Save
            // as… opens a real chooser. The file name is editable, because a
            // name derived from the engine means every figure of the same kind
            // lands on top of the last one.
            Label {
                text: "WHERE"
                color: Theme.textMuted
                font.pixelSize: 10
                font.letterSpacing: 1
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    text: root.defaultStem
                    selectByMouse: true
                    placeholderText: "file name"
                }
                Label {
                    text: "." + formats.currentText
                    color: Theme.textSecondary
                }
                Button {
                    text: "Save as…"
                    onClicked: saveAs.open()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    text: root.folder
                    color: Theme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                    // The whole path, for one that has been elided.
                    ToolTip.visible: folderHover.hovered
                    ToolTip.text: root.folder
                    HoverHandler { id: folderHover }
                }
                Label {
                    visible: root.willReplace
                    text: "replaces a file already there"
                    color: Theme.warning !== undefined ? Theme.warning : Theme.textSecondary
                    font.pixelSize: 10
                }
            }

            // ----------------------------------------------------- summary
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                ColumnLayout {
                    spacing: 1
                    Label {
                        text: root.vector
                              ? "Vector · " + root.widthIn.toFixed(2) + " × "
                                + root.heightIn.toFixed(2) + " in"
                              : root.pixelsWide + " × " + root.pixelsHigh + " px at "
                                + root.dpi + " dpi"
                        color: Theme.text
                        font.pixelSize: 11
                        font.bold: true
                    }
                    Label {
                        text: "about " + (root.estimateMb < 1
                                          ? (root.estimateMb * 1024).toFixed(0) + " KB"
                                          : root.estimateMb.toFixed(1) + " MB")
                              + (root.canvas.previewIsExact ? "" : "  ·  drawn from every row")
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Cancel"
                    flat: true
                    onClicked: root.close()
                }
                Button {
                    id: go
                    text: "Export"
                    highlighted: true
                    enabled: root.canvas.pointCount > 0
                    onClicked: root.writeIt()
                }
            }
        }
    }

    // One place that knows which function each format needs, so the button
    // above does not grow a chain of ifs and the three export paths cannot
    // drift apart in what they are given.
    function writeIt() {
        var ext = formats.currentText
        // The path the person can SEE in the dialog, not one computed again
        // somewhere else. Two ideas of where the file goes is how a dialog
        // comes to name one folder and write to another.
        var target = root.targetPath
        var ok = false
        if (ext === "pdf")
            ok = root.canvas.exportPdf(target, root.widthIn, root.heightIn, root.dpi)
        else if (ext === "svg")
            ok = root.canvas.exportSvg(target, root.widthIn, root.heightIn)
        else
            ok = root.canvas.exportRaster(target, root.pixelsWide, root.pixelsHigh,
                                          qualitySlider.value,
                                          root.canBeTransparent && transparentBox.checked)
        root.app.notify(ok ? ("Exported to " + target)
                           : (ext.toUpperCase() + " export failed"))
        // Remembered only on success, and only after the write: a folder that
        // could not be written to is not a folder to default to next time.
        if (ok) {
            root.app.rememberExportDirectory(root.folder)
            root.close()
        }
    }

    // Reset to the remembered folder each time the dialog opens, so a Save as…
    // that was cancelled does not leave the next export pointing somewhere the
    // person did not choose. This is the ONE place `folder` gets its value
    // from the controller - see the declaration.
    onOpened: root.folder = root.app.exportDirectory

    // ...and once at creation, because the path row and the replace warning are
    // bound to `folder` and are evaluated when the dialog's contents are built,
    // which happens before onOpened. Without this they would flash an empty
    // folder and ask fileExists() about a path with no directory in it.
    Component.onCompleted: root.folder = root.app.exportDirectory

    FileDialog {
        id: saveAs
        title: "Export the figure to…"
        fileMode: FileDialog.SaveFile
        currentFolder: root.app.suggestedExportUrl(root.defaultStem,
                                                   formats.currentText)
        onAccepted: {
            var chosen = root.app.localPathOf(selectedFile)
            var cut = Math.max(chosen.lastIndexOf("/"), chosen.lastIndexOf("\\"))
            if (cut > 0) {
                root.folder = chosen.substring(0, cut)
                var base = chosen.substring(cut + 1)
                var dot = base.lastIndexOf(".")
                nameField.text = dot > 0 ? base.substring(0, dot) : base
            }
        }
    }
}
