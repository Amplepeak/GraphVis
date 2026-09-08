// Qt 6 delegate scoping. Without this, an id from the enclosing file is not
// legally visible inside a delegate or an inline Component - it resolves only
// because the old unbound context lookup walks out of the component, which is
// slower at every evaluation and silently breaks the moment a model role
// shares a name. Bound resolves ids at compile time; delegates declare what
// they take from the model with `required property`.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import "components"
import "workspaces"
import "experimental"
import GraphVis

ApplicationWindow {
    id:root
    required property var app
    // Geometry and visibility are decided in applyDisplayMode() below, once the
    // target screen is known, so the window is never painted at Qt's default
    // position first. Visibility is driven only through `visibility` - setting
    // `visible` as well makes Qt warn about conflicting properties.
    width:1640;height:1000;visibility:Window.Hidden;minimumWidth:1120;minimumHeight:720
    title:"GraphVis 18 — "+app.workspaceName
    color:Theme.background

    // One palette for the whole window.
    //
    // Qt Quick Controls' Basic style draws Switch/CheckBox labels, ComboBox and
    // SpinBox text from the palette, not from any colour we set per-item. With
    // the default palette those are dark-on-dark (invisible labels) or
    // white-on-white boxes that ignore the theme entirely. Palettes propagate
    // down the item tree, so setting it once here fixes every control and makes
    // the theme actually apply.
    palette.window: Theme.surface
    palette.windowText: Theme.text
    palette.base: Theme.surfaceAlt
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surfaceAlt
    palette.buttonText: Theme.text
    palette.mid: Theme.border
    palette.dark: Theme.borderStrong
    palette.light: Theme.surfaceAlt
    palette.midlight: Theme.border
    palette.shadow: Theme.background
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    palette.placeholderText: Theme.textMuted
    palette.toolTipBase: Theme.surfaceAlt
    palette.toolTipText: Theme.text
    palette.link: Theme.accent
    // Disabled controls must stay readable, not merely dimmed.
    palette.disabled.windowText: Theme.textDisabled
    palette.disabled.text: Theme.textDisabled
    palette.disabled.buttonText: Theme.textDisabled
    palette.disabled.base: Theme.surface
    palette.disabled.button: Theme.surface

    // The chosen theme survives restarts, as it did in GraphVis 17. It is
    // stored by AppController via QSettings rather than the QtCore QML module,
    // which is not part of the deployed QML layout.
    Connections {
        target: Theme
        function onCurrentIndexChanged() { root.app.themeIndex = Theme.currentIndex }
    }

    // ---------------------------------------------------------------- display
    //
    // Four start-up modes, the set desktop applications and games conventionally
    // offer. Qt on its own centres a fixed-size window on the *virtual* desktop,
    // so on a two-monitor machine it opens straddling the bezel; every mode here
    // therefore asks AppController for one real screen's geometry first.
    readonly property int modeWindowed: 0
    readonly property int modeMaximised: 1
    readonly property int modeFullscreen: 2
    readonly property int modeBorderless: 3
    property bool applyingDisplayMode: false

    // The window's own flags as Qt created them. Assigning `flags` at all -
    // even the same value - recreates the native window, and a recreated window
    // came back painting black. Keeping the original value means windowed,
    // maximised and fullscreen never touch it.
    property int baseFlags: 0

    function applyDisplayMode(mode) {
        root.applyingDisplayMode = true

        var wantFrameless = (mode === root.modeBorderless)
        var targetFlags = wantFrameless ? (root.baseFlags | Qt.FramelessWindowHint)
                                        : root.baseFlags
        var changingFrame = (root.flags !== targetFlags)

        // Only a frame-style change needs the native window rebuilt, and only
        // then is the hide/show dance worth its flicker.
        if (changingFrame && root.visibility !== Window.Hidden)
            root.visibility = Window.Hidden
        if (changingFrame)
            root.flags = targetFlags

        if (mode === root.modeFullscreen) {
            // Qt's own fullscreen, which on Windows is a borderless window
            // covering the screen - no display mode change, no frame change.
            root.visibility = Window.FullScreen
        } else if (mode === root.modeBorderless) {
            // A frameless window over the work area, so the taskbar stays put.
            var full = app.targetWorkAreaGeometry()
            root.x = full.x; root.y = full.y
            root.width = full.width; root.height = full.height
            root.visibility = Window.Windowed
        } else {
            var box = app.preferredWindowGeometry(1640, 1000)
            root.x = box.x; root.y = box.y
            root.width = box.width; root.height = box.height
            root.visibility = (mode === root.modeMaximised) ? Window.Maximized
                                                            : Window.Windowed
        }

        root.applyingDisplayMode = false
        root.requestActivate()
        repaintNudge.restart()
    }

    // Belt and braces for the transition above: some drivers hand back a valid
    // but unpainted surface and only draw once the scene is marked dirty again.
    // Nudging an invisible one-pixel item does that, for a few frames.
    Item {
        id: repaintTick
        width: 1; height: 1; opacity: 0.0
    }
    Timer {
        id: repaintNudge
        interval: 50; repeat: true
        property int ticks: 0
        onTriggered: {
            repaintTick.x = (repaintTick.x === 0) ? 1 : 0
            if (++ticks > 8) { ticks = 0; stop() }
        }
        onRunningChanged: if (running) ticks = 0
    }

    Component.onCompleted: {
        Theme.currentIndex = app.themeIndex
        root.baseFlags = root.flags
        applyDisplayMode(app.displayMode)
    }

    Connections {
        target: root.app
        function onDisplayModeChanged() { root.applyDisplayMode(root.app.displayMode) }
    }

    // Remember where the user put the window, but only in windowed mode - a
    // maximised or fullscreen geometry would be restored as a "windowed" window
    // exactly covering the screen, which looks broken.
    onXChanged: root.rememberGeometry()
    onYChanged: root.rememberGeometry()
    onWidthChanged: root.rememberGeometry()
    onHeightChanged: root.rememberGeometry()
    function rememberGeometry() {
        if (root.applyingDisplayMode || root.visibility === Window.Hidden) return
        if (app.displayMode !== root.modeWindowed) return
        geometryTimer.restart()
    }
    Timer {
        id: geometryTimer
        interval: 400
        onTriggered: root.app.saveWindowGeometry(root.x, root.y, root.width, root.height)
    }

    // F11 toggles fullscreen and Escape leaves it, as in a browser or a game.
    Shortcut {
        sequences: ["F11"]
        onActivated: root.app.displayMode = (root.app.displayMode === root.modeFullscreen)
                                       ? root.modeWindowed : root.modeFullscreen
    }
    Shortcut {
        sequences: ["Esc"]
        enabled: root.app.displayMode === root.modeFullscreen || root.app.displayMode === root.modeBorderless
        onActivated: root.app.displayMode = root.modeWindowed
    }

    FileDialog{id:importDialog;title:"Import scientific dataset";nameFilters:root.app.importNameFilters();onAccepted:root.app.importDataset(selectedFile)}

    // Drop a file on the window to import it.
    //
    // The shortest path from "my data is in that folder" to "it is on screen"
    // is dragging it here, and it costs one item. A DropArea handles only drag
    // events, so it can sit over everything without taking a single click away
    // from the controls underneath.
    DropArea {
        anchors.fill: parent
        z: 9000
        onDropped: (drop) => {
            if (!drop.hasUrls) return
            // Every file dropped, in the order given. The importer already
            // queues them, which is what makes dragging a folder's worth of
            // runs onto the window do the obvious thing.
            for (var i = 0; i < drop.urls.length; ++i)
                root.app.importDataset(drop.urls[i])
            drop.accept()
        }
        Rectangle {
            anchors.fill: parent
            visible: parent.containsDrag
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.10)
            border.color: Theme.accent
            border.width: 2
            radius: Theme.radius
            Label {
                anchors.centerIn: parent
                text: "Drop to import"
                color: Theme.text
                font.pixelSize: 20
                font.bold: true
            }
        }
    }
    FileDialog{id:literatureDialog;title:"Open literature";nameFilters:root.app.literatureNameFilters();onAccepted:root.app.openLiterature(selectedFile)}

    // The window's menu bar. Settings that used to be spread across the
    // toolbar, a strip above the graph chooser and a combo box floating over
    // the corner of the plot now have one home; the toolbar keeps the actions
    // people reach for constantly.
    menuBar: MainMenuBar {
        app: root.app
        canvas: shellLoader.item ? shellLoader.item.canvas : null
        onImportRequested: importDialog.open()
        onLiteratureRequested: literatureDialog.open()
        onAddOnsRequested: addOnsDialog.open()
        onExportRequested: {
            var c = shellLoader.item ? shellLoader.item.canvas : null
            if (!c) return
            var target = root.app.exportPath(c.engine, "pdf")
            if (c.exportPdf(target)) root.app.notify("Exported " + target)
            else root.app.notify("PDF export failed")
        }
    }

    header:TopBar{app:root.app;onImportRequested:importDialog.open();onLiteratureRequested:literatureDialog.open();onAddOnsRequested:addOnsDialog.open()}

    // Add-ons gets a window of its own rather than a ninth sidebar tab: it is
    // something you visit twice a year, not something you work in.
    Dialog {
        id: addOnsDialog
        title: "GraphVis add-ons"
        modal: true
        anchors.centerIn: parent
        width: Math.min(760, root.width - 80)
        height: Math.min(720, root.height - 80)
        standardButtons: Dialog.Close
        AddOnsPanel {
            anchors.fill: parent
            app: root.app
        }
    }

    Loader {
        id: shellLoader
        anchors{left:parent.left;right:parent.right;top:parent.top;bottom:status.top}
        sourceComponent:root.app.experimentalUi?experimentalShell:classicShell
    }
    Component{id:classicShell;ClassicShell{app:root.app;onImportRequested:importDialog.open();onLiteratureRequested:literatureDialog.open()}}
    Component{id:experimentalShell;ExperimentalShell{app:root.app;onImportRequested:importDialog.open();onLiteratureRequested:literatureDialog.open()}}
    StatusStrip{id:status;anchors{left:parent.left;right:parent.right;bottom:parent.bottom} app:root.app}
}
