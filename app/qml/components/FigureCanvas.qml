// ONE FIGURE, with everything that makes it follow the person's settings.
//
// Lifted verbatim out of VisualizeWorkspace, where it was ninety lines of
// bindings inline around a single `PlotCanvas { id: plot }`. That was fine
// while there could only ever be one figure; a tab bar means there can be
// several, and several copies of ninety lines of bindings is how two figures
// end up quietly obeying different settings.
//
// Nothing here is new. Every binding is the one that was in the workspace, so
// a figure created from this component behaves exactly as the single canvas
// did - which is the point of extracting it before adding anything.
//
// What is NOT here, because it belongs to the HOST rather than to a figure:
// the anchors, and the restore of a figure state left behind by a scene-graph
// restart. That restore applies to ONE figure - the one that was open - so it
// stays with whichever figure the workspace decides is that one.
import QtQuick
import GraphVis

PlotCanvas {
    id: root
    required property var app

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
    legendLabels: root.app.plotLegendLabels
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
