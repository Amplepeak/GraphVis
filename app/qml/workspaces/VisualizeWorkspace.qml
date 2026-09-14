import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// PlotCanvas is a C++ type registered into this module by QML_ELEMENT. The
// other types here come from the relative directory import; a C++ type needs
// the module imported by name.
import GraphVis
import "../components"

Item {
    id:root
    required property var app
    // The plot itself, so a shell can hand it to the Publish workspace. The
    // canvas lives here and export lives there, and without this the two never
    // met.
    // The figure the controls act on. In the notebook that is whichever cell is
    // current; with one canvas it is that canvas. Everything above this - the
    // sidebar, the menu bar, the Publish workspace - asks for `canvas` and does
    // not need to know which layout is showing.
    readonly property var canvas: root.app.notebookLayout
                                  ? (notebook.item ? notebook.item.currentCanvas : null)
                                  : plot

    // The figure an action applies to, and an explanation when there is none.
    //
    // `canvas` is null in the notebook layout whenever the sheet has not
    // finished loading or no cell is current, and every handler that used it
    // began `if (!c) return`. So Apply, a scan recommendation and a mapping
    // change all did NOTHING AND SAID NOTHING in that state - the button
    // depressed, the list highlighted the choice, and the figure did not
    // change. From the outside that is indistinguishable from a broken button,
    // which is why it kept being reported as "Apply doesn't always work".
    //
    // Returning null is still the right behaviour; the silence was not.
    function targetCanvas(what) {
        var c = root.canvas
        if (c) return c
        root.app.notify(root.app.notebookLayout
                        ? ("No cell is selected, so there is nowhere to put "
                           + what + ". Click a cell in the notebook first.")
                        : ("The figure is not ready yet, so " + what
                           + " could not be applied. Try again in a moment."))
        return null
    }
    // Raised by the Import button in the dataset bar above the graph chooser.
    //
    // This is the whole reason that button did nothing. ActiveDatasetBar
    // emitted it, ControlSidebar forwarded it - and here the chain stopped:
    // nothing in this file listened, so the signal went nowhere and the only
    // way to open a file was the Data tab. A signal with no receiver is
    // indistinguishable, from the outside, from a button that is broken.
    signal importRequested()
    // The Ribbon's Find-a-graph button and the Command bar's hint both open the
    // window's palette, which the shell owns.
    signal commandRequested()

    // The window shape, read as a RECORD rather than decided by a chain of
    // `layout === n` tests. See app/src/UiLayouts.h: the six shapes this file
    // used to switch on all ran through the same three conditionals, so the
    // catalogue stayed on the left and Publish stayed on top whichever one was
    // picked. Everything structural below asks `spec` a question.
    readonly property var spec: root.app.uiLayoutSpec
    readonly property int layout: root.app.uiLayout

    // The enumerations, spelled out. QML has no access to the C++ enums and a
    // bare 2 in a binding is unreadable in either language.
    readonly property int sidebarNone: 0
    readonly property int sidebarLeft: 1
    readonly property int sidebarRight: 2
    readonly property int modeTabs: 0
    readonly property int modeInspector: 1
    readonly property int modeRail: 2
    readonly property int modeStack: 3
    readonly property int bandNone: 0
    readonly property int bandRibbon: 1
    readonly property int bandShelves: 2
    readonly property int bandWorkspaceTabs: 3
    readonly property int bandAddToolbar: 4
    readonly property int stripNone: 0
    readonly property int stripDataTable: 1
    readonly property int stripLog: 2
    readonly property int stripPageBar: 3
    readonly property int stripSections: 4
    readonly property int canvasSingle: 0
    readonly property int canvasNotebook: 1
    readonly property int canvasQuadrants: 2
    readonly property int canvasCentred: 3
    readonly property int canvasZen: 4

    readonly property bool sidebarDocked: root.spec.sidebar !== root.sidebarNone
                                          && !root.spec.floatingTools
    // Mirroring is still how the panel gets to the right-hand edge: reordering
    // a SplitView's children at run time is not something QML does, and a
    // second copy of the sidebar would be a second thing to keep in step.
    LayoutMirroring.enabled: root.spec.sidebar === root.sidebarRight
    LayoutMirroring.childrenInherit: true

    // The panel the rail or the page bar last asked for.
    property int chosenPanel: 0
    // Whether the tool panel is showing at all.
    //
    // It used to be a local flag that only a rail layout could clear, because
    // on the others there would have been no way to get the panel back. There
    // is one now - a slim strip on the panel's edge, below - so the fold is
    // offered on every layout, and it lives on the controller so it is still
    // folded when the program is opened again.
    readonly property bool sidebarOpen: !root.app.sidebarCollapsed
    function showSidebar() { root.app.sidebarCollapsed = false }
    // The second panel's title and, on the layouts that summon it, whether
    // anything has asked for it yet.
    readonly property string secondaryTitle: root.spec.secondaryPanel === "data"
                                             ? "Columns" : "Axes"
    property bool secondarySummoned: false
    // How tall the bottom strip opens. A table wants room; a log does not.
    readonly property int bottomHeight: root.spec.bottomStrip === root.stripDataTable ? 230
                                      : (root.spec.bottomStrip === root.stripLog ? 150 : 40)

    // The summoning layouts open their inspector when the figure is touched.
    // Figma's asymmetry: the browse side stays shut and the edit side calls
    // itself, so a click on the figure is enough to start editing it.
    Connections {
        target: root.canvas
        enabled: root.spec.inspectorSummons && root.canvas !== null
        function onSourceChanged() { root.secondarySummoned = true }
    }

    // Opening a .gvfig restores the canvas. The controller imports the figure's
    // datasets first and only then emits this, so the columns the state names
    // exist by the time they are selected.
    Connections {
        target: root.app
        function onFigureLoaded(canvasState) {
            if (root.canvas) root.canvas.applyFigureState(canvasState)
            root.app.rendererMode = "Qt 2-D"
        }
    }
    // The window's own vertical split, so the strip along the bottom can be
    // dragged rather than being whatever height this file decided.
    //
    // This was a ColumnLayout with the strip pinned to bottomHeight. "I can't
    // drag the bars to make the graph window bigger" was literally true of it:
    // on a Data-table layout, 230 px of the window belonged to the table for
    // good, and the only way to give the figure more was to change layout. The
    // panes carry their own travel limits, so the strip can be dragged down to
    // a sliver or up to most of the window, and the figure keeps 160 px
    // whatever happens.
    SplitView {
        id: shellSplit
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: GvSplitHandle {}

    RowLayout {
        SplitView.fillHeight: true
        SplitView.minimumHeight: 160
        spacing: 0

        // The icon rail, OUTSIDE the split.
        //
        // A rail is not a pane - it has one width and no reason to be dragged -
        // so putting it in the SplitView would give it a handle that does
        // nothing useful and let it be dragged to nothing.
        IconRail {
            id: leftRail
            Layout.fillHeight: true
            visible: root.spec.rail
            currentIndex: root.chosenPanel
            // Clicking the current symbol shuts the panel on EVERY rail layout
            // now, not only the one whose spec said the rail persists: the rail
            // itself is the way back, so there was never a layout where the
            // fold could strand somebody.
            allowCollapse: true
            collapsed: !root.sidebarOpen
            onPanelChosen: (i) => { root.chosenPanel = i; root.showSidebar() }
            onCollapseToggled: root.app.sidebarCollapsed = !root.app.sidebarCollapsed
            onCommandRequested: root.commandRequested()
        }

        // The way back, when the tool panel is folded.
        //
        // A layout with a rail already has one - the rail is still there with
        // the panel shut. Every other layout would have nothing at all on
        // screen, so the fold could never be offered on them; eighteen pixels
        // of edge with the panel's name down it is what makes it offerable.
        Rectangle {
            id: sidebarTab
            Layout.fillHeight: true
            implicitWidth: 18
            visible: root.sidebarDocked && !root.sidebarOpen && !root.spec.rail
            color: tabHover.hovered ? Theme.surface : Theme.surfaceAlt
            border.color: Theme.border
            Label {
                anchors.centerIn: parent
                // Rotated so the word fits an eighteen pixel strip. Reading
                // upwards is the convention every editor's folded panel uses.
                rotation: -90
                text: "▸  CONTROLS"
                color: tabHover.hovered ? Theme.text : Theme.textMuted
                font.pixelSize: 10
                font.bold: true
                font.letterSpacing: 1
            }
            HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.showSidebar() }
            ToolTip.visible: tabHover.hovered
            ToolTip.text: "Show the controls"
        }

        SplitView {
            id: mainSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            // The divider people actually reach for. It was the default single
            // pixel until now; ControlSidebar's inner splits had a visible
            // handle and this one, between the controls and the figure, did
            // not.
            handle: GvSplitHandle {}

    // The controls dock can be torn out into its own window, restoring the
    // flexibility GraphVis 17 had. The sidebar keeps its state either way.
    DockPanel {
        id: sidebarDock
        title: "GraphVis Controls"
        visible: (root.sidebarDocked && root.sidebarOpen) || sidebarDock.floating
        // Free docks floats the controls over the figure; Zen and Command bar
        // have none on screen at all until Ctrl+K asks for them.
        floating: root.spec.floatingTools
        // Each layout says how wide its panel wants to be. A rail layout is
        // narrower because the rail beside it is carrying the navigation.
        SplitView.preferredWidth: root.spec.sidebarWidth
        // Explicit travel limits on both panes. Without a maximum here and a
        // minimum on the canvas, the handle could not be dragged to the right
        // and panel content was clipped instead of the panel growing.
        //
        // 280 was the floor while a squeezed panel overflowed instead of
        // shrinking; the panels shrink now, and a floor that does not fit is
        // worse than a narrow panel - on a 900 px window the three-pane
        // layouts could not fit their own minimums and pushed the last panel
        // off the right-hand edge of the window entirely.
        SplitView.minimumWidth:240
        SplitView.maximumWidth:Math.max(240, root.width - 320)

        // Fold it away. Beside the tear-out button, which is the other thing
        // this panel can do to get out of the figure's way - the two questions
        // are "somewhere else" and "not now", and they belong together.
        headerActions: [
            ToolButton {
                id: foldSidebar
                implicitWidth: 26
                implicitHeight: 22
                visible: !sidebarDock.floating
                ToolTip.visible: foldSidebar.hovered
                ToolTip.text: "Fold the controls away"
                onClicked: root.app.sidebarCollapsed = true
                contentItem: Label {
                    text: "◂"
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        ]

        ControlSidebar {
        id:sidebar
        anchors.fill: parent
        app:root.app
        inspectorMode: root.spec.sidebarMode === root.modeInspector
        // The tab bar disappears when something else is doing the choosing.
        // Two controls that both claim to say which panel is open is how they
        // end up disagreeing.
        tabsVisible: root.spec.sidebarMode === root.modeTabs
                     || root.spec.sidebarMode === root.modeStack
        currentTab: root.chosenPanel
        onCurrentTabChanged: root.chosenPanel = sidebar.currentTab
        canvas: plot
        onImportRequested: root.importRequested()
        onApplyMapping:(x,y,z,c,size,alpha,invert,voxelBins,smartRender,smartProfile)=>{
            app.applyMapping(x,y,z,c,size,alpha,invert,voxelBins,smartRender,smartProfile)
            // vtkLoader.item is a QObject as far as any static check can tell:
            // GraphVis.VTK is a separate, lazily-loaded module. The guard on
            // the line is the check, so the lint is told so rather than left
            // reporting a member it has no way to know about.
            // qmllint disable missing-property
            if(app.rendererMode==="VTK / PBR" && vtkLoader.item && vtkLoader.item.reload)
                vtkLoader.item.reload()
            // qmllint enable missing-property
            // ALL four mapped roles, not just two.
            //
            // This used to be `plot.yColumns = y ? [y] : []`, which threw Z and
            // Colour away. A multi-column engine reads its inputs as series -
            // a heatmap's x, y and value, a 3-D scatter's x, y and z - so every
            // one of them got a single series, drew a frame with nothing in it,
            // and only Line Chart worked. The canvas composes them according to
            // what the engine actually needs; see PlotCanvas::rebuild.
            var target = root.targetCanvas("the column mapping")
            if (!target) return
            target.xColumn = x
            target.yColumns = y ? [y] : []
            target.zColumn = z ? z : ""
            target.colorColumn = c ? c : ""
        }
        // Applying a catalogue entry switches to the 2-D renderer and draws it.
        onGraphSelected:(entry)=>{
            app.rendererMode = "Qt 2-D"
            // root.canvas, not `plot`: in the notebook the figure being edited
            // is whichever cell is current, and writing to the hidden single
            // canvas meant choosing a graph did nothing visible at all.
            var c = root.targetCanvas(entry.engine)
            if (!c) return
            c.engine = entry.engine
            c.variant = entry.scale ? entry.scale : ""
            c.title = entry.engine
            // Recorded here rather than in the library, so the list under File
            // is what was actually drawn rather than what was clicked and then
            // abandoned.
            // The category too, or the recents list cannot find its way back to a
            // catalogue entry: an engine name is not unique across categories.
            app.noteVisualisation(entry.engine, entry.scale ? entry.scale : "",
                                  entry.category ? entry.category : "")
        }
        // A Smart Suite recommendation carries its own mapping, so it selects
        // the graph and the axes together - the point of the scan.
        onScanRecommendation:(graph, mappings)=>{
            app.rendererMode = "Qt 2-D"
            var t = root.targetCanvas(graph)
            if (!t) return
            t.engine = graph
            t.variant = ""
            t.title = graph
            // Only the roles the recommendation actually names.
            //
            // This used to CLEAR the ones it did not: a recommendation carrying
            // an x and a z but no y set yColumns to an empty list, and a figure
            // with no y column draws nothing at all - so clicking a
            // recommendation read as a control that does nothing. Leaving a
            // role alone keeps whatever was already mapped, which is at worst
            // the previous figure and at best exactly right.
            var m = mappings || ({})
            var applied = []
            if (m.x) { t.xColumn = m.x; applied.push("X " + m.x) }
            if (m.y) { t.yColumns = [m.y]; applied.push("Y " + m.y) }
            if (m.z) { t.zColumn = m.z; applied.push("Z " + m.z) }
            if (m.c) { t.colorColumn = m.c; applied.push("colour " + m.c) }
            app.noteVisualisation(graph, "")
            // Says what it did. A recommendation that could only fill some of
            // the axes is a useful thing to be told, not a thing to hide.
            app.notify(applied.length > 0
                       ? graph + " — " + applied.join(", ")
                       : graph + " — the recommendation named no columns, so the "
                               + "current mapping was kept")
        }
        }
    }
    // The canvas is a dock too, so a second monitor can hold the plot while the
    // controls stay on the first. The Rust/WGPU and VTK viewports embed a
    // native window in this item, and reparenting that across QWindows is not
    // something to do silently - the tear-out is offered for the Qt 2-D canvas.
    DockPanel {
        id: canvasDock
        // What is on screen, said in the panel's own title rather than in a bar
        // across the bottom of the figure. The bar was in the way of the
        // buttons beneath it and looked bolted on; a panel saying what it
        // contains is just a panel title.
        title: root.app.rendererMode !== "Qt 2-D"
               ? "Canvas"
               : (plot.pointCount > 0
                  ? "Canvas · " + plot.message
                    + (plot.showingFullRender ? " · full resolution"
                                              : (plot.previewIsExact ? "" : " · preview"))
                  : "Canvas")
        floatable: root.app.rendererMode === "Qt 2-D"
        headerVisible: root.spec.canvas !== root.canvasZen
        floatingWidth: 1040
        floatingHeight: 760
        SplitView.fillWidth: true
        SplitView.minimumWidth: 300

        // The figure's controls, in the panel header.
        //
        // These used to float along the bottom edge of the plot, on top of the
        // figure, growing sideways over one another as more of them appeared -
        // and the row was wide enough to sit over the buttons underneath it.
        // A strip that covers the controls you would use to act on it is worse
        // than no strip. Everything here acts on the whole canvas, which is
        // exactly what a panel header is for.
        headerActions: [
            // What is under the pointer, and only while there is something
            // under it. First because it changes constantly and the eye should
            // not have to hunt for it.
            StatusPill {
                visible: plot.cursorOnPlot
                text: plot.cursorText
                textColor: Theme.text
            },
            // Zoom without a wheel.
            //
            // PlotCanvas::zoomBy was written, made Q_INVOKABLE for exactly this,
            // and never called by anything: the wheel handler uses zoomAt, which
            // needs a pointer position, so there was no way to zoom from a
            // trackpad-less machine, a touchscreen without pinch, or a keyboard.
            // zoomBy zooms about the middle of the figure, which is what a
            // button means.
            Button {
                id: zoomOut
                text: "−"
                implicitWidth: 28
                visible: plot.viewInteractive && plot.pointCount > 0
                ToolTip.visible: zoomOut.hovered
                ToolTip.text: "Zoom out about the last point the pointer was over, or the middle of the figure if it has not been on it yet"
                onClicked: plot.zoomBy(1.0 / 1.25)
            },
            Button {
                id: zoomIn
                text: "+"
                implicitWidth: 28
                visible: plot.viewInteractive && plot.pointCount > 0
                ToolTip.visible: zoomIn.hovered
                ToolTip.text: "Zoom in about the last point the pointer was over, or the middle of the figure if it has not been on it yet"
                onClicked: plot.zoomBy(1.25)
            },
            // The full-render policy is NOT here.
            //
            // It was, on the reasoning that it decides something about this
            // figure and belongs near it. That reasoning ignored what it cost:
            // the combo is 250 px of a row that has to hold the cursor
            // read-out, the zoom buttons, two unit selectors, the colour map
            // and the export button - and on an ordinary window it pushed the
            // export button off the end entirely, so the one control in that
            // row anybody needs every session was the one that disappeared.
            //
            // It is a preference, it is set once, and the View menu already
            // has it. A thing you choose once does not earn permanent space
            // beside a thing you use constantly.
            UnitSelector { axis: "X"; canvas: plot },
            UnitSelector { axis: "Y"; canvas: plot },
            // The colour-map picker is NOT here any more.
            //
            // It is a choice made once for a figure, and it was holding
            // permanent width in the row that also carries the cursor read-out,
            // the zoom buttons, two unit selectors and the export button - the
            // comment above records that an over-wide control in this row once
            // pushed the export button off the end entirely. It lives in the
            // sidebar, beside the background and the grid, where the rest of
            // the figure's appearance is set.
            Button {
                id: cameraReset
                text: "Reset camera"
                visible: plot.view3D
                ToolTip.visible: cameraReset.hovered
                ToolTip.text: "Back to the starting angle. Drag the figure to turn it, "
                            + "wheel or pinch to zoom, double-click to reset."
                onClicked: plot.resetCamera()
            },
            Button {
                id: viewReset
                text: "Reset view"
                visible: plot.viewZoomed
                ToolTip.visible: viewReset.hovered
                ToolTip.text: "Fit the axes back to the data. Double-click or double-tap the figure does the same."
                onClicked: plot.resetView()
            },
            Button {
                id: annotateButton
                text: plot.annotating ? "Stop annotating"
                                      : (plot.annotationCount > 0
                                         ? "Notes (" + plot.annotationCount + ")"
                                         : "Annotate")
                checkable: true
                checked: plot.annotating
                visible: plot.viewInteractive && plot.pointCount > 0
                ToolTip.visible: annotateButton.hovered
                ToolTip.text: "Add a note to the figure. Notes are anchored to a data "
                            + "point, so they stay put through a zoom, and they export "
                            + "into the PDF as real selectable text."
                onClicked: plot.annotating = annotateButton.checked
            },
            Button {
                id: clearNotes
                text: "Clear notes"
                visible: plot.annotationCount > 0
                onClicked: plot.clearAnnotations()
            },
            // Last, hard against the pop-out button, which is where the person
            // asked for it.
            // One button, and the choices behind it.
            //
            // It said "Export PDF" and wrote a 6 x 4 inch, 600 dpi PDF with no
            // way to say otherwise - while PlotCanvas had three export
            // functions with eight parameters between them and QML called one
            // of them with none. An SVG, a 300 dpi plate, a slide-sized PNG and
            // a figure at a journal's exact column width were all already
            // possible and none of them was reachable.
            Button {
                id: exportButton
                text: "Export"
                enabled: plot.pointCount > 0
                ToolTip.visible: exportButton.hovered
                ToolTip.text: "Format, size and resolution - vector or raster, "
                            + "drawn by the same backend as the screen"
                onClicked: exportDialog.open()
            }
        ]

        Rectangle {
            anchors.fill: parent
            color:Theme.background; border.color:Theme.border; radius:Theme.radius

            ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // The Ribbon layout's band. Only in that layout: in the other five
            // the same controls are already somewhere the person can see them,
            // and a second copy is a second thing to keep in step.
            // The band above the figure. Directly above it rather than across
            // the whole window, because every one of these is ABOUT the figure:
            // a ribbon of commands for it, the columns on its axes, the named
            // arrangement it is drawn in.
            RibbonBar {
                Layout.fillWidth: true
                visible: root.spec.topBand === root.bandRibbon
                app: root.app
                canvas: root.canvas
                onGraphSearchRequested: root.commandRequested()
            }

            // Tableau's shelves: the mapping as permanent chrome, so "what is
            // this figure plotting" is answered by looking rather than by
            // opening a tab.
            ShelfBar {
                Layout.fillWidth: true
                visible: root.spec.topBand === root.bandShelves
                app: root.app
                canvas: root.canvas
            }

            // Blender's workspace tabs: named arrangements, not documents.
            Rectangle {
                Layout.fillWidth: true
                visible: root.spec.topBand === root.bandWorkspaceTabs
                implicitHeight: 32
                color: Theme.surfaceAlt
                border.color: Theme.border
                // Clipped: a row of buttons that does not fit is laid out past
                // the end of the band, where it paints over the panel beside
                // the figure. Six tabs fit in any window this program is usable
                // in, so clipping is the backstop rather than the behaviour.
                clip: true
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    spacing: 2
                    Repeater {
                        model: [
                            { name: "Explore", panel: 2 },
                            { name: "Plot",    panel: 0 },
                            { name: "Fit",     panel: 4 },
                            { name: "Model",   panel: 5 },
                            { name: "Write",   panel: 6 },
                            { name: "Publish", panel: 7 }
                        ]
                        delegate: ToolButton {
                            required property var modelData
                            text: modelData.name
                            font.pixelSize: 11
                            checkable: true
                            checked: root.chosenPanel === modelData.panel
                            onClicked: { root.chosenPanel = modelData.panel
                                         root.showSidebar() }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // Veusz's add-a-thing toolbar: the verbs, where the verbs are the
            // main thing you do.
            Rectangle {
                Layout.fillWidth: true
                visible: root.spec.topBand === root.bandAddToolbar
                implicitHeight: 32
                color: Theme.surfaceAlt
                border.color: Theme.border
                // Five verbs plus a label is more than a narrow window holds, so
                // the band flicks sideways instead of laying its last buttons
                // out past the end of itself and over the panel beside the
                // figure. `addBand`, not `parent`: a Flickable reparents its
                // declared children onto its content item, whose width is the
                // contentWidth being computed here.
                Flickable {
                    id: addBand
                    anchors.fill: parent
                    contentWidth: addRow.width + 8
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick
                RowLayout {
                    id: addRow
                    x: 8
                    width: Math.max(addBand.width - 8, implicitWidth)
                    height: addBand.height
                    spacing: 4
                    Label {
                        text: "Add"
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.bold: true
                    }
                    Repeater {
                        model: [
                            { name: "Graph",      panel: 0 },
                            { name: "Axes",       panel: 3 },
                            { name: "Fit",        panel: 4 },
                            { name: "Note",       panel: -1 },
                            { name: "Dataset",    panel: 2 }
                        ]
                        delegate: Button {
                            required property var modelData
                            text: modelData.name
                            flat: true
                            font.pixelSize: 11
                            onClicked: {
                                if (modelData.panel < 0) {
                                    if (root.canvas) root.canvas.annotating = true
                                    return
                                }
                                root.chosenPanel = modelData.panel
                                root.showSidebar()
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                }
            }

            // Command bar and Studio hide the sidebar, so something has to say
            // how to get it back. One line, once, rather than an empty window
            // and a shortcut nobody was told about.
            Rectangle {
                Layout.fillWidth: true
                visible: root.spec.sidebar === root.sidebarNone
                implicitHeight: 30
                color: Theme.surfaceAlt
                border.color: Theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    spacing: 8
                    Label {
                        text: "⌕"
                        color: Theme.accent
                        font.pixelSize: 15
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Press Ctrl+K to find a graph, a column, a layout or an action"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                    }
                    Button {
                        text: "Search"
                        flat: true
                        onClicked: root.commandRequested()
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.app.rendererMode==="Qt 2-D" ? 2 : (root.app.rendererMode==="VTK / PBR" ? 1 : 0)
                Item {
                    WindowContainer { anchors.fill:parent; window:root.app.viewportWindow }
                    StatusPill { anchors{right:parent.right;bottom:parent.bottom;margins:12} text:"Rust / WGPU · persistent direct surface" }
                }
                Loader {
                    id:vtkLoader
                    active: root.app.rendererMode === "VTK / PBR"
                    source: active ? "qrc:/qt/qml/GraphVis/lazy/VtkViewport.qml" : ""
                    onLoaded: {
                        item.controller = root.app
                        item.mappingSource = sidebar
                    }
                    onStatusChanged: {
                        if(status === Loader.Error)
                            root.app.notify("VTK/PBR viewport could not load. See the GraphVis startup log for QML/plugin details.")
                    }
                }
                // A column, so the notice below has somewhere to be that is not
                // on top of the figure. What genuinely belongs inside the
                // figure - the annotation editor, the ready notice - still
                // anchors to the plot item.
                // The Qt 2-D slot holds both layouts. One canvas or a notebook
                // of them - hidden rather than destroyed, because destroying
                // the canvas takes its figure, its zoom and its notes with it,
                // and switching layout is not meant to throw work away.
                Item {
                    Loader {
                        id: notebook
                        anchors.fill: parent
                        visible: root.app.notebookLayout
                        active: root.app.notebookLayout
                        sourceComponent: notebookComponent
                    }
                    Component {
                        id: notebookComponent
                        NotebookCanvas { app: root.app }
                    }

                ColumnLayout {
                    anchors.fill: parent
                    visible: !root.app.notebookLayout
                    spacing: 4
                // The optional colour-vision strip, above the figure it acts on.
                //
                // Off by default and not a default part of the toolbar: it used
                // to be a permanent sidebar panel, which spent space every
                // session on a setting most people never open. Turned on from
                // View > Colour vision, and remembered.
                ColourVisionBar {
                    app: root.app
                    canvas: plot
                    visible: root.app.colourVisionToolbarVisible
                    Layout.fillWidth: true
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The figure's own box.
                    //
                    // Filling, except in the Centred layout, where it is held to
                    // a column in the middle at roughly the proportions it will
                    // have on the page - so what you are looking at while you
                    // work is the shape of the thing you are going to publish,
                    // not a figure stretched to whatever the window happens to
                    // be.
                    Item {
                        id: figureBox
                        anchors.centerIn: parent
                        readonly property bool centred: root.spec.canvas === root.canvasCentred
                        width: figureBox.centred
                               ? Math.min(parent.width - 48, (parent.height - 48) * 1.4)
                               : parent.width
                        height: figureBox.centred ? parent.height - 48 : parent.height
                    PlotCanvas {
                        id: plot
                        anchors.fill: parent
                        anchors.margins: 6
                        arrowPath: root.app.activeArrowPath
                        // The figure's own background, which need not be the
                        // interface's. A plot going into a paper is white
                        // whatever the person likes to work in.
                        //   0 follow the theme   1 dark   2 light   3 white
                        //   4 a colour the person picked
                        //
                        // Case 4 takes its ink and its grid from the controller
                        // rather than from a case written here, because both
                        // are DERIVED from the background - see
                        // AppController::figureForeground - so that a chosen
                        // ground cannot end up carrying axis text nobody can
                        // read.
                        backgroundColor: {
                            switch (root.app.figureTheme) {
                            case 1: return "#111820"
                            case 2: return "#f4f6f9"
                            case 3: return "#ffffff"
                            case 4: return root.app.figureBackground
                            default: return Theme.background
                            }
                        }
                        foregroundColor: {
                            switch (root.app.figureTheme) {
                            case 1: return "#dbe6f0"
                            case 2: case 3: return "#14181d"
                            case 4: return root.app.figureForeground
                            default: return Theme.text
                            }
                        }
                        gridColor: {
                            switch (root.app.figureTheme) {
                            case 1: return "#26384f"
                            case 2: return "#d3d9e2"
                            case 3: return "#e2e6ec"
                            case 4: return root.app.figureGridColour
                            default: return Theme.border
                            }
                        }
                        gridVisible: root.app.plotGridVisible
                        gridDensity: root.app.plotGridDensity
                        gridDensityY: root.app.plotGridDensityY
                        pieLabels: root.app.plotPieLabels
                        polarConvention: root.app.plotPolarConvention
                        scaleLabelsVisible: root.app.plotScaleLabels
                        fieldInterpolation: root.app.plotFieldInterpolation
                        // The scattered estimator and its policies. -1 keeps
                        // the grid-filling path above.
                        fieldEstimator: root.app.plotFieldEstimator
                        fieldExtrapolation: root.app.plotFieldExtrapolation
                        fieldValuePolicy: root.app.plotFieldValuePolicy
                        fieldResponseSpace: root.app.plotFieldResponseSpace
                        fieldNeighbours: root.app.plotFieldNeighbours
                        fieldIdwPower: root.app.plotFieldIdwPower
                        fieldSmoothing: root.app.plotFieldSmoothing
                        fieldFootprint: root.app.plotFieldFootprint
                        fieldBridging: root.app.plotFieldBridging
                        fieldBridgeMaxCells: root.app.plotFieldBridgeMaxCells
                        fieldInvalidDisplay: root.app.plotFieldInvalidDisplay
                        fieldKrigingVariogram: root.app.plotFieldKrigingVariogram
                        fieldLoessFraction: root.app.plotFieldLoessFraction
                        // The series palette follows the persisted plot setting,
                        // never the theme - see components/ColourVisionBar.qml.
                        colourVision: root.app.plotColourVision
                        colourVisionPreview: root.app.plotColourVisionPreview
                        // The field colour map and the full-render behaviour
                        // belong to the person rather than to a figure, so they
                        // are persisted on the controller and bound down here.
                        // Assigned rather than bound in the other direction:
                        // ColourMapSelector writes to app.plotColourMap, which
                        // then reaches the canvas through this binding.
                        colourMap: root.app.plotColourMap
                        fullRenderPolicy: root.app.fullRenderPolicy
                        fullRenderAskAfterSeconds: root.app.fullRenderAskAfterSeconds
                    }
                    // Placing and editing notes. Only the part that has to ask a
                    // person what a note says: the canvas stores and draws them,
                    // so the PDF export and the self-test need none of this.
                    AnnotationLayer { canvas: plot }
                    // Bottom right of the figure. It used to have to sit above
                    // the floating control row to avoid landing on the Export
                    // button; with the controls in the header there is nothing
                    // below it to clear.
                    PreviewReadyNotice {
                        canvas: plot
                        anchors {
                            right: plot.right
                            bottom: plot.bottom
                            rightMargin: 12; bottomMargin: 12
                        }
                    }
                    // The full-resolution progress, at the bottom right of the
                    // FIGURE - which is where its own comment always said it
                    // sat, and where it was asked for. It was in the panel
                    // header instead: a long way from the picture it is about,
                    // and in a row that has to make space for it whether or not
                    // a render is running. Here it costs nothing when idle,
                    // because it is over the figure rather than beside it.
                    //
                    // Above the preview notice rather than beside it: the two
                    // are never wanted at once - one says a render is running,
                    // the other that one has finished - but sharing a corner
                    // means neither has to move when the other appears.
                    RenderProgressBadge {
                        canvas: plot
                        anchors {
                            right: plot.right
                            bottom: plot.bottom
                            rightMargin: 12; bottomMargin: 12
                        }
                    }
                    }

                }
                // Underneath, wrapping, dismissible.
                PlotNotice {
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    Layout.bottomMargin: 6
                    text: plot.notice
                    warning: true
                }
                }
                }
            }
            }
        }
    }

    // Everything about writing the figure out. At workspace scope rather than
    // inside the header row, so it is centred on the window and outlives the
    // button that opens it.
    ExportDialog {
        id: exportDialog
        app: root.app
        canvas: plot
    }

    // A second panel on the OPPOSITE edge, for the layouts that keep browsing
    // and editing visible at once rather than making them take turns. Figma's
    // asymmetry and JupyterLab's two sidebars are both this.
    DockPanel {
        id: secondaryDock
        title: root.secondaryTitle
        // A third pane needs a window wide enough to hold three. Below that it
        // steps aside rather than being squeezed off the edge - its contents
        // are all reachable from the main sidebar, so nothing is lost.
        visible: root.spec.secondaryPanel !== ""
                 && (!root.spec.inspectorSummons || root.secondarySummoned)
                 && root.width >= 820
        SplitView.preferredWidth: root.spec.secondaryWidth
        SplitView.minimumWidth: 200
        SplitView.maximumWidth: Math.max(200, root.width - 380)
        floatingWidth: 380
        floatingHeight: 560

        Loader {
            anchors.fill: parent
            active: secondaryDock.visible
            sourceComponent: root.spec.secondaryPanel === "data" ? dataPanel : mapPanel
        }
        Component {
            id: mapPanel
            MappingPanel {
                app: root.app
                canvas: root.canvas
                onApplyRequested: {
                    var t = root.targetCanvas("the column mapping")
                    if (!t) return
                    t.xColumn = xValue
                    t.yColumns = yValue ? [yValue] : []
                    t.zColumn = zValue ? zValue : ""
                    t.colorColumn = colorValue ? colorValue : ""
                }
            }
        }
        Component {
            id: dataPanel
            DataWorkspace { app: root.app }
        }
    }
        }

        // The right-hand rail, for the layouts that put a strip on both edges.
        IconRail {
            Layout.fillHeight: true
            visible: root.spec.railRightToo
            currentIndex: root.chosenPanel
            onPanelChosen: (i) => { root.chosenPanel = i; root.showSidebar() }
            onCommandRequested: root.commandRequested()
        }
    }

    // The strip along the bottom. A data table wants width and gets none in a
    // sidebar; solver and import messages are output and belong under the thing
    // that produced them; and the page bar is the layout's whole navigation.
    Loader {
        // A pane of the window split rather than a fixed band. The binding is
        // the height it OPENS at; SplitView overwrites it the moment the handle
        // is dragged, which is the point.
        SplitView.preferredHeight: root.spec.bottomStrip === root.stripPageBar
                                   ? 44 : root.bottomHeight
        // A page bar is navigation and has one useful height; everything else
        // can be dragged down to a title strip and back.
        SplitView.minimumHeight: root.spec.bottomStrip === root.stripPageBar ? 44 : 28
        SplitView.maximumHeight: root.spec.bottomStrip === root.stripPageBar
                                 ? 44 : Math.max(28, root.height - 200)
        active: root.spec.bottomStrip !== root.stripNone
        // An invisible child is not a pane, so a layout with no strip gets no
        // second pane and no handle to drag into nothing.
        visible: active
        sourceComponent: {
            switch (root.spec.bottomStrip) {
            case root.stripDataTable: return bottomData
            case root.stripLog:       return bottomLog
            case root.stripPageBar:   return bottomPages
            case root.stripSections:  return bottomSections
            default:                  return null
            }
        }
    }
    Component {
        id: bottomData
        DataWorkspace { app: root.app }
    }
    Component {
        id: bottomLog
        Rectangle {
            color: Theme.surfaceAlt
            border.color: Theme.border
            ScrollView {
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                TextArea {
                    readOnly: true
                    color: Theme.textSecondary
                    font.family: "Consolas, monospace"
                    font.pixelSize: 11
                    text: root.app.messageLog
                    wrapMode: TextArea.NoWrap
                }
            }
        }
    }
    Component {
        id: bottomPages
        PageBar {
            onPageChosen: (panel) => { root.chosenPanel = panel; root.showSidebar() }
        }
    }
    Component {
        id: bottomSections
        Rectangle {
            color: Theme.surfaceAlt
            border.color: Theme.border
            // Prism's typed sections. Data, Results, Graphs and Layouts are the
            // four kinds of sheet a project is made of there, and the tabs say
            // which kind you are looking at rather than which document.
            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Repeater {
                    model: [
                        { name: "Data",    panel: 2 },
                        { name: "Results", panel: 4 },
                        { name: "Graphs",  panel: 0 },
                        { name: "Layouts", panel: 7 }
                    ]
                    delegate: ToolButton {
                        required property var modelData
                        text: modelData.name
                        font.pixelSize: 11
                        checkable: true
                        checked: root.chosenPanel === modelData.panel
                        onClicked: { root.chosenPanel = modelData.panel; root.showSidebar() }
                    }
                }
            }
        }
    }
    }
}
