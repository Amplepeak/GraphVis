// Publication Studio.
//
// This was a mock-up: a format list, a DPI list and an Export button that was
// permanently disabled behind a tooltip explaining why. It now exports.
//
// The important idea is that a journal figure is not a screen figure saved at a
// higher resolution. Each profile carries a column width in millimetres, a text
// size in points at final size, a minimum line weight and a resolution floor,
// and the figure is RE-RENDERED at those measurements rather than scaled to
// them - scaling a screen figure down to 89 mm shrinks its 8 pt labels to about
// 3 pt, which is how a figure comes back from a copy editor.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GraphVis

PanelScroll {
    id: root
    spacing: 10

    required property var app
    // The live canvas. Null until the Visualize workspace has built one, which
    // is why every control below checks it rather than assuming.
    property var canvas: null

    readonly property bool ready: canvas !== null && canvas.pointCount > 0
    property string profileName: "Nature single-column"
    readonly property var profile: canvas ? canvas.publicationProfile(root.profileName) : null

    FileDialog {
        id: figureDialog
        title: "Export figure"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Vector PDF (*.pdf)", "Scalable vector (*.svg)", "Raster image (*.png)"]
        onAccepted: {
            var path = selectedFile.toString().replace("file:///", "")
            var fmt = path.toLowerCase().endsWith(".svg") ? "svg"
                    : path.toLowerCase().endsWith(".png") ? "png" : "pdf"
            if (root.canvas.exportWithProfile(path, root.profileName, fmt))
                root.app.notify("Exported " + path)
            else
                root.app.notify("Export failed — see the status line")
        }
    }
    FileDialog {
        id: recipeDialog
        title: "Export GraphVis project recipe"
        fileMode: FileDialog.SaveFile
        nameFilters: ["GraphVis JSON recipe (*.json)"]
        onAccepted: root.app.exportProjectState(selectedFile)
    }


    Label {
        text: "Publication Studio"
        font.pixelSize: 17; font.bold: true
        color: Theme.text
        Layout.margins: 12
    }

    GvGroupBox {
        title: "Journal profile"
        Layout.fillWidth: true
        Layout.margins: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap

            ComboBox {
                Layout.fillWidth: true
                model: root.canvas ? root.canvas.publicationProfiles() : []
                currentIndex: Math.max(0, model.indexOf(root.profileName))
                onActivated: root.profileName = currentText
            }

            GridLayout {
                Layout.fillWidth: true
                visible: root.profile !== null
                columns: 2
                columnSpacing: Theme.gapWide
                rowSpacing: 3

                Label { text: "Width"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                Label {
                    text: root.profile ? root.profile.widthMm.toFixed(1) + " mm  ("
                                         + root.profile.widthIn.toFixed(2) + " in)" : ""
                    color: Theme.text; font.pixelSize: Theme.fontSizeSmall
                }
                Label { text: "Resolution"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                Label {
                    text: root.profile ? root.profile.dpi + " dpi" : ""
                    color: Theme.text; font.pixelSize: Theme.fontSizeSmall
                }
                Label { text: "Body text"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                Label {
                    text: root.profile ? root.profile.baseFontSize + " pt " + root.profile.fontFamily : ""
                    color: Theme.text; font.pixelSize: Theme.fontSizeSmall
                }
                Label { text: "Line weight"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                Label {
                    text: root.profile ? root.profile.lineWidth + " pt  ·  " + root.profile.colorSpace : ""
                    color: Theme.text; font.pixelSize: Theme.fontSizeSmall
                }
            }

            // The journal's own guidance, carried through from the profile.
            // It says what the baseline is and that the journal's current
            // instructions take precedence over any of it.
            Label {
                Layout.fillWidth: true
                visible: root.profile !== null && root.profile.notes !== ""
                text: root.profile ? root.profile.notes : ""
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    GvGroupBox {
        title: "Export"
        Layout.fillWidth: true
        Layout.leftMargin: 8; Layout.rightMargin: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                text: "The figure is re-drawn at the profile's measurements and from the full "
                    + "dataset — the on-screen point budget does not apply to an export."
            }

            Button {
                Layout.fillWidth: true
                text: "Export figure…"
                enabled: root.ready
                onClicked: figureDialog.open()
            }
            Label {
                Layout.fillWidth: true
                visible: !root.ready
                text: "Draw a graph first — there is nothing to export yet."
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                Button {
                    Layout.fillWidth: true
                    text: "Quick PDF"
                    enabled: root.ready
                    onClicked: {
                        var target = root.app.exportPath(root.canvas.engine, "pdf")
                        if (root.canvas.exportWithProfile(target, root.profileName, "pdf"))
                            root.app.notify("Exported " + target)
                        else
                            root.app.notify("PDF export failed")
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: "Quick SVG"
                    enabled: root.ready
                    onClicked: {
                        var target = root.app.exportPath(root.canvas.engine, "svg")
                        if (root.canvas.exportWithProfile(target, root.profileName, "svg"))
                            root.app.notify("Exported " + target)
                        else
                            root.app.notify("SVG export failed")
                    }
                }
            }
        }
    }

    GvGroupBox {
        title: "Reproducibility"
        Layout.fillWidth: true
        Layout.leftMargin: 8; Layout.rightMargin: 8; Layout.bottomMargin: 10

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap

            Button {
                Layout.fillWidth: true
                text: "Show the script that redraws this figure"
                enabled: root.ready
                onClicked: {
                    scriptText.text = root.canvas.reproducibleScript(root.profileName)
                    scriptSheet.open()
                }
            }
            Button {
                Layout.fillWidth: true
                text: "Export graph recipe + provenance"
                onClicked: recipeDialog.open()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                text: "The recipe is the Rust AppState snapshot, not settings reconstructed from "
                    + "the controls, so it records what was actually drawn."
            }
        }
    }

    Dialog {
        id: scriptSheet
        title: "Reproducible script"
        modal: true
        anchors.centerIn: parent
        width: Math.min(760, root.width - 40)
        height: Math.min(560, root.height - 40)
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                text: "Python and matplotlib, because that is what the reader of a methods section "
                    + "will have. GraphVis draws with its own backend, so tick placement will differ "
                    + "from the exported PDF; the data, the mapping and the geometry are exact."
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    id: scriptText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    font.family: "Consolas"
                    font.pixelSize: 12
                }
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: "Copy"
                onClicked: { scriptText.selectAll(); scriptText.copy(); scriptText.deselect() }
            }
        }
    }
}
