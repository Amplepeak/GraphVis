// The Literature workspace reads papers, it does not render them.
//
// It previously imported QtQuick.Pdf for PdfDocument/PdfMultiPageView. That
// module ships only with the Qt PDF module, which this Qt build does not have,
// and a missing import fails the whole type - so ClassicShell and then Main
// failed to load with:
//   module "QtQuick.Pdf" is not installed
// Extraction is done natively by app.analyzeLiterature(), so no PDF rendering
// is needed here and the import is gone.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis
// Without this the four GroupBoxes below silently fell back to the stock
// control instead of GvGroupBox, which exists precisely because the Basic
// style draws the label on the frame line with no background - on a dark
// palette that reads as struck-through text.
import "../components"

Rectangle {
    id: root
    required property var app
    signal openRequested()
    color: Theme.background

    readonly property bool hasPaper: app.literatureUrl.toString() !== ""
    readonly property string paperName: hasPaper ? app.literatureUrl.toString().split("/").pop() : ""
    readonly property bool analysed: app.literatureAnalysis.ok === true

    ListModel { id: annotations }

    RowLayout {
        anchors.fill: parent; anchors.margins: 8; spacing: 8

        Rectangle {
            Layout.preferredWidth: 250; Layout.fillHeight: true; radius: 10; color: Theme.surface; border.color: Theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 10
                Label { text: "LIBRARY"; color: Theme.textMuted; font.pixelSize: 11; font.bold: true }
                Button { text: "Open literature…"; Layout.fillWidth: true; onClicked: root.openRequested() }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                Label { text: root.hasPaper ? root.paperName : "No paper open"; color: Theme.text; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: root.analysed ? "Extraction complete" : (root.hasPaper ? "Not analysed yet" : ""); color: Theme.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                GvGroupBox {
                    title: "Research tools"; Layout.fillWidth: true
                    ColumnLayout { anchors.fill: parent
                        Button { text: "Extract figures & tables"; Layout.fillWidth: true; enabled: root.hasPaper && !root.app.busy; onClicked: root.app.analyzeLiterature() }
                        Button { text: "Use extracted dataset"; Layout.fillWidth: true; enabled: root.analysed; onClicked: root.app.importFirstLiteratureDataset() }
                        Button { text: "Recreate selected graph"; Layout.fillWidth: true; enabled: false; ToolTip.visible: hovered; ToolTip.text: "Enable after selecting a detected figure and completing axis calibration" }
                    }
                }
                // Reading a paper with a model. OFF, and folded away, because
                // everything above this works without it: text, tables, figures
                // and the recommender are all local, and that is the product
                // rather than a fallback.
                //
                // What this adds is the one thing local parsing cannot do -
                // read numbers back off a published chart that came with no
                // table - and it is deliberately any-provider: an
                // OpenAI-compatible endpoint of the person's choosing, or a
                // model on their own machine.
                GvGroupBox {
                    title: "Reading help (optional)"
                    collapsed: true
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6

                        ComboBox {
                            id: aiProvider
                            Layout.fillWidth: true
                            model: root.app.literatureAiProviderNames
                            currentIndex: root.app.literatureAiProvider
                            onActivated: root.app.literatureAiProvider = currentIndex
                        }

                        // What leaves the computer, stated where the choice is
                        // made rather than in a manual nobody opens. Someone
                        // working on unpublished data needs this sentence
                        // before they pick an endpoint, not after.
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: root.app.literatureAiProvider === 1 ? Theme.warning : Theme.textMuted
                            text: root.app.literatureAiProvider === 0
                                  ? "Nothing leaves this computer. Papers are read here."
                                  : (root.app.literatureAiProvider === 1
                                     ? "Each figure image and its caption are uploaded to the endpoint below. Do not use a hosted endpoint for unpublished figures you cannot share."
                                     : "The model runs on this computer. Nothing is uploaded; the first use of a session takes a while to load.")
                        }

                        TextField {
                            visible: root.app.literatureAiProvider === 1
                            Layout.fillWidth: true
                            placeholderText: "https://…/v1/chat/completions"
                            text: root.app.literatureAiEndpoint
                            onEditingFinished: root.app.literatureAiEndpoint = text
                        }
                        TextField {
                            visible: root.app.literatureAiProvider > 0
                            Layout.fillWidth: true
                            placeholderText: root.app.literatureAiProvider === 1
                                             ? "model name" : "local model id"
                            text: root.app.literatureAiModel
                            onEditingFinished: root.app.literatureAiModel = text
                        }

                        // WHERE the key is, never the key. A settings file is
                        // plain text in AppData and ends up in every backup and
                        // support bundle; a key typed into one is a key leaked
                        // by all of them.
                        Label {
                            visible: root.app.literatureAiProvider === 1
                            text: "API key — GraphVis reads it, never stores it"
                            color: Theme.textSecondary
                            font.pixelSize: 11
                        }
                        TextField {
                            visible: root.app.literatureAiProvider === 1
                            Layout.fillWidth: true
                            placeholderText: "environment variable name"
                            text: root.app.literatureAiKeyEnv
                            onEditingFinished: root.app.literatureAiKeyEnv = text
                        }
                        TextField {
                            visible: root.app.literatureAiProvider === 1
                            Layout.fillWidth: true
                            placeholderText: "…or a file holding the key"
                            text: root.app.literatureAiKeyFile
                            onEditingFinished: root.app.literatureAiKeyFile = text
                        }
                        Label {
                            visible: root.app.literatureAiProvider === 1
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: root.app.literatureAiKeyPresent ? Theme.positive : Theme.warning
                            text: root.app.literatureAiKeyPresent
                                  ? "Key found." : "No key found where those point."
                        }

                        RowLayout {
                            visible: root.app.literatureAiProvider > 0
                            Layout.fillWidth: true
                            Button {
                                text: "Test"
                                enabled: !root.app.busy
                                onClicked: root.app.testLiteratureAi()
                                ToolTip.visible: hovered
                                ToolTip.text: "Sends a 16×16 blank image, not your paper, and reports what came back."
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                color: Theme.textSecondary
                                text: root.app.literatureAiStatus
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                // "AI/science service" was one label for two unrelated things.
                // The science add-on is Python doing deterministic parsing; the
                // reader above is the only part with a model in it, and it is
                // off. Saying "AI ready" of the first is how someone concludes
                // their paper has been sent somewhere.
                Label {
                    text: root.app.scienceServiceAvailable
                          ? "Science add-on ready · reading help " + (root.app.literatureAiProvider === 0 ? "off" : "on")
                          : "Reading papers on this computer · science add-on not installed"
                    color: root.app.scienceServiceAvailable ? Theme.positive : Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: 10; color: Theme.surface; border.color: Theme.border
            ColumnLayout {
                anchors.fill: parent; spacing: 0

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 50; color: Theme.background
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 7
                        Label { text: root.hasPaper ? root.paperName : "Literature Reader"; color: Theme.text; elide: Text.ElideMiddle; Layout.maximumWidth: 420 }
                        Item { Layout.fillWidth: true }
                        Button { text: "Analyze"; enabled: root.hasPaper && !root.app.busy; onClicked: root.app.analyzeLiterature() }
                        Button { text: "Close paper"; enabled: root.hasPaper; onClicked: root.app.clearLiterature() }
                    }
                }

                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true

                    ColumnLayout {
                        anchors.centerIn: parent; width: parent.width * 0.7; spacing: 14
                        visible: root.hasPaper
                        Label { Layout.alignment: Qt.AlignHCenter; text: root.paperName; font.pixelSize: 26; font.bold: true; color: Theme.text; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label {
                            Layout.alignment: Qt.AlignHCenter; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; color: Theme.textSecondary
                            text: root.app.busy && root.app.workspaceMode === "Literature" ? root.app.busyLabel
                                  : root.analysed ? "Extraction complete. Send the data to the workspace to plot it."
                                  : "Run Analyze to extract figures, tables and numeric series from this paper."
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter; color: Theme.positive; visible: root.analysed
                            text: (root.app.literatureAnalysis.datasets ? root.app.literatureAnalysis.datasets.length + " numeric datasets" : "")
                                  + (root.app.literatureAnalysis.text_chars ? " · " + root.app.literatureAnalysis.text_chars + " characters indexed" : "")
                        }
                        Button { Layout.alignment: Qt.AlignHCenter; text: "Send extracted data to workspace"; enabled: root.analysed; onClicked: root.app.importFirstLiteratureDataset() }
                    }

                    Column {
                        anchors.centerIn: parent; spacing: 14; visible: !root.hasPaper
                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Read Literature"; font.pixelSize: 38; font.bold: true; color: Theme.text }
                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Open a scientific paper to extract data and reconstruct figures."; color: Theme.textSecondary }
                        Button { anchors.horizontalCenter: parent.horizontalCenter; text: "Open literature"; onClicked: root.openRequested() }
                    }

                    DropArea {
                        anchors.fill: parent
                        onDropped: (drop) => { if (drop.hasUrls && drop.urls.length > 0) root.app.openLiterature(drop.urls[0]) }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 330; Layout.fillHeight: true; radius: 10; color: Theme.background; border.color: Theme.border
            ScrollView {
                anchors.fill: parent
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: "LITERATURE INTELLIGENCE"; color: Theme.textMuted; font.pixelSize: 11; font.bold: true; Layout.margins: 12 }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                    Label { text: root.app.busy && root.app.workspaceMode === "Literature" ? root.app.busyLabel : (root.analysed ? "Extraction complete" : "Select a figure, table or method section to inspect it."); color: root.analysed ? Theme.positive : Theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12 }
                    GvGroupBox {
                        title: "Detected content"; Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                        ColumnLayout { anchors.fill: parent
                            Label { text: root.app.literatureAnalysis.datasets ? root.app.literatureAnalysis.datasets.length + " numeric datasets" : "Figures · tables · equations"; color: Theme.textSecondary }
                            Label { text: root.app.literatureAnalysis.text_chars ? root.app.literatureAnalysis.text_chars + " text characters indexed" : "Method and unit links appear here"; color: Theme.textMuted; wrapMode: Text.WordWrap }
                        }
                    }
                    GvGroupBox {
                        title: "Actions"; Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                        ColumnLayout { anchors.fill: parent
                            Button { text: "Analyze paper"; Layout.fillWidth: true; enabled: !root.app.busy && root.hasPaper; onClicked: root.app.analyzeLiterature() }
                            Button { text: "Reconstruct graph"; Layout.fillWidth: true; enabled: false; ToolTip.visible: hovered; ToolTip.text: "Select a detected figure before reconstruction" }
                            Button { text: "Compare with active dataset"; Layout.fillWidth: true; enabled: false; ToolTip.visible: hovered; ToolTip.text: "Available after a reconstructed literature series is selected" }
                            Button { text: "Send extracted data to workspace"; Layout.fillWidth: true; enabled: root.analysed; onClicked: root.app.importFirstLiteratureDataset() }
                        }
                    }
                    GvGroupBox {
                        title: "Notes"; Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                        ColumnLayout { anchors.fill: parent
                            TextArea { id: noteEditor; placeholderText: "Add a note about this paper…"; Layout.fillWidth: true; Layout.preferredHeight: 100; wrapMode: TextEdit.Wrap }
                            RowLayout {
                                Button { text: "Add note"; enabled: noteEditor.text.trim().length > 0; onClicked: { annotations.append({kind: "Note", body: noteEditor.text.trim()}); noteEditor.clear(); root.app.notify("Annotation added") } }
                                Button { text: "Clear notes"; enabled: annotations.count > 0; onClicked: annotations.clear() }
                            }
                            Repeater {
                                model: annotations
                                delegate: Label {
                                    // Declared rather than looked up. The role
                                    // was called "text", which is also Label's
                                    // own property, so an unqualified read was
                                    // ambiguous; the role is "body" now.
                                    required property string kind
                                    required property string body
                                    text: kind + " \u2014 " + body
                                    color: Theme.textMuted
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                    Label { text: root.app.scienceServiceInstallHint(); color: Theme.textMuted; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; Layout.margins: 12 }
                }
            }
        }
    }
}
