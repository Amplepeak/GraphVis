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
import QtQuick.Window
import GraphVis

MenuBar {
    id: root
    required property var app
    // The OPEN FIGURES, listed here as well as on the strip above the canvas.
    //
    // Handed down rather than read off the workspace: the menu bar belongs to
    // the window and the figures belong to the Visualize workspace, and a menu
    // that reached across to fetch them would be a second route to the same
    // list. Main.qml binds these from whichever shell is loaded, the same way
    // it already routes Export.
    property var figureTitles: []
    property int figureIndex: 0
    signal figureSelected(int index)
    signal figureAddRequested()

    // The layouts of one family, and that family's name. Filtering here beats
    // a second property per group on the controller, and both are needed
    // because the five submenus below are written out by hand.
    // Which estimator a name sits at, asked of the backend rather than written
    // as a number here.
    //
    // The kriging and LOESS submenus were gated on `=== 14` and `=== 16`, which
    // are correct today and silently point at the wrong estimator the moment
    // one is inserted above them - and the whole reason nine of these were
    // gated off at one point is that a control naming one thing and acting on
    // another is worse than no control.
    function estimatorIndex(name) {
        var names = root.canvas ? root.canvas.fieldEstimatorNames() : []
        for (var i = 0; i < names.length; ++i)
            if (names[i] === name) return i
        return -1
    }

    function layoutsIn(group) {
        var out = []
        var all = root.app.uiLayoutList
        for (var i = 0; i < all.length; ++i)
            if (all[i].group === group) out.push(all[i])
        return out
    }
    function groupName(group) {
        var groups = root.app.uiLayoutGroupList
        return (group >= 0 && group < groups.length) ? groups[group].name : ""
    }
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
            // A SHORTCUT, because two layouts have no header to put a button
            // in. Zen and Bare hide the canvas panel's header entirely - that
            // is the point of them - and the Export button lives in that
            // header, so on those two layouts the menu was the only route and
            // it had no key of its own.
            shortcut: "Ctrl+E"
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
        // "Reset the figure's view" used to be here. It is a VIEW operation -
        // it changes what you are looking at, not what the figure is - and View
        // is where it was looked for and not found. It is in that menu now, and
        // is not repeated here: one command in two menus is two places to keep
        // in step and one more thing to read past.
        Action {
            text: "Clear notes on the figure"
            enabled: root.canvas !== null && root.canvas.annotationCount > 0
            onTriggered: root.canvas.clearAnnotations()
        }
    }

    Menu {
        title: "&View"

        // Putting the figure back where it started. Both of them, named
        // separately rather than one entry that quietly does different things
        // on a 2-D and a 3-D figure: on a rotated surface "reset view" and
        // "reset camera" are two different undos and a person wants to ask for
        // the one they mean.
        Action {
            text: "Reset the figure's view"
            enabled: root.canvas !== null
            onTriggered: {
                if (root.canvas.view3D) root.canvas.resetCamera()
                else root.canvas.resetView()
            }
        }
        Action {
            text: "Reset the 3-D camera"
            enabled: root.canvas !== null && root.canvas.view3D
            onTriggered: if (root.canvas) root.canvas.resetCamera()
        }
        Action {
            // The wording follows the figure. On an axis-less engine there are
            // no axes to fit and resetView puts the drawing back in the middle
            // of its frame instead, so a menu item promising to fit axes would
            // be describing something that cannot happen.
            text: root.canvas && root.canvas.frameMoved && !root.canvas.viewZoomed
                  ? "Put the figure back in its frame"
                  : "Fit the axes back to the data"
            enabled: root.canvas !== null
                     && (root.canvas.viewZoomed || root.canvas.frameMoved)
            onTriggered: if (root.canvas) root.canvas.resetView()
        }

        MenuSeparator {}

        // The window's own bars. Each can also be folded from a control on the
        // bar itself; these are here so the folds are FINDABLE - a person
        // looking for "how do I get more room for the figure" looks in a menu,
        // not for a caret they have not noticed - and so a bar folded in an
        // earlier session can be brought back from a fixed place.
        Action {
            text: "Toolbar"
            checkable: true
            checked: !root.app.topBarCollapsed
            onTriggered: root.app.topBarCollapsed = !root.app.topBarCollapsed
        }
        Action {
            text: "Controls panel"
            checkable: true
            checked: !root.app.sidebarCollapsed
            onTriggered: root.app.sidebarCollapsed = !root.app.sidebarCollapsed
        }
        Action {
            text: "Workspace bar"
            checkable: true
            checked: !root.app.navRailCollapsed
            // Only the layouts that put the workspaces on an edge have one.
            enabled: !!(root.app.uiLayoutSpec && root.app.uiLayoutSpec.navEdge !== 0)
            onTriggered: root.app.navRailCollapsed = !root.app.navRailCollapsed
        }
        // THE FIGURE STRIP BELONGS WITH THE OTHER BARS, and it was the one
        // show-or-hide toggle missing from the group.
        //
        // It was in two places, neither of them this one: on the Figures menu,
        // and inside View > Colour vision - where it had nothing whatever to do
        // with colour vision and had simply been dropped beside the toolbar
        // toggle that does. So the person looking for "how do I get more room
        // for the figure", who finds Toolbar, Controls panel and Workspace bar
        // right here, did not find the fourth bar among them.
        //
        // Named as a bare noun like its three neighbours rather than "Show the
        // figure strip": they are a group of things that are either on screen
        // or not, and one of them phrased as an instruction reads as a
        // different kind of control.
        Action {
            text: "Figure strip"
            checkable: true
            checked: root.app.figureTabsVisible
            onTriggered: root.app.figureTabsVisible = !root.app.figureTabsVisible
        }

        MenuSeparator {}

        Menu {
            id: themeMenu
            title: "Interface theme"

            // The same filter the theme picker applies, from the same measured
            // tags. Two lists of themes that disagree about which are offered
            // is precisely the shape that keeps costing this project time, and
            // here it would be visible: the picker showing four entries and
            // this menu showing all hundred and eight.
            readonly property string visionKind:
                root.app ? Theme.visionKindAt(root.app.plotColourVision) : ""
            readonly property int hiddenCount:
                themeMenu.visionKind === ""
                    ? 0
                    : Theme.themeCount
                      - Theme.indicesCarrying(themeMenu.visionKind).length

            Repeater {
                model: Theme.themes
                delegate: MenuItem {
                    id: themeItem
                    required property var modelData
                    required property int index
                    // The current theme stays listed even when it fails the
                    // mode. A menu that hides what is currently ticked leaves
                    // nothing ticked, which reads as no theme being set at all.
                    readonly property bool carries:
                        Theme.themeCarries(themeItem.index, themeMenu.visionKind)
                        || Theme.currentIndex === themeItem.index
                    visible: themeItem.carries
                    height: themeItem.carries ? themeItem.implicitHeight : 0
                    text: themeItem.modelData.name
                    checkable: true
                    checked: Theme.currentIndex === themeItem.index
                    onTriggered: Theme.currentIndex = themeItem.index
                }
            }

            MenuSeparator {
                visible: themeMenu.hiddenCount > 0
                height: visible ? implicitHeight : 0
            }
            // Says what is missing and why, rather than leaving a short menu to
            // be read as a short list of themes.
            MenuItem {
                enabled: false
                visible: themeMenu.hiddenCount > 0
                height: visible ? implicitHeight : 0
                text: themeMenu.hiddenCount + " hidden — their status colours "
                      + "are one shade for " + Theme.cvdLabel(themeMenu.visionKind)
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

        // Colour vision lives HERE rather than in the sidebar.
        //
        // It had a permanent panel above the graph library, which is prime
        // space in a 400 px sidebar - and it is a setting a small number of
        // people set once and everyone else never opens. A menu costs nothing
        // when it is not wanted, and the optional toolbar below is there for
        // the people who do change it often.
        Menu {
            title: "Colour vision"
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
            MenuSeparator {}
            // What the setting actually did to the figure on screen - including
            // when the answer is "nothing, that colour map was already safe",
            // which is the answer whose absence made the setting look broken.
            MenuItem {
                enabled: false
                visible: root.canvas !== null && root.canvas.colourVisionNote !== ""
                height: visible ? implicitHeight : 0
                text: root.canvas ? root.canvas.colourVisionNote : ""
            }
            MenuSeparator {}
            // The setting turned into something you can look at.
            //
            // Everything else here is a claim the person setting it cannot
            // check: they have normal colour vision, the figure looks much as
            // it did, and they reasonably conclude nothing happened. This shows
            // the figure as the reader it is for actually receives it.
            Action {
                text: "Preview as this reader sees it"
                checkable: true
                enabled: root.app.plotColourVision > 0
                checked: root.app.plotColourVisionPreview
                onTriggered: root.app.plotColourVisionPreview
                             = !root.app.plotColourVisionPreview
            }
            MenuSeparator {}
            // The figure strip used to be here too. It is a bar you show or
            // hide, not a colour-vision setting, and it now sits with the other
            // three bars under View. Only the colour-vision toolbar is left,
            // which is the one that does belong to this menu.
            Action {
                text: "Show the colour-vision toolbar"
                checkable: true
                checked: root.app.colourVisionToolbarVisible
                onTriggered: root.app.colourVisionToolbarVisible
                             = !root.app.colourVisionToolbarVisible
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

        // How a field is estimated between the measurements.
        //
        // Two different things live here and the difference is the point. The
        // first fills the holes of an already-binned grid: cheap, and it is
        // what every figure has used so far. The second estimates from the
        // SCATTERED measurements themselves, which is what GraphVis 17 did -
        // binning first throws away where in its cell each sample was, and no
        // estimator afterwards can recover that.
        Menu {
            title: "Estimating a field"

            Menu {
                title: "Method"
                MenuItem {
                    text: "Fill the grid (fast)"
                    checkable: true
                    checked: root.app.plotFieldEstimator < 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Bin the measurements into cells first, then fill the "
                                + "cells nothing landed in. Fast, and good enough when the "
                                + "sweep is dense."
                    onTriggered: root.app.plotFieldEstimator = -1
                }
                MenuSeparator {}
                Repeater {
                    // Only what this build computes. An estimator that is named
                    // and not implemented draws something under a name that
                    // means something else.
                    model: {
                        var out = []
                        var all = root.canvas ? root.canvas.fieldEstimatorList() : []
                        for (var i = 0; i < all.length; ++i)
                            if (all[i].available) out.push(all[i])
                        return out
                    }
                    delegate: MenuItem {
                        id: estimatorItem
                        required property var modelData
                        text: estimatorItem.modelData.name
                        checkable: true
                        checked: root.app.plotFieldEstimator === estimatorItem.modelData.index
                        ToolTip.visible: estimatorItem.hovered
                        ToolTip.text: estimatorItem.modelData.group
                        onTriggered: root.app.plotFieldEstimator = estimatorItem.modelData.index
                    }
                }
            }

            Menu {
                title: "Beyond the measurements"
                enabled: root.app.plotFieldEstimator >= 0
                Repeater {
                    model: root.canvas ? root.canvas.fieldExtrapolationNames() : []
                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        checkable: true
                        checked: root.app.plotFieldExtrapolation === index
                        ToolTip.visible: hovered
                        ToolTip.text: index === 0
                            ? "The conservative default. The convex hull of the samples is "
                            + "the boundary of what was measured, and colouring past it puts "
                            + "an extrapolation on the page in the same ink as a measurement."
                            : "Extends the estimate to the whole rectangle. For when the "
                            + "rectangle IS the experiment and the corners simply failed."
                        onTriggered: root.app.plotFieldExtrapolation = index
                    }
                }
            }

            Menu {
                title: "Values"
                enabled: root.app.plotFieldEstimator >= 0
                Repeater {
                    model: root.canvas ? root.canvas.fieldValuePolicyNames() : []
                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        checkable: true
                        checked: root.app.plotFieldValuePolicy === index
                        ToolTip.visible: hovered
                        ToolTip.text: index === 1
                            ? "A spline through noisy data overshoots, and an overshoot on a "
                            + "colour scale reads as a peak nobody recorded."
                            : "Let the estimator go past the measured range where the fit says so."
                        onTriggered: root.app.plotFieldValuePolicy = index
                    }
                }
                MenuSeparator {}
                Repeater {
                    model: root.canvas ? root.canvas.fieldResponseSpaceNames() : []
                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        checkable: true
                        checked: root.app.plotFieldResponseSpace === index
                        ToolTip.visible: hovered
                        ToolTip.text: index === 1
                            ? "Estimate in log10 of the response. Halfway between 1 and 10000 "
                            + "is 5000 in linear values and 100 in log ones; for a quantity "
                            + "spanning decades the second is the one you meant."
                            : "Estimate in the values as measured."
                        onTriggered: root.app.plotFieldResponseSpace = index
                    }
                }
            }

            // The two numeric parameters that belong to one estimator each.
            // Offered only while that estimator is chosen, because a control
            // that does nothing is worse than no control.
            Menu {
                title: "Kriging variogram"
                enabled: root.app.plotFieldEstimator === root.estimatorIndex("Ordinary Kriging")
                Repeater {
                    model: root.canvas ? root.canvas.fieldKrigingVariogramNames() : []
                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        checkable: true
                        checked: root.app.plotFieldKrigingVariogram === index
                        ToolTip.visible: hovered
                        ToolTip.text: "How quickly the response is assumed to decorrelate "
                                    + "with distance. Gaussian gives the smoothest field and "
                                    + "the strongest short-range correlation; exponential the "
                                    + "least."
                        onTriggered: root.app.plotFieldKrigingVariogram = index
                    }
                }
            }
            Menu {
                title: "LOESS span"
                enabled: root.app.plotFieldEstimator === root.estimatorIndex("LOESS / LOWESS")
                Repeater {
                    model: [0.10, 0.25, 0.40, 0.60, 1.00]
                    delegate: MenuItem {
                        required property real modelData
                        text: Math.round(modelData * 100) + "% of the runs"
                        checkable: true
                        checked: Math.abs(root.app.plotFieldLoessFraction - modelData) < 0.001
                        ToolTip.visible: hovered
                        ToolTip.text: "How much of the sweep each local fit sees. Smaller "
                                    + "follows the data more closely; larger smooths harder."
                        onTriggered: root.app.plotFieldLoessFraction = modelData
                    }
                }
            }

            MenuSeparator {}

            // What a run that failed does to the picture.
            //
            // A sweep that solves an ODE at every point does not always
            // converge, and a non-finite response is an outcome rather than a
            // missing row. These three decide how much of the map one failure
            // takes out, which masked regions may be filled in again, and what
            // a region that stays masked actually shows.
            Menu {
                title: "Failed runs"
                enabled: root.app.plotFieldEstimator >= 0

                Menu {
                    title: "How much one failure masks"
                    Repeater {
                        model: root.canvas ? root.canvas.fieldFootprintNames() : []
                        delegate: MenuItem {
                            required property string modelData
                            required property int index
                            text: modelData
                            checkable: true
                            checked: root.app.plotFieldFootprint === index
                            ToolTip.visible: hovered
                            ToolTip.text: [
                                "One grid cell. The narrowest honest answer, and the one "
                              + "that keeps a dense sweep readable.",
                                "As far as the runs around it are apart — measured at that "
                              + "failure, so it widens where the sweep is thin and stays "
                              + "narrow where it is crowded.",
                                "Everything nearer to the failed run than to any successful "
                              + "one. The most conservative, and the reason a dense sweep "
                              + "full of isolated dropouts can come out looking like Swiss "
                              + "cheese.",
                                "Ignore failures and fit straight through them. Fastest, and "
                              + "it shows a surface where nothing was measured."][index]
                            onTriggered: root.app.plotFieldFootprint = index
                        }
                    }
                }

                Menu {
                    title: "Filling failures back in"
                    Repeater {
                        model: root.canvas ? root.canvas.fieldBridgingNames() : []
                        delegate: MenuItem {
                            required property string modelData
                            required property int index
                            text: modelData
                            checkable: true
                            checked: root.app.plotFieldBridging === index
                            ToolTip.visible: hovered
                            ToolTip.text: [
                                "Every failure stays a hole.",
                                "Single dropped cells only.",
                                "Enclosed holes up to the size below.",
                                "Any hole with successful runs all the way round it."][index]
                              + "\n\nA masked region touching the edge of the sweep is never "
                              + "filled, whatever this says: its far side was never measured, "
                              + "so joining across it would be invention rather than "
                              + "interpolation."
                            onTriggered: root.app.plotFieldBridging = index
                        }
                    }
                    MenuSeparator {}
                    Repeater {
                        model: [1, 4, 12, 40, 200]
                        delegate: MenuItem {
                            required property int modelData
                            text: "up to " + modelData + " cells"
                            checkable: true
                            checked: root.app.plotFieldBridgeMaxCells === modelData
                            enabled: root.app.plotFieldBridging === 2
                            onTriggered: root.app.plotFieldBridgeMaxCells = modelData
                        }
                    }
                }

                Menu {
                    title: "What a masked area shows"
                    Repeater {
                        model: root.canvas ? root.canvas.fieldInvalidDisplayNames() : []
                        delegate: MenuItem {
                            required property string modelData
                            required property int index
                            text: modelData
                            checkable: true
                            checked: root.app.plotFieldInvalidDisplay === index
                            ToolTip.visible: hovered
                            ToolTip.text: [
                                "Nothing. The honest default: a hole in the map is a region "
                              + "nothing is known about.",
                                "The figure's background colour, so the hole has an edge.",
                                "The nearest measured value, repeated.",
                                "The mean of the surrounding cells, spread inwards.",
                                "The bottom of the colour scale, so it reads as 'as low as "
                              + "this map goes' rather than as a measurement.",
                                "Reflected across the edge of the hole, so a contour running "
                              + "into it continues with the slope it arrived with instead of "
                              + "stopping dead."][index]
                            onTriggered: root.app.plotFieldInvalidDisplay = index
                        }
                    }
                }
            }

            MenuSeparator {}

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

            // HOW FINE that grid is.
            //
            // PlotCanvas has carried fieldResolution since the field engines
            // were written and nothing ever set it, so every heat map, contour
            // and surface in the program was drawn at the automatic resolution
            // with no way to ask for another. It is the difference between a
            // map of the structure and a map of the sampling: too coarse and a
            // real feature is one cell, too fine and every cell is one
            // measurement and the picture is noise.
            Menu {
                title: "Field detail"
                enabled: root.canvas !== null && root.canvas.usesColourMap
                Repeater {
                    model: [
                        { label: "Automatic — chosen from how many samples there are", cells: 0 },
                        { label: "Coarse · 40 cells", cells: 40 },
                        { label: "Medium · 80 cells", cells: 80 },
                        { label: "Fine · 160 cells", cells: 160 },
                        { label: "Very fine · 280 cells", cells: 280 }
                    ]
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        checked: root.canvas !== null && root.canvas.fieldResolution === modelData.cells
                        onTriggered: if (root.canvas) root.canvas.fieldResolution = modelData.cells
                    }
                }
            }

            // HOW MANY LINES THE MESH HAS, which is not how finely the surface
            // was sampled.
            //
            // 3D Mesh and 3D Topography / Surface build the identical grid and
            // differ by one boolean, and the mesh was every quad of that grid
            // outlined - so at the resolution a real survey gives you, the
            // strokes closed the gaps and it drew as a solid. The two engines
            // could be told apart only by zooming in far enough to see one
            // cell.
            Menu {
                title: "Mesh lines"
                enabled: root.canvas !== null && root.canvas.wireframeEngine
                Repeater {
                    model: [
                        { label: "Automatic — one line per grid cell", lines: 0 },
                        { label: "Open · 8 lines", lines: 8 },
                        { label: "Coarse · 16 lines", lines: 16 },
                        { label: "Medium · 24 lines", lines: 24 },
                        { label: "Fine · 40 lines", lines: 40 },
                        { label: "Very fine · 64 lines", lines: 64 }
                    ]
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        checked: root.canvas !== null && root.canvas.meshDensity === modelData.lines
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.lines === 0
                            ? "One line per cell of the field grid. On a dense survey "
                            + "that is a solid block rather than a mesh."
                            : "The surface keeps its own sampling — only the number of "
                            + "lines you can see through changes."
                        onTriggered: if (root.canvas) root.canvas.meshDensity = modelData.lines
                    }
                }
            }
        }

        MenuSeparator {}

        Menu {
            title: "Layout"
            // The window shapes, in families, because a flat list of
            // twenty-six names is a list nobody reads.
            //
            // These families were written out one Menu at a time, because a
            // Repeater CANNOT create a Menu - its delegate has to be an Item
            // and Menu is not one, so the submenus were created and never
            // added to anything and this menu opened onto an empty panel.
            //
            // Hand-writing them then failed the other way: a sixth family was
            // added to the table and there was no sixth Menu here, so five
            // shapes existed, were persisted, were selectable from a script,
            // and could not be reached from either place that lists them. This
            // menu and LayoutPicker are two copies of the same list, so the
            // fault had to be fixed twice.
            //
            // Instantiator is what does what the Repeater cannot: it creates
            // non-Item objects and hands each one back, and insertMenu puts it
            // in. Adding a family is now a row in UiLayouts.cpp and nothing
            // here.
            id: layoutMenu
            Instantiator {
                model: root.app.uiLayoutGroupList
                onObjectAdded: (index, object) => layoutMenu.insertMenu(index, object)
                onObjectRemoved: (index, object) => layoutMenu.removeMenu(object)
                delegate: Menu {
                    id: familyMenu
                    required property int index
                    required property var modelData
                    title: familyMenu.modelData.name
                    Repeater {
                        model: root.layoutsIn(familyMenu.index)
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.icon + "   " + modelData.name
                            checkable: true
                            checked: root.app.uiLayout === modelData.index
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.description
                                          + "\n(after " + modelData.from + ")"
                            onTriggered: root.app.uiLayout = modelData.index
                        }
                    }
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

        // "Colour-blind mode", not "Graph colours".
        //
        // It was reported as not doing anything, and the name was half the
        // reason: "Graph colours" reads as a palette picker, so a person who
        // opens it and sees the figure barely change concludes it is broken
        // rather than that their chosen map already passes. The name now says
        // what the setting is FOR, and the entries under it name the
        // deficiency rather than a colour scheme.
        Menu {
            title: "Colour-blind mode"
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
            //
            // THE NAMES HERE HAVE TO BE THE MODE STRINGS. This menu offered
            // "Rust / WGPU" while every piece of code that reads the setting
            // compares against "Native WGPU" - so the entry never showed as
            // checked, the picker in the top bar snapped back to Qt 2-D, and
            // applyMapping fell past its `rendererMode_=="Native WGPU"` branch
            // and pushed nothing into the viewport it had just switched to. The
            // renderer half-worked from the menu and worked from the top bar,
            // which is a difference nobody would think to look for.
            Repeater {
                model: [
                    { name: "Qt 2-D",
                      why: "All 434 catalogue engines, and the only renderer that exports true vector PDF and SVG. Start here; it is the default and it is what the catalogue is for." },
                    { name: "Native WGPU",
                      why: "A GPU point cloud for scatter and volume data too large for the 2-D painter - millions of points. It draws straight onto a native surface, so it has no catalogue engines and cannot export vectors." },
                    { name: "VTK / PBR",
                      why: "Lit, rotatable 3-D with physically based materials, for showing a surface rather than measuring one. Needs the OpenGL scene graph, which is chosen at startup - so it takes effect after a restart." }
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
            MenuSeparator {}
            // The one thing that finishes a switch to VTK.
            //
            // The scene graph's graphics API is chosen before the first window
            // exists and cannot be changed afterwards, so a running session
            // cannot switch to the renderer that needs OpenGL. Rather than
            // leave "restart GraphVis" as an instruction, this does it - and
            // carries the figure across, so the restart costs nothing but the
            // wait.
            MenuItem {
                text: "Restart now to finish switching"
                enabled: root.app.rendererNeedsRestart()
                ToolTip.visible: hovered
                ToolTip.text: enabled
                    ? "Reopens GraphVis on the OpenGL scene graph and puts this figure back"
                    : "Only needed after choosing VTK / PBR, which cannot start mid-session"
                onTriggered: root.app.restartApplication(
                    root.canvas ? root.canvas.figureState() : ({}))
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

    // A Help menu, because the application had none.
    //
    // The renderer is the setting people ask about: three names, no obvious
    // default, and two of them behave differently enough that picking wrong
    // looks like a fault. A tooltip can say one sentence; choosing between
    // three things needs them side by side.
    // The open figures. The same list the strip above the canvas shows, from
    // the same place - so switching figure from the menu and from a tab cannot
    // come to mean different things.
    Menu {
        title: "F&igures"
        Repeater {
            model: root.figureTitles
            MenuItem {
                required property int index
                required property string modelData
                text: (index + 1) + "  " + modelData
                checkable: true
                checked: index === root.figureIndex
                onTriggered: root.figureSelected(index)
            }
        }
        MenuSeparator {}
        Action {
            text: "Add a figure"
            onTriggered: root.figureAddRequested()
        }
        // The strip toggle has moved to View, beside the toolbar, the controls
        // panel and the workspace bar - the other three things that are either
        // on screen or not. This menu is for the figures themselves.
    }

    // The Help menu is a way into the whole manual, not a shortlist.
    //
    // It used to be three links, and three links is a menu that answers three
    // questions and implies there are only three. The manual is eighty-six
    // topics in fourteen sections covering every panel, control and menu in
    // the program; the way to say so is to put the sections on the menu and
    // let the reader see how much is there.
    //
    // `openHelp` rather than two statements at every call site: setting one of
    // the two start properties and leaving the other at whatever the last
    // caller set it to is a bug that shows up as the wrong article opening,
    // and it is invisible in review because each line looks right on its own.
    function openHelp(topicId, sectionId) {
        helpBrowser.startAt = topicId || ""
        helpBrowser.startSection = sectionId || ""
        helpBrowser.open()
    }

    Menu {
        title: "&Help"

        Action {
            text: "Help contents…"
            onTriggered: root.openHelp("", "")
        }

        MenuSeparator {}

        // Every section of the manual, read from the manual itself. A section
        // added to config/help_written.json appears here with no edit - the
        // menu cannot fall behind the document it opens.
        Menu {
            title: "Browse the manual"
            Repeater {
                model: root.app ? root.app.helpSections : []
                delegate: MenuItem {
                    id: helpSectionItem
                    required property var modelData
                    text: helpSectionItem.modelData.title
                    onTriggered: root.openHelp("", helpSectionItem.modelData.id)
                    ToolTip.text: helpSectionItem.modelData.blurb
                    ToolTip.visible: hovered && helpSectionItem.modelData.blurb !== ""
                    ToolTip.delay: 400
                }
            }
        }

        MenuSeparator {}

        // The questions people actually arrive with, in the words they arrive
        // in. Every id here is checked against the manual by the passive audit
        // (help_topic_missing), because a menu item pointing at a topic that
        // has been renamed opens the browser at the wrong article and says
        // nothing - the silent name mismatch this project keeps meeting.
        Menu {
            title: "How do I…"
            Action {
                text: "Make my first figure"
                onTriggered: root.openHelp("start-first", "")
            }
            Action {
                text: "Import a file GraphVis does not recognise"
                onTriggered: root.openHelp("data-import", "")
            }
            Action {
                text: "Choose the right graph"
                onTriggered: root.openHelp("gr-library", "")
            }
            Action {
                text: "Put several series on one figure"
                onTriggered: root.openHelp("data-series", "")
            }
            Action {
                text: "Export a figure for a paper"
                onTriggered: root.openHelp("pub-export", "")
            }
            Action {
                text: "Check a figure for colour vision"
                onTriggered: root.openHelp("col-vision", "")
            }
            Action {
                text: "Read numbers off a published figure"
                onTriggered: root.openHelp("lit-derender", "")
            }
            Action {
                text: "Draw a choropleth from region outlines"
                onTriggered: root.openHelp("data-boundaries", "")
            }
        }

        Menu {
            title: "Why is…"
            Action {
                text: "My figure blank?"
                onTriggered: root.openHelp("tb-nothing", "")
            }
            Action {
                text: "Everything squashed into one corner?"
                onTriggered: root.openHelp("tb-onepixel", "")
            }
            Action {
                text: "My column not in the list?"
                onTriggered: root.openHelp("tb-nocolumn", "")
            }
            Action {
                text: "The export different from the screen?"
                onTriggered: root.openHelp("tb-export", "")
            }
            Action {
                text: "This graph marked unverified?"
                onTriggered: root.openHelp("tb-unverified", "")
            }
            Action {
                text: "It slow?"
                onTriggered: root.openHelp("tb-slow", "")
            }
            Action {
                text: "A control I was using missing?"
                onTriggered: root.openHelp("tb-missing", "")
            }
        }

        MenuSeparator {}

        Action {
            text: "Which renderer should I use?"
            onTriggered: root.openHelp("set-renderers", "")
        }
        Action {
            text: "Estimating a field: what it is doing"
            onTriggered: root.openHelp("fi-what", "")
        }
        Action {
            text: "All %1 graph categories".arg(
                      root.app && root.app.graphCategories
                      ? root.app.graphCategories.length : 0)
            onTriggered: root.openHelp("ref-categories", "")
        }
    }

    // The renderer popup used to be written out by hand here. Its text is now
    // one topic among the rest, so it can be searched and linked to instead of
    // living only behind a menu item somebody has to already know about.
    HelpBrowser {
        id: helpBrowser
        app: root.app
    }

}
