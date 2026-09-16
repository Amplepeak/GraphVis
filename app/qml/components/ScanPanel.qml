// Qt 6 delegate scoping. Without this, an id from the enclosing file is not
// legally visible inside a delegate or an inline Component - it resolves only
// because the old unbound context lookup walks out of the component, which is
// slower at every evaluation and silently breaks the moment a model role
// shares a name. Bound resolves ids at compile time; delegates declare what
// they take from the model with `required property`.
pragma ComponentBehavior: Bound
// Smart Suite / Scan Dataset.
//
// Runs GraphVis 17's intelligent_scan over the active dataset through the
// optional Python science service, and turns each recommendation into a
// one-click action: stage that catalogue graph with the axis mapping the
// scanner chose. Ported behaviour, including the thinking time maximum - a
// longer maximum widens column breadth and pair sampling rather than just
// waiting. It is called a maximum rather than a budget because it is a ceiling
// the scan stops at, not an allowance it sets out to spend: a scan that runs
// out of things to try returns early with the best answer it found.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import GraphVis

ColumnLayout {
    id: root
    required property var app
    signal recommendationChosen(string graph, var mappings)

    // THE RESULTS LIST CAN LEAVE THE SIDEBAR.
    //
    // Reported as "need to be bigger to see more graphs here". In a 360 px
    // sidebar this list gets whatever is left after the controls above it,
    // which is three cards at a time out of a scan that routinely returns
    // twenty or more - and the note next door explains why it cannot simply be
    // given more: a layout handed less than its children's minimum does not
    // shrink them, it draws them over the panel below.
    //
    // THE LIST MOVES, IT IS NOT COPIED. Same design as FigureWindow, and for
    // the reason recorded there at length: a second list built from the same
    // model looks identical until something changes, and then one of them is
    // showing a scan that no longer exists. `parent` is a BINDING here, and
    // `anchors.fill: parent` follows it, so the one live ListView fills the
    // slot in the sidebar while it lives there and fills the window while it
    // lives here, with nothing to keep in step by hand.
    property bool listFloating: false
    // The one list, named so a test can hold it and watch where it goes. There
    // is no other way to ask: once it is floating it is a child of a Window's
    // content item and no longer anywhere under this panel, so a search of the
    // panel's children finds nothing and "it was copied" and "it was moved"
    // look the same from outside.
    readonly property alias resultsList: recList

    spacing: Theme.gap

    // Measured, not assumed: with three results in a 360 px sidebar this block
    // needs 194 px, because the summary line wraps to two. The library panel
    // capped it at 180. A QtQuick layout given less than its children's minimum
    // does not shrink them - it lays them out past its own edge - so the
    // overflow painted straight over the search field below, and the
    // recommendation text and "Search graphs..." appeared on top of each other.
    //
    // The cap is fixed next door. This is the guarantee: whatever height this
    // panel is given, it cannot draw outside it. Clipping a little content is a
    // visible, understandable loss; painting over a neighbouring control is
    // corruption that looks like a broken program.
    clip: true

    // GraphVis 17 offers seconds through hours; the scanner treats it as a
    // soft deadline and always returns the best result found so far.
    readonly property var budgets: [
        { label: "Quick · 8 s",     seconds: 8 },
        { label: "Standard · 30 s", seconds: 30 },
        { label: "Deep · 5 min",    seconds: 300 },
        { label: "Thorough · 30 min", seconds: 1800 },
        { label: "Exhaustive · 2 h",  seconds: 7200 }
    ]

    // One click: scan if needed, then draw the best answer.
    //
    // Deliberately NOT a separate top-level tab with its own data picker,
    // canvas and overlay controls. All three already exist here and in
    // Visualize, and a parallel copy of them is two of everything that can
    // disagree - which is the fault this codebase's own comments complain about
    // most often. This is the same scan, the same recommendation list and the
    // same apply path that a click on a card takes; it just skips the reading.
    property bool applyBestWhenReady: false

    function applyBest() {
        var found = root.app.scanRecommendations
        if (!found || found.length === 0) return false
        root.recommendationChosen(found[0].graph, found[0].mappings || ({}))
        return true
    }

    Connections {
        target: root.app
        // scanChanged carries the scanning flag AND the results, so this fires
        // at the start of a scan too; waiting for scanning to go false is what
        // makes it the finish rather than the start.
        function onScanChanged() {
            if (!root.applyBestWhenReady || root.app.scanning) return
            root.applyBestWhenReady = false
            root.applyBest()
        }
    }

    Button {
        id: goButton
        Layout.fillWidth: true
        highlighted: true
        text: root.app.scanning
              ? "Scanning…"
              : (root.app.scanRecommendations.length > 0
                 ? "Draw the best one"
                 : "Go — scan and draw the best")
        enabled: !root.app.busy && root.app.activeDatasetId !== ""
        onClicked: {
            if (root.applyBest()) return
            // Nothing to apply yet, so scan and pick it up when it lands.
            root.applyBestWhenReady = true
            root.app.scanDataset(root.budgets[budgetBox.currentIndex].seconds,
                                 useLiterature.checked)
        }
        ToolTip.visible: goButton.hovered
        ToolTip.text: root.app.activeDatasetId === ""
            ? "Load a dataset first"
            : "Rank the graphs for this data and draw the top one, without choosing anything"
    }

    // Said out loud rather than left as a button that appears to do nothing: a
    // scan CAN legitimately come back with no recommendation - one constant
    // column, or two rows - and silence there is indistinguishable from a
    // broken button.
    Label {
        Layout.fillWidth: true
        visible: !root.app.scanning && root.app.scanRecommendations.length === 0
                 && root.app.scanSummary.examined_pairs !== undefined
                 && !root.app.scanSummary.error
        text: "The last scan found nothing worth recommending — usually too few rows, "
            + "or columns that do not vary."
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        // "Thinking time maximum", not "budget". A budget reads as an
        // allowance that will be spent; this is a ceiling the scanner stops
        // at, and it returns the best answer found so far whenever it stops.
        Label { text: "Thinking time maximum"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
        ComboBox {
            id: budgetBox
            Layout.fillWidth: true
            currentIndex: 1
            textRole: "label"
            model: root.budgets
        }
    }

    // Phase 4. GraphVis 17 separated "Scan Dataset" from "Scan + Literature…";
    // here it is one scan with the paper as optional context, which keeps the
    // two from drifting apart.
    CheckBox {
        id: useLiterature
        text: root.app.literatureContextAvailable
              ? "Use the open paper as context"
              : "Use the open paper as context (analyse a paper first)"
        enabled: root.app.literatureContextAvailable
        checked: root.app.literatureContextAvailable
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        Button {
            Layout.fillWidth: true
            text: root.app.scanning ? "Scanning…"
                 : (useLiterature.checked ? "Scan + Literature" : "Scan Dataset")
            enabled: !root.app.busy && root.app.activeDatasetId !== ""
            onClicked: root.app.scanDataset(root.budgets[budgetBox.currentIndex].seconds,
                                            useLiterature.checked)
        }
        // A scan of the same data, maximum and paper is cached and comes back
        // instantly. Rescan forces the work to be redone.
        Button {
            text: "Rescan"
            ToolTip.visible: hovered
            ToolTip.text: "Ignore the cached result and scan again"
            enabled: !root.app.busy && root.app.activeDatasetId !== ""
            onClicked: root.app.scanDataset(root.budgets[budgetBox.currentIndex].seconds,
                                            useLiterature.checked, true)
        }
        Button {
            text: "Clear"
            ToolTip.visible: hovered
            ToolTip.text: "Clear the results shown here"
            enabled: root.app.scanRecommendations.length > 0
            onClicked: root.app.clearScan()
        }
        Button {
            text: "⌫"
            implicitWidth: 34
            ToolTip.visible: hovered
            ToolTip.text: "Delete every cached scan for this project"
            onClicked: root.app.clearScanCache()
        }
        Button {
            text: root.listFloating ? "⇲" : "⇱"
            implicitWidth: 34
            checkable: true
            checked: root.listFloating
            ToolTip.visible: hovered
            ToolTip.text: root.listFloating
                          ? "Put the results back in the sidebar"
                          : "Open the results in a window you can move and resize"
            onClicked: root.listFloating = !root.listFloating
        }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        // Two lines at most. This is the line that wraps and pushes the whole
        // block past its allowance in a narrow sidebar; an unbounded error
        // message could otherwise take the list's height entirely.
        maximumLineCount: 2
        elide: Text.ElideRight
        font.pixelSize: Theme.fontSizeSmall
        color: root.app.scanSummary.error ? Theme.warning : Theme.textMuted
        text: {
            if (root.app.scanning) return "Profiling columns, scoring pairs and ranking graph-specific mappings…"
            if (root.app.scanSummary.error) return String(root.app.scanSummary.error)
            if (root.app.scanRecommendations.length > 0)
                return root.app.scanRecommendations.length + " recommendations · "
                     + (root.app.scanSummary.cached ? "cached · "
                        : Number(root.app.scanSummary.analysis_seconds).toFixed(1) + " s · ")
                     + root.app.scanSummary.examined_pairs + " pairs examined"
                     + (root.app.scanSummary.budget_exhausted ? " · budget reached" : "")
                     + (root.app.scanSummary.literature_used > 0 ? " · paper used as context" : "")
            if (root.app.activeDatasetId === "") return "Import or select a dataset to scan."
            if (!root.app.scienceServiceAvailable) return "Scanning uses the optional GraphVis Science add-on."
            return "Scan profiles every numeric column, scores relationships and ranks graphs with a mapping for each."
        }
    }

    // THE SLOT THE LIST SITS IN while it is docked. It holds the place in this
    // layout whether or not the list is in it, which is what keeps the controls
    // above from jumping up the panel the moment the window opens.
    Item {
        id: listSlot
        Layout.fillWidth: true
        Layout.fillHeight: true
        // Low enough that the whole block fits inside the share the library
        // panel gives it on a short window. A floor of 120 made the block
        // taller than its own allowance, so the buttons above it were pushed
        // out of the panel instead of the list shrinking.
        Layout.minimumHeight: 70

        Label {
            anchors.centerIn: parent
            width: parent.width - 16
            visible: root.listFloating
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            text: "The results are in their own window."
        }

        // BOTH OF THESE LIVE IN THE SLOT, not beside it in the ColumnLayout.
        //
        // A layout manages its own children, and writing the list as a child of
        // one - even a list that reparents itself away on its very next line -
        // gives the layout an item it is entitled to position and size.
        // The linter reports exactly that - "Detected anchors on an item that
        // is managed by a layout. This is undefined behavior." - and it came
        // out as three warnings for one mistake. `listSlot` is a plain Item, so
        // what is written inside it is nobody else's business.
        //
        // (The tool's name is left out of this comment on purpose: a comment
        // that opens with it is read as a lint DIRECTIVE, and every word after
        // it came back as "unknown category".)
        //
        // The window the list moves into. Built with the panel and shown on
        // demand, so the list has somewhere to go the first time the button is
        // pressed, rather than a component being constructed under the hands of
        // whoever pressed it.
        Window {
            id: popOut
            title: "Scan results — GraphVis"
            width: 760
            height: 620
            minimumWidth: 360
            minimumHeight: 240
            color: Theme.background
            visible: root.listFloating
            // Closed from the window's own titlebar rather than from the button.
            // Without this the property stays true, the binding keeps the list
            // parented to a hidden window, and the sidebar shows an empty slot with
            // the results nowhere at all.
            onClosing: root.listFloating = false
        }

        ListView {
            id: recList
            // NOT a child of the layout, in either state. Docked, its parent is the
            // slot above; floating, it is the window's content item. Neither is the
            // ColumnLayout, which is why this carries no Layout attached properties
            // - they would be read by nothing and would look like the thing sizing
            // it. `anchors.fill: parent` is what sizes it, in both homes, because
            // the anchor is a binding on a parent that changes.
            parent: root.listFloating ? popOut.contentItem : listSlot
            anchors.fill: parent
            clip: true
            spacing: 4
            model: root.app.scanRecommendations
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: rec
                required property var modelData
                width: ListView.view.width
                height: body.implicitHeight + 14
                radius: Theme.radius
                color: hover.hovered ? Theme.surfaceAlt : "transparent"
                border.color: hover.hovered ? Theme.borderStrong : Theme.border

                ColumnLayout {
                    id: body
                    anchors.fill: parent
                    anchors.margins: 7
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: rec.modelData.graph
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Theme.fontSizeBody
                            elide: Text.ElideRight
                        }
                        // Confidence, as the scanner scored it.
                        Rectangle {
                            implicitWidth: scoreLabel.implicitWidth + 10
                            implicitHeight: scoreLabel.implicitHeight + 4
                            radius: 3
                            color: rec.modelData.source === "literature" ? Theme.accent : Theme.surfaceAlt
                            border.color: Theme.border
                            Label {
                                id: scoreLabel
                                anchors.centerIn: parent
                                text: Math.round(Number(rec.modelData.score) * 100) + "%"
                                font.pixelSize: Theme.fontSizeSmall
                                color: rec.modelData.source === "literature" ? Theme.onAccent : Theme.textSecondary
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: {
                            var m = rec.modelData.mappings || {}
                            var parts = []
                            if (m.x) parts.push("X " + m.x)
                            if (m.y) parts.push("Y " + m.y)
                            if (m.z) parts.push("Z " + m.z)
                            return parts.join("   ")
                        }
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: rec.modelData.reason || ""
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }

                HoverHandler { id: hover }
                TapHandler {
                    onTapped: root.recommendationChosen(rec.modelData.graph, rec.modelData.mappings || ({}))
                }
            }
        }
    }
}
