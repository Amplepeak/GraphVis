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

    // FIFTY-ONE STYLES, AND NOT A LIST WRITTEN HERE.
    //
    // This was four entries, hand-kept, beside a formatter that had its own
    // set - which is how a style could be offered by the picker and then not
    // applied by the service. The list now comes from the same generated file
    // the formatter's table is written to, so the two cannot come apart; see
    // tools/make_citation_styles.py and the audit check that fails when they
    // disagree.
    //
    // The first three are the three this program's users actually use, and the
    // rest are alphabetical. That ordering is decided in the table, not here.
    readonly property var styles: root.app.citationStyles
    readonly property var chosenStyle: (styleBox.currentIndex >= 0
                                        && styleBox.currentIndex < root.styles.length)
                                       ? root.styles[styleBox.currentIndex] : null
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
            // SIZED TO THE LONGEST NAME IN IT, not to whatever the row has
            // left over. With four entries the popup happened to fit; with
            // fifty-one, several names are longer than the field and were
            // being drawn with their ends cut off - and a style list that
            // elides "Chicago Notes & Bibliogra…" is a list you cannot choose
            // from.
            //
            // The popup is allowed to be wider than the control, which is
            // normal for a picker and is why the panel does not have to be.
            popup.width: Math.max(styleBox.width, styleBox.implicitPopupWidth)
            readonly property real implicitPopupWidth: {
                var widest = 0
                for (var i = 0; i < root.styles.length; ++i) {
                    const w = styleMetrics.advanceWidth(String(root.styles[i].label))
                    if (w > widest) widest = w
                }
                // Room for the text, the padding either side and the scrollbar
                // a list this long will always have.
                return Math.min(widest + 48, 460)
            }
            // FontMetrics, NOT TextMetrics, and the difference is not cosmetic.
            //
            // TextMetrics measures ONE string - the one in its `text` property -
            // and exposes `advanceWidth` as a read-only PROPERTY of it.
            // FontMetrics measures the FONT and offers `advanceWidth(text)` as a
            // METHOD, which is what the loop above needs: it asks about
            // fifty-one different labels.
            //
            // Written as TextMetrics, the call above is a property being
            // invoked - "TypeError: Property 'advanceWidth' of object
            // QQuickTextMetrics is not a function", twice on every startup. QML
            // resolves that at run time, so it is not a syntax error and
            // qmllint passes it; the popup simply fell back to the control's own
            // width and went on eliding the longer style names, which is the
            // exact fault the block above exists to fix.
            //
            // ColourMapSelector.qml has done this correctly with FontMetrics
            // since it was written. This is the same code written beside a
            // working copy of itself and not carried across - the same root
            // cause as the export-filter crash in AppController.cpp.
            FontMetrics { id: styleMetrics; font: styleBox.font }
            // Fifty-one rows is more than a screen, so the popup scrolls and
            // opens on the current entry rather than at the top.
            popup.contentItem: ListView {
                clip: true
                implicitHeight: Math.min(contentHeight, 420)
                model: styleBox.delegateModel
                currentIndex: styleBox.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
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

    // WHAT THE RULES ARE BASED ON, said where the choice is made.
    //
    // Some of these fifty-one are published manuals and some are a name people
    // use for a family of styles that has no single manual - "Harvard" and
    // "Oxford referencing" chief among them. Formatting to one of those and
    // saying nothing invites a person to hand in a reference list their
    // department will reject, so the picker says which kind it is.
    Label {
        Layout.fillWidth: true
        visible: !!(root.chosenStyle && root.chosenStyle.caveat)
        text: root.chosenStyle ? String(root.chosenStyle.caveat || "") : ""
        color: Theme.warning
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        visible: !!(root.chosenStyle && root.chosenStyle.source === "library"
                    && !root.chosenStyle.caveat)
        text: "Formatted from a university library guide rather than from the "
              + "style's own manual, which is not public. Check one reference "
              + "against your target journal before you submit."
        color: Theme.textMuted
        font.pixelSize: 10
        wrapMode: Text.WordWrap
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
        enabled: !!(root.canvas && !root.app.busy)
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
                "style": root.chosenStyle ? String(root.chosenStyle.key) : "apa",
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
        visible: !!(root.app.analysisResult && root.app.analysisResult.operation === undefined && String(root.app.analysisResult.error || "") !== "")
        text: root.app.analysisResult ? String(root.app.analysisResult.error || "") : ""
        color: Theme.warning
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }
}
