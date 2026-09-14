// One click from a figure on screen to a block you can paste into a paper.
//
// The image export already existed - exportPdf, exportSvg, exportPng, and
// exportWithProfile to re-render at a journal's real measurements. What was
// missing is everything around the image: the float, the width, the caption,
// the label, and the fact that every style puts them in a different order. APA
// 7 captions ABOVE the figure and puts the note below; IEEE, Nature and Harvard
// caption below. That is why the style here changes the block rather than only
// the reference list.
//
// The block is built by the science service, not here, so there is exactly one
// implementation of the layout rules. A second copy in QML would be a second
// thing to keep in step, and it is the kind of copy that quietly stops matching.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ColumnLayout {
    id: root
    required property var app
    required property var canvas
    property string profileName: ""
    spacing: 6

    readonly property var styles: [
        { label: "APA 7",   key: "apa"     },
        { label: "IEEE",    key: "ieee"    },
        { label: "Nature",  key: "nature"  },
        { label: "Harvard", key: "harvard" }
    ]
    readonly property var formats: [
        { label: "Vector PDF", key: "pdf" },
        { label: "SVG",        key: "svg" },
        { label: "PNG",        key: "png" }
    ]

    // Only this operation's reply, so a statistics result from a moment ago
    // cannot appear under a button labelled LaTeX.
    readonly property var block: (root.app.analysisResult
                                  && root.app.analysisResult.operation === "latex_figure")
                                 ? root.app.analysisResult : null
    property string savedTo: ""

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Label { text: "Style"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
        ComboBox {
            id: styleBox
            Layout.fillWidth: true
            textRole: "label"
            model: root.styles
            currentIndex: 0
        }
        Label { text: "Format"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
        ComboBox {
            id: formatBox
            Layout.preferredWidth: 108
            textRole: "label"
            model: root.formats
            currentIndex: 0
        }
    }

    TextField {
        id: captionField
        Layout.fillWidth: true
        placeholderText: "Caption — what the figure shows"
    }
    TextField {
        id: noteField
        Layout.fillWidth: true
        placeholderText: "Note (optional) — error bars, n, exclusions"
    }

    Button {
        id: buildButton
        Layout.fillWidth: true
        highlighted: true
        enabled: root.canvas && !root.app.busy
        text: "Save the figure and build the snippet"
        onClicked: {
            const fmt = root.formats[formatBox.currentIndex].key
            const base = String(root.canvas.engine || "figure").replace(/[^A-Za-z0-9]+/g, "_")
            const target = root.app.exportPath(base, fmt)
            // Re-rendered at the journal's measurements when a profile is set,
            // rather than scaled - scaling an 8 pt label down to 89 mm is how a
            // figure comes back from a copy editor.
            const wrote = (root.profileName !== "" && root.canvas.exportWithProfile)
                          ? root.canvas.exportWithProfile(target, root.profileName, fmt)
                          : (fmt === "svg" ? root.canvas.exportSvg(target)
                             : fmt === "png" ? root.canvas.exportPng(target)
                                             : root.canvas.exportPdf(target))
            if (!wrote) return
            root.savedTo = target
            // requiresDataset false: a caption reads no columns, and a Function
            // Plot has no dataset at all.
            root.app.runAnalysis("latex_figure", {
                "image_path": target,
                "caption": captionField.text,
                "note": noteField.text,
                "style": root.styles[styleBox.currentIndex].key,
                "dataset": String(root.app.activeTableName || "")
            }, false)
        }
    }

    Label {
        Layout.fillWidth: true
        visible: root.savedTo !== ""
        text: "Saved " + root.savedTo
        color: Theme.textMuted
        font.pixelSize: 10
        elide: Text.ElideMiddle
    }

    // The snippet. Selectable and read-only: it is output, and an editable box
    // invites changes that the next rebuild silently discards.
    TextArea {
        id: snippet
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        readOnly: true
        wrapMode: TextEdit.NoWrap
        font.family: "Consolas, monospace"
        font.pixelSize: 11
        visible: root.block !== null
        text: root.block ? String(root.block.latex || "") : ""
        background: Rectangle {
            color: Theme.surfaceAlt
            border.color: Theme.border
            radius: Theme.radius
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.block !== null
        spacing: 6
        Button {
            text: "Copy"
            // selectAll + copy rather than a clipboard API: TextEdit already
            // owns one, and adding a C++ clipboard invokable for a control that
            // has it built in is a second way to do one thing.
            onClicked: { snippet.selectAll(); snippet.copy(); snippet.deselect() }
        }
        Label {
            Layout.fillWidth: true
            text: root.block
                  ? "Refer to it as " + String(root.block.reference || "")
                    + "  ·  needs " + String(root.block.preamble || "")
                  : ""
            color: Theme.textMuted
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }

    Label {
        Layout.fillWidth: true
        visible: root.app.analysisResult
                 && root.app.analysisResult.operation === undefined
                 && String(root.app.analysisResult.error || "") !== ""
        text: root.app.analysisResult ? String(root.app.analysisResult.error || "") : ""
        color: Theme.warning
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }
}
