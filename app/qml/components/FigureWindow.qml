// A figure in a window of its own.
//
// THE FIGURE MOVES. It is not copied, mirrored or re-created here: the live
// FigureCanvas is REPARENTED out of the workspace and into this window's
// content item, and reparented back when the window closes. That is the whole
// design, and the reason this took a second pass rather than shipping with the
// tab bar.
//
// A copy would have been far easier and would have been a lie. Two canvases
// showing the same engine and mapping look identical for about four seconds -
// until you change a column, and the sidebar edits the one in the workspace
// while you are looking at the one in the window. A figure you cannot edit that
// is presented as your figure is worse than no floating at all, which is why
// the tab bar shipped with the menu item removed rather than stubbed.
//
// Reparenting works because `anchors.fill: parent` is a BINDING on `parent`,
// not a one-time assignment: change the parent and the anchor follows it. So
// the canvas fills the workspace's figure box while it lives there and fills
// this window while it lives here, with nothing to keep in step by hand.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import GraphVis

Window {
    id: root

    // The live canvas item. Handed in, never created.
    required property var figure
    required property string figureTitle
    // Where the figure came from, RECORDED AT THE MOMENT IT LEAVES.
    //
    // This was `required property var homeParent`, bound in the workspace to
    // `figureItem(index).parent`. A binding on `.parent` cannot remember a
    // parent: the instant takeFigure() reparents the canvas, the binding
    // re-evaluates to the window's own content item, so "put it back where it
    // came from" meant "put it back where it already is". The figure could
    // never return.
    //
    // A plain property, assigned once, in takeFigure. Not a binding, because
    // what is wanted here is a snapshot and a binding is the opposite of one.
    property var homeParent: null

    signal docked()

    title: figureTitle + " — GraphVis"
    width: 900
    height: 640
    minimumWidth: 320
    minimumHeight: 240
    color: Theme.background

    // Taking the figure, and giving it back.
    //
    // Both directions are guarded on the figure still existing: a figure can be
    // closed from the tab bar while its window is open, and reparenting a
    // destroyed item is a crash rather than a warning.
    function takeFigure() {
        if (!root.figure) return
        // Already here: taking it again would record THIS window as home.
        if (root.figure.parent === root.contentItem) return
        root.homeParent = root.figure.parent
        root.figure.parent = root.contentItem
    }
    function returnFigure() {
        if (!root.figure || !root.homeParent) return
        root.figure.parent = root.homeParent
        root.homeParent = null
    }

    // NO Component.onCompleted HERE.
    //
    // It used to be `Component.onCompleted: root.takeFigure()`, and the window
    // is created by an Instantiator for EVERY figure, floating or not - so on
    // startup the one and only canvas was immediately reparented into a hidden
    // window and the workspace drew nothing at all. An empty canvas area, on
    // every engine, with the mapping panel correctly reporting that it had all
    // its columns.
    //
    // Visibility is the only thing that moves the figure now, and visibility
    // follows `floating` in the model. A window that is never shown never
    // touches it.
    onVisibleChanged: {
        if (root.visible) root.takeFigure()
        else root.returnFigure()
    }
    onClosing: {
        root.returnFigure()
        root.docked()
    }

    // A window with no figure in it yet, or whose figure has been closed, says
    // so rather than showing an empty frame that looks like a broken render -
    // the same reasoning as the mapping panel's "reads 3 columns" note.
    Label {
        anchors.centerIn: parent
        visible: !root.figure
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeSmall
        text: "This figure has been closed."
    }
}
