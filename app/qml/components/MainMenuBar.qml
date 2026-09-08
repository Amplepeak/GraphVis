// The menu bar GraphVis 17 had and this rewrite did not.
//
// Everything below already existed and was reachable - somewhere. The theme
// picker was a combo box in the toolbar, the figure background and grid were in
// a strip above the graph chooser, the full-resolution preview policy was a
// combo box floating over the bottom right corner of the plot, and the axis
// scales had no control at all. Six places, none of them where a person looks
// first, and the toolbar had already been through three collapse breakpoints
// trying to fit them.
//
// A menu bar is where an application's settings go. It costs 24 px, it does not
// have to shrink as the window narrows, and it is the first place anybody looks
// for "how do I turn that off".
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import GraphVis

MenuBar {
    id: root
    required property var app
    // The live canvas, for the items that act on the figure rather than on a
    // persisted setting. Null while a non-Qt renderer is showing.
    property var canvas: null

    signal importRequested()
    signal literatureRequested()
    signal addOnsRequested()
    signal exportRequested()

    Menu {
        title: "&File"

        Action {
            text: "Import dataset…"
            shortcut: StandardKey.Open
            onTriggered: root.importRequested()
        }
        Action {
            text: "Open literature…"
            onTriggered: root.literatureRequested()
        }

        MenuSeparator {}

        Menu {
            id: recentData
            title: "Recent datasets"
            // Greyed rather than hidden: an empty Recent menu tells you the
            // feature exists and that you have not used it yet, where a missing
            // one tells you nothing.
            enabled: root.app.recentDatasets.length > 0
            Repeater {
                model: root.app.recentDatasets
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name
                    // The path, because two datasets called "run" from two
                    // folders are otherwise the same line twice.
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.path
                    onTriggered: root.app.openRecentDataset(modelData.path)
                }
            }
        }
        Menu {
            id: recentVis
            title: "Recent visualisations"
            enabled: root.app.recentVisualisations.length > 0
            Repeater {
                model: root.app.recentVisualisations
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.label
                    enabled: root.canvas !== null
                    onTriggered: {
                        root.app.rendererMode = "Qt 2-D"
                        root.canvas.engine = modelData.engine
                        root.canvas.variant = modelData.variant ? modelData.variant : ""
                        root.canvas.title = modelData.engine
                    }
                }
            }
        }
        Action {
            text: "Clear recent files"
            enabled: root.app.recentDatasets.length > 0
                     || root.app.recentVisualisations.length > 0
            onTriggered: root.app.clearRecents()
        }

        MenuSeparator {}

        Action {
            text: "Save figure as…"
            enabled: root.canvas !== null && root.canvas.pointCount > 0
            onTriggered: root.exportRequested()
        }

        MenuSeparator {}

        Action {
            text: "Quit"
            shortcut: StandardKey.Quit
            onTriggered: Qt.quit()
        }
    }

    Menu {
        title: "&Edit"
        Action {
            text: "Undo"
            shortcut: StandardKey.Undo
            onTriggered: root.app.undo()
        }
        Action {
            text: "Redo"
            shortcut: StandardKey.Redo
            onTriggered: root.app.redo()
        }
        MenuSeparator {}
        Action {
            text: "Reset the figure's view"
            enabled: root.canvas !== null
            onTriggered: {
                if (root.canvas.view3D) root.canvas.resetCamera()
                else root.canvas.resetView()
            }
        }
        Action {
            text: "Clear notes on the figure"
            enabled: root.canvas !== null && root.canvas.annotationCount > 0
            onTriggered: root.canvas.clearAnnotations()
        }
    }

    Menu {
        title: "&View"

        Menu {
            title: "Interface theme"
            Repeater {
                model: Theme.themes
                delegate: MenuItem {
                    required property var modelData
                    required property int index
                    text: modelData.name
                    checkable: true
                    checked: Theme.currentIndex === index
                    onTriggered: Theme.currentIndex = index
                }
            }
        }

        Menu {
            title: "Figure background"
            Repeater {
                model: root.app.figureThemeNames
                delegate: MenuItem {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: root.app.figureTheme === index
                    onTriggered: root.app.figureTheme = index
                }
            }
        }

        MenuSeparator {}

        Action {
            text: "Grid"
            checkable: true
            checked: root.app.plotGridVisible
            onTriggered: root.app.plotGridVisible = !root.app.plotGridVisible
        }
        Menu {
            title: "Grid density"
            enabled: root.app.plotGridVisible
            Repeater {
                model: [
                    { label: "Default", value: 0 },
                    { label: "Sparse · 4", value: 4 },
                    { label: "Normal · 7", value: 7 },
                    { label: "Fine · 12", value: 12 },
                    { label: "Very fine · 20", value: 20 }
                ]
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.label
                    checkable: true
                    checked: root.app.plotGridDensity === modelData.value
                    onTriggered: root.app.plotGridDensity = modelData.value
                }
            }
        }
        Action {
            text: "Numbers on the axes"
            checkable: true
            checked: root.app.plotScaleLabels
            onTriggered: root.app.plotScaleLabels = !root.app.plotScaleLabels
        }

        Menu {
            title: "Filling gaps in a field"
            // Heat maps, contours and surfaces bin scattered measurements into
            // a grid, and most of that grid catches nothing. None is honest and
            // unreadable; the rest estimate, which is a choice the person makes
            // rather than one the program makes for them.
            Repeater {
                model: root.app.plotFieldInterpolationNames
                delegate: MenuItem {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: root.app.plotFieldInterpolation === index
                    onTriggered: root.app.plotFieldInterpolation = index
                }
            }
        }

        MenuSeparator {}

        Menu {
            title: "Layout"
            // The six window shapes. Notebook is the default; the rest are here
            // rather than discarded, because which one suits a person is not
            // something the application can decide for them.
            Repeater {
                model: root.app.uiLayoutNames
                delegate: MenuItem {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: root.app.uiLayout === index
                    ToolTip.visible: hovered
                    ToolTip.text: root.app.uiLayoutDescriptions[index]
                    onTriggered: root.app.uiLayout = index
                }
            }
        }

        MenuSeparator {}

        Menu {
            title: "Window"
            Repeater {
                model: root.app.displayModeNames
                delegate: MenuItem {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: root.app.displayMode === index
                    onTriggered: root.app.displayMode = index
                }
            }
        }
    }

    Menu {
        title: "&Options"

        Menu {
            title: "Full-resolution render"
            Repeater {
                model: root.app.fullRenderPolicyNames
                delegate: MenuItem {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: root.app.fullRenderPolicy === index
                    onTriggered: root.app.fullRenderPolicy = index
                }
            }
            MenuSeparator {}
            Menu {
                title: "Ask above"
                // Only means anything on the policy that asks conditionally.
                enabled: root.app.fullRenderPolicy === 2
                Repeater {
                    model: [1, 3, 5, 10, 30, 60]
                    delegate: MenuItem {
                        required property int modelData
                        text: modelData + " s"
                        checkable: true
                        checked: Math.abs(root.app.fullRenderAskAfterSeconds - modelData) < 0.01
                        onTriggered: root.app.fullRenderAskAfterSeconds = modelData
                    }
                }
            }
        }

        Menu {
            title: "Graph colours"
            Repeater {
                model: root.app.plotColourVisionNames
                delegate: MenuItem {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: root.app.plotColourVision === index
                    onTriggered: root.app.plotColourVision = index
                }
            }
        }

        Menu {
            title: "Renderer"
            // Each with what it is actually for. The three names on their own
            // said nothing about which to pick, and two of them can take a
            // moment to start.
            Repeater {
                model: [
                    { name: "Qt 2-D",
                      why: "The 203 catalogue engines, exported as true vector PDF. The default, and the only one that can export vectors." },
                    { name: "Rust / WGPU",
                      why: "GPU point cloud for very large scatter and volume data. Draws through a native surface, so it cannot export vectors." },
                    { name: "VTK / PBR",
                      why: "Lit, rotatable 3-D with physically based materials. Needs the optional VTK component installed." }
                ]
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name
                    checkable: true
                    checked: root.app.rendererMode === modelData.name
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.why
                    onTriggered: root.app.rendererMode = modelData.name
                }
            }
        }

        Menu {
            title: "Graph packs"
            // A pack is a filter over a catalogue that ships whole - every
            // engine is in this executable either way - so switching one off
            // shortens the library and nothing else. All on by default.
            Repeater {
                model: root.app.graphPacks
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name + "  (" + modelData.entryCount + ")"
                    checkable: true
                    checked: modelData.enabled
                    enabled: modelData.id !== "base"
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.description
                    onTriggered: root.app.setPackEnabled(modelData.id, !modelData.enabled)
                }
            }
        }

        MenuSeparator {}

        Action {
            text: "Experimental interface"
            checkable: true
            checked: root.app.experimentalUi
            onTriggered: root.app.experimentalUi = !root.app.experimentalUi
        }
        Action {
            text: "Add-ons and optional components…"
            onTriggered: root.addOnsRequested()
        }
    }
}
