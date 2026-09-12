pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ToolBar {
    id: root
    required property var app
    signal importRequested()
    signal literatureRequested()
    signal addOnsRequested()

    // The layout's own opinion about this bar. 0 words, 1 symbols, 2 gone -
    // see NavStyle in UiLayouts.h. Every layout in the table used to leave this
    // bar exactly as it is, so however differently two shapes arranged their
    // panels, the window looked identical from the neck up.
    readonly property int navStyle: (root.app.uiLayoutSpec
                                     && root.app.uiLayoutSpec.navStyle !== undefined)
                                    ? root.app.uiLayoutSpec.navStyle : 0
    readonly property bool symbols: root.navStyle === 1

    // Folded away, and remembered between sessions.
    //
    // A 58 px band across the top of the window is the single biggest thing
    // between the person and a bigger figure, and every control on it is also
    // in the menu bar or behind Ctrl+K.
    //
    // Folded, the bar is a strip that SAYS what it is. The first version of
    // this was 14 px with a small caret in the middle in the muted text colour,
    // which was reported the only way that could be reported: the bar was
    // closed and the way back could not be found. A control whose whole job is
    // to be discovered after the thing it restores has vanished has to be the
    // most obvious thing on that strip, not the most tasteful - so it is 22 px,
    // it is tinted, the caret is the accent colour, it carries the words "Show
    // toolbar", and the entire width of the window is the click target.
    readonly property bool folded: root.app.topBarCollapsed
    // implicitHeight, NOT height. Main.qml gives the window header
    // `height: visible ? implicitHeight : 0`, and an assignment there overrides
    // a `height` binding written here - so this was a `height:` that never
    // reached the window, which is why the symbols layouts were still 58 px
    // tall rather than the 38 they ask for.
    implicitHeight: root.folded ? 22 : (root.symbols ? 38 : 58)

    // The toolbar carries more controls than fit on a 1280-wide window, and a
    // RowLayout will not shrink a ToolButton below its implicit width - it just
    // pushes the right-hand end past the edge, where it is clipped and
    // unreachable. So the bar degrades in two deliberate steps instead:
    //
    //   compact  drop the "Theme"/"Display" labels, narrow the combo boxes and
    //            reduce "Experimental UI" to its glyph
    //   narrow   drop the title and reduce Undo/Redo/Reset camera to glyphs
    //   tiny     fold theme and display into one "View" button with a popup
    //
    // The thresholds are the widths at which each layout stops fitting, plus a
    // margin for the longest theme name. At tiny the pickers are the last thing
    // to go, because they are still reachable from the View popup.
    // Symbols mode reuses the degradation the bar already does when a window is
    // narrow, rather than inventing a second way to be small: at "tiny" the
    // pickers are already one View popup and the actions are already glyphs, so
    // the whole bar is symbols and the only thing left to do is the six
    // workspace buttons below.
    readonly property bool compact: root.symbols || width < 1620
    readonly property bool narrow: root.symbols || width < 1400
    readonly property bool tiny: root.symbols || width < 1150

    background: Rectangle { color: Theme.surface; border.color: Theme.border }

    // The folded strip. The whole width of it is the way back.
    Rectangle {
        anchors.fill: parent
        visible: root.folded
        // Tinted rather than the surface colour, so the strip reads as a
        // control rather than as the edge of the window.
        color: foldedHover.hovered
               ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.26)
               : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.13)
        Row {
            anchors.centerIn: parent
            spacing: 6
            Label {
                text: "▾"
                color: Theme.accent
                font.pixelSize: 11
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                text: "Show toolbar"
                color: foldedHover.hovered ? Theme.text : Theme.textSecondary
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        HoverHandler { id: foldedHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.app.topBarCollapsed = false }
        ToolTip.visible: foldedHover.hovered
        ToolTip.text: "Show the toolbar again  ·  also under View ▸ Toolbar"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: root.compact ? 4 : 7
        visible: !root.folded

        Image {
            source: "qrc:/qt/qml/GraphVis/assets/graphvis_icon.png"
            sourceSize.width: 31; sourceSize.height: 31
            fillMode: Image.PreserveAspectFit
        }
        Label {
            text: "GraphVis 18"
            font.pixelSize: 19; font.bold: true
            color: Theme.text
            visible: !root.narrow
        }
        ToolSeparator {}

        // The six workspaces. One Repeater rather than six buttons, so a
        // symbol, a word and a tooltip cannot drift apart between them - and
        // the current one is marked, which the six hand-written buttons never
        // were: the bar said where you could go and never where you are.
        Repeater {
            model: [
                { mode: "Home",       glyph: "⌂", label: "Home" },
                { mode: "Literature", glyph: "▣", label: "Read Literature" },
                { mode: "Visualize",  glyph: "◈", label: "Visualize" },
                { mode: "Data",       glyph: "▤", label: "Data" },
                { mode: "Analysis",   glyph: "Σ", label: "Analysis" },
                { mode: "Publish",    glyph: "↗", label: "Publish" }
            ]
            delegate: ToolButton {
                id: navButton
                required property var modelData
                text: root.symbols ? navButton.modelData.glyph
                                   : navButton.modelData.glyph + " " + navButton.modelData.label
                checkable: true
                checked: root.app.workspaceMode === navButton.modelData.mode
                font.bold: navButton.checked
                // Switches to the workspace and nothing else. Opening a file
                // browser unasked, because nothing is loaded yet, is the tab
                // deciding what you wanted; the button inside it is where that
                // belongs.
                onClicked: root.app.workspaceMode = navButton.modelData.mode
                ToolTip.visible: root.symbols && navButton.hovered
                ToolTip.text: navButton.modelData.label
            }
        }
        ToolSeparator {}
        ToolButton {
            text: root.symbols ? "⤓" : "Import data"
            onClicked: root.importRequested()
            ToolTip.visible: root.symbols && hovered
            ToolTip.text: "Import data"
        }
        ToolButton {
            text: root.narrow ? "↶" : "Undo"
            enabled: !root.app.busy
            onClicked: root.app.undo()
            ToolTip.visible: root.narrow && hovered; ToolTip.text: "Undo"
        }
        ToolButton {
            text: root.narrow ? "↷" : "Redo"
            enabled: !root.app.busy
            onClicked: root.app.redo()
            ToolTip.visible: root.narrow && hovered; ToolTip.text: "Redo"
        }

        Item { Layout.fillWidth: true; Layout.minimumWidth: 8 }

        ToolButton {
            text: root.compact ? "✦" : (checked ? "✦ Experimental UI" : "Experimental UI")
            checkable: true
            checked: root.app.experimentalUi
            ToolTip.visible: hovered
            ToolTip.text: "Switch between the stable scientific layout and the new literature-first interface"
            onToggled: root.app.experimentalUi = checked
        }

        // The window's shape. Beside the renderer because they are the same
        // kind of question - which machinery is drawing, and what the window
        // looks like around it - and both belong to the Visualize workspace.
        Label {
            text: "Layout"; color: Theme.textSecondary
            visible: root.app.workspaceMode === "Visualize" && !root.compact
        }
        // A button that opens a grouped menu now, not a combo box: twenty-one
        // layouts in one flat list would be a list nobody reads, and the family
        // is the question a person can actually answer first.
        LayoutPicker {
            app: root.app
            Layout.preferredWidth: root.compact ? 140 : 190
            visible: root.app.workspaceMode === "Visualize"
        }

        Label {
            text: "Renderer"; color: Theme.textSecondary
            visible: root.app.workspaceMode === "Visualize" && !root.compact
        }
        ComboBox {
            Layout.preferredWidth: root.compact ? 118 : 150
            visible: root.app.workspaceMode === "Visualize"
            model: ["Qt 2-D", "Native WGPU", "VTK / PBR"]
            currentIndex: root.app.rendererMode === "VTK / PBR" ? 2
                          : (root.app.rendererMode === "Native WGPU" ? 1 : 0)
            onActivated: root.app.rendererMode = currentText
            ToolTip.visible: root.compact && hovered
            ToolTip.text: "Renderer"
        }
        ToolButton {
            text: root.narrow ? "⟲" : "Reset camera"
            visible: root.app.workspaceMode === "Visualize"
            onClicked: root.app.resetCamera()
            ToolTip.visible: root.narrow && hovered; ToolTip.text: "Reset camera"
        }
        ToolButton { text: "Cancel"; visible: root.app.busy; onClicked: root.app.cancelActiveJob() }
        ToolSeparator {}

        // 100 themes in seven groups, so this is a searchable grouped picker
        // with palette previews rather than a ComboBox nobody could scan.
        Label { text: "Theme"; color: Theme.textSecondary; visible: !root.compact }
        ThemePicker {
            id: themeBox
            app: root.app
            visible: !root.tiny
            compact: root.compact
            Layout.preferredWidth: root.compact ? 62 : 200
            Layout.preferredHeight: 30
        }

        // How the window opens, remembered between sessions.
        Label { text: "Display"; color: Theme.textSecondary; visible: !root.compact }
        ComboBox {
            id: displayBox
            visible: !root.tiny
            Layout.preferredWidth: root.compact ? 150 : 178
            model: root.app.displayModeNames
            currentIndex: root.app.displayMode
            onActivated: root.app.displayMode = currentIndex
            ToolTip.visible: hovered
            ToolTip.text: "Window mode. F11 toggles fullscreen, Esc leaves it."
        }

        // Optional components - domain formats, chart reading. Always reachable,
        // because a user who does not know these exist cannot ask for them.
        ToolButton {
            visible: !root.tiny
            text: root.narrow ? "⊕" : "⊕ Add-ons"
            onClicked: root.addOnsRequested()
            ToolTip.visible: hovered
            ToolTip.text: "Install or remove the optional science components"
        }

        // Very narrow windows get both pickers in one popup rather than losing
        // them off the right-hand edge.
        ToolButton {
            id: viewButton
            visible: root.tiny
            text: "⚙ View"
            onClicked: viewPopup.opened ? viewPopup.close() : viewPopup.open()
            ToolTip.visible: hovered
            ToolTip.text: "Theme and window mode"
        }

        // Last on the row, hard against the right-hand edge, which is where a
        // window's own fold controls live.
        ToolButton {
            id: foldButton
            text: "▴"
            implicitWidth: 24
            onClicked: root.app.topBarCollapsed = true
            ToolTip.visible: foldButton.hovered
            ToolTip.text: "Fold the toolbar away — everything on it is also in "
                        + "the menus and in Ctrl+K"
        }
    }

    Popup {
        id: viewPopup
        // Same reason as ThemePicker: no Overlay.overlay in a geometry binding.
        x: root.width - width - 14
        y: root.height + 6
        margins: 8
        padding: 14
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.borderStrong
            radius: 8
        }
        ColumnLayout {
            spacing: 8
            Label { text: "Theme"; color: Theme.textSecondary }
            ThemePicker {
                app: root.app
                Layout.preferredWidth: 240
                Layout.preferredHeight: 32
            }
            Label { text: "Display"; color: Theme.textSecondary }
            ComboBox {
                Layout.preferredWidth: 240
                model: root.app.displayModeNames
                currentIndex: root.app.displayMode
                onActivated: root.app.displayMode = currentIndex
            }
            Label {
                text: "F11 toggles fullscreen · Esc leaves it"
                color: Theme.textMuted
                font.pixelSize: 11
            }
            MenuSeparator { Layout.fillWidth: true }
            Button {
                Layout.fillWidth: true
                text: "Add-ons…"
                onClicked: { viewPopup.close(); root.addOnsRequested() }
            }
        }
    }
}
