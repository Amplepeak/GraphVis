// What happens when the full-resolution render lands.
//
// The on-screen figure is always a decimated preview, drawn immediately so the
// interface cannot hang; the same picture is then rendered from every row on a
// worker thread. The question this answers is what to do at the moment that
// second picture is ready.
//
// It used to be: put up a notice and wait to be asked, every time, for every
// render however brief. So the better picture existed and was not being shown
// - on a 200,000-point dataset the preview draws a twentieth of the data and
// sat there until someone clicked "Show it". Showing it is now the default.
//
// The notice still exists for the other two settings, and even on Automatic the
// swap is refused while a drag is in progress: a picture that changes under a
// finger is not a better picture.
import QtQuick
import QtQuick.Controls
import GraphVis

ComboBox {
    id: root
    required property var app
    required property var canvas

    // Only where there IS a second render to talk about. On a figure small
    // enough that the preview draws every point, this setting decides nothing.
    visible: !root.canvas.previewIsExact && root.canvas.pointCount > 0
    width: root.visible ? root.implicitWidth : 0
    implicitWidth: 250

    model: root.app.fullRenderPolicyNames
    currentIndex: root.app.fullRenderPolicy
    onActivated: root.app.fullRenderPolicy = root.currentIndex

    ToolTip.visible: root.hovered
    ToolTip.text: "The figure on screen is a preview drawn from "
                + root.canvas.previewPointCount + " of " + root.canvas.fullPointCount
                + " points. This chooses what happens when the full-resolution "
                + "render finishes: show it straight away, ask first, or ask only "
                + "when it took longer than "
                + root.app.fullRenderAskAfterSeconds + " seconds."
}
