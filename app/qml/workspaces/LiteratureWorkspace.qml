// The Literature workspace reads papers, it does not render them.
//
// It previously imported QtQuick.Pdf for PdfDocument/PdfMultiPageView. That
// module ships only with the Qt PDF module, which this Qt build does not have,
// and a missing import fails the whole type - so ClassicShell and then Main
// failed to load with:
//   module "QtQuick.Pdf" is not installed
// Extraction is done natively by app.analyzeLiterature(), so no PDF rendering
// is needed here and the import is gone.
//
// Bound, because this file now has Repeater delegates that reach the file's
// root id. Without it those resolve through the old unbound context lookup,
// which works until a model role happens to share a name and then silently
// resolves to the wrong thing.
pragma ComponentBehavior: Bound
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

    // ONE ACTION, ONE GATE, ONE SENTENCE.
    //
    // Reported from the built application: "why is the recreat greyed out when
    // i selected one of the graphs also it looks liek hte buttons on both side
    // greyed out might do the same thing or something".
    //
    // They did. The Library panel's "Research tools" group was the Literature
    // intelligence panel's "Actions" group written a second time: three buttons
    // calling the same three functions under different names -
    //
    //   Extract figures and tables  =  Analyze paper           analyzeLiterature()
    //   Recreate selected graph     =  Reconstruct graph       lab.trace()
    //   Use extracted dataset       =  Send extracted data …   importFirstLiteratureDataset()
    //
    // - with separately written enabled expressions, which had already drifted.
    // "Use extracted dataset" was enabled by `analysed` alone: the exact
    // condition that was wrong on its twin and was reported as a broken button.
    // Fixing one copy of a rule fixes one copy of the rule.
    //
    // So the conditions live here, once, and both panels read them. The reason
    // a button is grey is a property too, not a tooltip: a tooltip explains a
    // disabled control only to someone who already suspects it is waiting
    // rather than broken, and the person who asked the question above did not,
    // because nothing on screen suggested it.
    readonly property bool canExtract: root.hasPaper && !root.app.busy

    readonly property bool canTrace: root.analysed && lab.calibrated && !root.app.busy
    // The location matters as much as the list. The person HAD selected a
    // figure; the things still missing are asked for by controls UNDER that
    // figure, in the middle pane - which is not where they were looking when a
    // button on the far side of the window refused them.
    readonly property string traceBlocked: {
        if (!root.analysed) return "Extract the paper first."
        if (root.canTrace || lab.missingForCalibration === "") return ""
        if (lab.imagePath === "") return "Still needs " + lab.missingForCalibration + "."
        return "Still needs " + lab.missingForCalibration
               + " — the controls for these are under the figure itself, in the "
               + "middle of the window."
    }

    readonly property bool canSend: root.analysed
                                    && root.app.literatureSummary.drawable > 0
    readonly property string sendBlocked: {
        if (!root.analysed || root.canSend) return ""
        if (!(root.app.literatureSummary.tables > 0))
            return "Nothing has been extracted from this paper yet."
        return "Nothing to send: none of the " + root.app.literatureSummary.tables
               + " extracted tables has two numeric columns over three or more "
               + "rows. Use Reconstruct graph to read numbers off a figure instead."
    }

    ListModel { id: annotations }

    // What was extracted BEFORE this session.
    //
    // Every extraction already wrote a sidecar beside its dataset, and the
    // service has always been able to read them back - literature.extractions -
    // and nothing ever called it. So closing the program threw away every
    // extraction it had made: the work survived on disk and the interface could
    // not see it. Asked for once, when the workspace is opened.
    Component.onCompleted: root.app.loadPastExtractions()

    // Three panes whose widths are the person's business.
    //
    // This was a RowLayout of fixed widths - 250, whatever is left, 330 - so
    // the side panels could be neither resized nor moved, and on a narrow
    // window their contents were simply cut off at the edge: a button reading
    // "Compare with active data" with the rest of the word past the border.
    // A SplitView gives each divider a handle, and the two side panes are
    // DockPanels, so they can also be torn out into windows of their own the
    // way the visualise workspace's panels can.
    SplitView {
        anchors.fill: parent; anchors.margins: 8
        orientation: Qt.Horizontal
        // "A SplitView gives each divider a handle" was only half true: the
        // default handle is a single pixel, so the dividers here were as hard
        // to find as the one between the sidebar and the figure.
        handle: GvSplitHandle {}

        DockPanel {
            title: "Library"
            SplitView.preferredWidth: 260
            SplitView.minimumWidth: 150
            floatingWidth: 360; floatingHeight: 640
            Rectangle {
            anchors.fill: parent
            radius: 10; color: Theme.surface; border.color: Theme.border
            // PanelScroll rather than a bare column: this panel now carries a
            // reader configuration as well as the library, and on a laptop
            // screen that is taller than the pane. A column with no scroll does
            // not get shorter, it gets cut.
            PanelScroll {
                anchors.fill: parent; anchors.margins: 12; contentSpacing: 10
                Label { text: "LIBRARY"; color: Theme.textMuted; font.pixelSize: 11; font.bold: true }
                Button { text: "Open literature…"; Layout.fillWidth: true; onClicked: root.openRequested() }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                Label { text: root.hasPaper ? root.paperName : "No paper open"; color: Theme.text; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: root.analysed ? "Extraction complete" : (root.hasPaper ? "Not analysed yet" : ""); color: Theme.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                // Papers this program has read before. Opening one puts it
                // back where it was rather than extracting it again, which on
                // a scanned paper is minutes saved.
                GvGroupBox {
                    title: "Extracted before"
                    Layout.fillWidth: true
                    collapsed: true
                    visible: root.app.pastExtractions.length > 0
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        Repeater {
                            model: root.app.pastExtractions
                            delegate: Button {
                                id: pastButton
                                required property var modelData
                                Layout.fillWidth: true
                                flat: true
                                text: {
                                    const t = pastButton.modelData.title
                                    if (t !== undefined && String(t) !== "") return String(t)
                                    const p = String(pastButton.modelData.path || pastButton.modelData)
                                    return p.split(/[\\/]/).pop()
                                }
                                ToolTip.visible: pastButton.hovered
                                ToolTip.text: String(pastButton.modelData.path || pastButton.modelData)
                                onClicked: {
                                    const chosen = String(pastButton.modelData.path
                                                          || pastButton.modelData)
                                    if (chosen !== "") root.app.openLiterature("file:///" + chosen)
                                }
                            }
                        }
                    }
                }

                // The one action this panel is the natural home of: it follows
                // "Open literature…" at the top, and it is what turns a paper
                // into everything the other two panels then work on.
                //
                // The two buttons that used to sit beneath it - "Use extracted
                // dataset" and "Recreate selected graph" - were the same calls
                // as "Send extracted data to workspace" and "Reconstruct graph"
                // on the right-hand side, and are gone rather than duplicated
                // under a second name. See the gate properties at the top of
                // this file for why. They act on a figure and on an extraction,
                // both of which are shown on the other side of the window, so
                // that is where their buttons belong.
                GvGroupBox {
                    title: "Read this paper"; Layout.fillWidth: true
                    ColumnLayout { anchors.fill: parent
                        // "&" in a Button is a MNEMONIC, not an ampersand: Qt eats it and
                        // underlines the next letter, so this button read
                        // "Extract figures _tables" on screen. Spelled out
                        // rather than escaped as "&&", which is the same trap
                        // waiting for the next person to edit the line.
                        Button {
                            text: "Extract figures and tables"
                            Layout.fillWidth: true
                            enabled: root.canExtract
                            onClicked: root.app.analyzeLiterature()
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !root.hasPaper
                            text: "Open a paper first."
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: root.analysed
                            text: "What to do with it is in the Literature "
                                  + "intelligence panel, on the right."
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
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
        }

        Rectangle {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 240
            radius: 10; color: Theme.surface; border.color: Theme.border
            // Dragged narrow, the splash headline is wider than this pane, and
            // without this it paints straight over the panel next door.
            clip: true
            ColumnLayout {
                anchors.fill: parent; spacing: 0

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 50; color: Theme.background
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 7
                        // The title gives way to the buttons rather than
                        // pushing them off the end: a paper name that has been
                        // shortened can still be read, and an Analyze button
                        // past the edge of the pane cannot be pressed. It was
                        // capped at 420 with a spacer taking the rest, which
                        // is fine until the pane is narrower than 420.
                        Label {
                            text: root.hasPaper ? root.paperName : "Literature Reader"
                            color: Theme.text
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            Layout.maximumWidth: 420
                        }
                        Button { text: "Analyze"; enabled: root.hasPaper && !root.app.busy; onClicked: root.app.analyzeLiterature() }
                        Button { text: "Close paper"; enabled: root.hasPaper; onClicked: root.app.clearLiterature() }
                    }
                }

                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true

                    // Once a paper has been extracted, the middle of the window
                    // is the figures themselves rather than a summary of how
                    // many there were. Reading numbers back off a published
                    // chart is the thing this workspace exists for, and it used
                    // to have nowhere to happen: the extractor found the
                    // figures, wrote them to disk, and the interface showed a
                    // count.
                    FigureLab {
                        id: lab
                        anchors.fill: parent
                        app: root.app
                        visible: root.analysed
                    }

                    // What the extraction looks like while it runs, and what it
                    // says when it stops. Over the middle of the window rather
                    // than as a line in a side panel: a four-minute OCR pass
                    // with a grey sentence somewhere off to the left is
                    // indistinguishable from a program that has hung, which is
                    // exactly how it was reported.
                    BusyOverlay {
                        id: extractBusy
                        anchors.fill: parent
                        app: root.app
                        // Only OUR job. The controller has one busy flag for
                        // every science operation, so without this the panel
                        // would light up during an unrelated import.
                        active: root.app.workspaceMode === "Literature"
                    }

                    // The summary is built when the analysis lands rather than
                    // bound to it, so it survives being dismissed and does not
                    // reappear every time something else touches the map.
                    Connections {
                        target: root.app
                        function onLiteratureChanged() {
                            if (!root.analysed) return
                            const a = root.app.literatureAnalysis
                            const figures = root.app.literatureFigures.length
                            const datasets = a.datasets ? a.datasets.length : 0
                            const chars = a.text_chars ? a.text_chars : 0
                            var parts = []
                            parts.push(figures + (figures === 1 ? " figure" : " figures"))
                            parts.push(datasets + (datasets === 1 ? " table" : " tables"))
                            if (chars > 0) parts.push(chars.toLocaleString(Qt.locale()) + " characters of text")
                            extractBusy.doneSummary = "Read the paper — " + parts.join(", ")
                            // A paper the extractor had something to complain
                            // about says so here rather than in a log nobody
                            // opens: no figures found is a normal outcome for a
                            // vector-drawn paper and a person needs telling.
                            if (a.warnings && a.warnings.length > 0)
                                extractBusy.doneSummary += "\n" + a.warnings.join(" · ")
                            else if (figures === 0)
                                extractBusy.doneSummary += "\nNo raster figures in this PDF — its charts are "
                                                         + "probably drawn as vectors, which this cannot trace yet."
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent; width: parent.width * 0.7; spacing: 14
                        visible: root.hasPaper && !root.analysed
                        Label { Layout.alignment: Qt.AlignHCenter; text: root.paperName; font.pixelSize: 26; font.bold: true; color: Theme.text; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label {
                            Layout.alignment: Qt.AlignHCenter; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; color: Theme.textSecondary
                            text: root.app.busy && root.app.workspaceMode === "Literature" ? root.app.busyLabel
                                  : root.analysed ? "Extraction complete. Send the data to the workspace to plot it."
                                  : "Press \"Extract figures and tables\" to pull the figures, tables and numeric series out of this paper."
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter; color: Theme.positive; visible: root.analysed
                            text: (root.app.literatureAnalysis.datasets ? root.app.literatureAnalysis.datasets.length + " numeric datasets" : "")
                                  + (root.app.literatureAnalysis.text_chars ? " · " + root.app.literatureAnalysis.text_chars + " characters indexed" : "")
                        }
                        // The same gate as the copy in the side panel - see the
                        // long note there. Two buttons that call one action
                        // must agree about when that action can run, or the
                        // person finds one of them enabled and the other not
                        // for the same paper.
                        Button {
                            id: sendButtonWide
                            Layout.alignment: Qt.AlignHCenter
                            text: "Send extracted data to workspace"
                            enabled: root.canSend
                            onClicked: root.app.importFirstLiteratureDataset()
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.maximumWidth: 420
                            horizontalAlignment: Text.AlignHCenter
                            visible: root.sendBlocked !== ""
                            text: root.sendBlocked
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }

                    // The empty state, sized to whatever the pane has rather
                    // than to the text: this is the one thing on screen when
                    // the pane is narrow, so it is also the first thing to
                    // spill out of it.
                    Column {
                        id: splash
                        anchors.centerIn: parent; spacing: 14; visible: !root.hasPaper
                        width: Math.min(parent.width - 24, 520)
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: "Read Literature"
                            font.pixelSize: splash.width < 320 ? 26 : 38
                            font.bold: true; color: Theme.text
                        }
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: "Open a scientific paper to extract data and reconstruct figures."
                            color: Theme.textSecondary
                        }
                        Button { anchors.horizontalCenter: parent.horizontalCenter; text: "Open literature"; onClicked: root.openRequested() }
                    }

                    DropArea {
                        anchors.fill: parent
                        onDropped: (drop) => { if (drop.hasUrls && drop.urls.length > 0) root.app.openLiterature(drop.urls[0]) }
                    }
                }
            }
        }

        DockPanel {
            title: "Literature intelligence"
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 180
            floatingWidth: 420; floatingHeight: 720
            Rectangle {
            anchors.fill: parent
            radius: 10; color: Theme.background; border.color: Theme.border
            // A ScrollView with `width: parent.width` on its column, which is
            // the trap PanelScroll exists to stop: inside a ScrollView the
            // parent is the CONTENT item, so the column sized itself to its own
            // content and grew wider than the pane. Nothing clipped it and
            // nothing scrolled, so the buttons ran off the right-hand edge with
            // their labels cut in half, and the panel could not be scrolled to
            // reach what was below either. PanelScroll binds to availableWidth
            // and clips.
            PanelScroll {
                anchors.fill: parent; contentSpacing: 10
                    Label { text: "LITERATURE INTELLIGENCE"; color: Theme.textMuted; font.pixelSize: 11; font.bold: true; Layout.margins: 12 }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                    Label { text: root.app.busy && root.app.workspaceMode === "Literature" ? root.app.busyLabel : (root.analysed ? "Extraction complete" : "Select a figure, table or method section to inspect it."); color: root.analysed ? Theme.positive : Theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12 }
                    GvGroupBox {
                        title: "Detected content"; Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                        ColumnLayout { anchors.fill: parent
                            // HOW MANY CAN BE DRAWN, not how many were found.
                            //
                            // This said `datasets.length` - "12 numeric
                            // datasets" - for a thesis whose twelve tables were
                            // a contents page and running heads. Every one was
                            // a table to pdfplumber and none was a table of
                            // numbers, so the count invited a button press that
                            // could not work. The count and the sentence below
                            // it come from AppController, which applies the same
                            // rule the chooser does.
                            Label {
                                text: root.app.literatureSummary.tables !== undefined
                                      ? root.app.literatureSummary.drawable + " of "
                                        + root.app.literatureSummary.tables
                                        + " tables can be drawn"
                                      : "Figures · tables · equations"
                                color: root.app.literatureSummary.drawable > 0
                                       ? Theme.positive : Theme.textSecondary
                            }
                            Label {
                                visible: !!root.app.literatureSummary.guidance
                                text: root.app.literatureSummary.guidance || ""
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeSmall
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Label { text: root.app.literatureAnalysis.text_chars ? root.app.literatureAnalysis.text_chars + " text characters indexed" : "Method and unit links appear here"; color: Theme.textMuted; wrapMode: Text.WordWrap }
                        }
                    }
                    GvGroupBox {
                        title: "Actions"; Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                        ColumnLayout { anchors.fill: parent
                            // Was "Analyze paper". Same call as the Library
                            // panel's button, so it is now the same words as
                            // well: two names for one action is what made a
                            // person ask whether the buttons on both sides of
                            // the window "might do the same thing or something".
                            Button {
                                text: "Extract figures and tables"
                                Layout.fillWidth: true
                                enabled: root.canExtract
                                onClicked: root.app.analyzeLiterature()
                            }
                            Button {
                                id: reconstructButton
                                text: "Reconstruct graph"
                                Layout.fillWidth: true
                                enabled: root.canTrace
                                onClicked: lab.trace()
                                ToolTip.visible: reconstructButton.hovered
                                ToolTip.text: lab.calibrated
                                    ? "Read the calibrated figure into a table of numbers"
                                    : "Calibrate a figure in the middle of the window first"
                            }
                            // Why the button above is grey. It was explained
                            // only by a tooltip, so a disabled button looked
                            // like a broken one - the commonest reading of a
                            // control that will not respond.
                            Label {
                                Layout.fillWidth: true
                                visible: root.traceBlocked !== ""
                                text: root.traceBlocked
                                color: Theme.textMuted
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                            Button {
                                id: compareButton
                                text: "Compare with active dataset"
                                Layout.fillWidth: true
                                // A reconstruction is a dataset like any other,
                                // so "compare" is: import it, then go to the
                                // workspace where two datasets can be plotted
                                // against each other. Nothing special, which is
                                // the point - the numbers off the paper are now
                                // ordinary numbers.
                                enabled: !!(root.app.lastReconstruction && root.app.lastReconstruction.rows > 0 && !root.app.busy)
                                onClicked: {
                                    if (root.app.importReconstruction())
                                        root.app.workspaceMode = "Visualize"
                                }
                                ToolTip.visible: compareButton.hovered
                                ToolTip.text: compareButton.enabled
                                    ? "Import the traced series as a dataset and open it in Visualize, "
                                    + "beside whatever is already loaded"
                                    : "Read a figure first — this imports the numbers that came out of it"
                            }
                            // Same reasoning as above. This one is waiting on a
                            // different thing - a reconstruction that has
                            // actually produced rows - and saying which removes
                            // the guess.
                            Label {
                                Layout.fillWidth: true
                                visible: !compareButton.enabled && !root.app.busy && root.analysed
                                text: "Runs once 'Reconstruct graph' has produced a table."
                                color: Theme.textMuted
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                            // ENABLED ONLY WHEN THERE IS SOMETHING TO SEND.
                            //
                            // It used to be enabled the moment the paper was
                            // analysed, whether or not any extracted table was
                            // drawable. Pressing it on a paper whose numbers
                            // are all in its figures - which is most papers -
                            // did nothing visible: the refusal was written to
                            // the status strip at the bottom of the window,
                            // which is not where anyone is looking when they
                            // have just pressed a button in this panel. It read
                            // as a broken button, and was reported as one.
                            //
                            // `drawable` is already computed, for the line a
                            // few rows above that says "None of the N extracted
                            // tables is a table of numbers" - so the panel HAD
                            // the answer and the button was not asking it.
                            //
                            // Disabled with the reason underneath is the
                            // pattern the two buttons above already use; this
                            // was the one control in the group that failed
                            // silently instead.
                            Button {
                                id: sendButton
                                text: "Send extracted data to workspace"
                                Layout.fillWidth: true
                                enabled: root.canSend
                                onClicked: root.app.importFirstLiteratureDataset()
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: root.sendBlocked !== ""
                                text: root.sendBlocked
                                color: Theme.textMuted
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
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
