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
        var target = root.app.exportPath(root.canvas.engine, ext)
        var ok = false
        if (ext === "pdf")
            ok = root.canvas.exportPdf(target, root.widthIn, root.heightIn, root.dpi)
        else if (ext === "svg")
            ok = root.canvas.exportSvg(target, root.widthIn, root.heightIn)
        else
            ok = root.canvas.exportRaster(target, root.pixelsWide, root.pixelsHigh,
                                          qualitySlider.value,
                                          root.canBeTransparent && transparentBox.checked)
        root.app.notify(ok ? ("Exported " + target)
                           : (ext.toUpperCase() + " export failed"))
        if (ok) root.close()
    }
}
