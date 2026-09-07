import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ToolBar {
    id: root
    required property var app
    signal importRequested()
    signal literatureRequested()
    height: 58

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
    readonly property bool compact: width < 1620
    readonly property bool narrow: width < 1400
    readonly property bool tiny: width < 1150

    background: Rectangle { color: Theme.surface; border.color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: root.compact ? 4 : 7

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

        ToolButton { text: "⌂ Home"; onClicked: root.app.workspaceMode = "Home" }
        ToolButton {
            text: root.narrow ? "▣ Literature" : "▣ Read Literature"
            font.bold: true
            // Switches to the tab and nothing else. Opening a file browser
            // unasked, just because nothing was loaded yet, is the tab deciding
            // what you wanted; the button inside it is where that belongs.
            onClicked: root.app.workspaceMode = "Literature"
        }
        ToolButton { text: "◈ Visualize"; onClicked: root.app.workspaceMode = "Visualize" }
        ToolButton { text: "Data"; onClicked: root.app.workspaceMode = "Data" }
        ToolButton { text: "Analysis"; onClicked: root.app.workspaceMode = "Analysis" }
        ToolButton { text: "Publish"; onClicked: root.app.workspaceMode = "Publish" }
        ToolSeparator {}
        ToolButton { text: "Import data"; onClicked: root.importRequested() }
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
        }
    }
}
