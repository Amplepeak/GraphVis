// A GroupBox whose title does not collide with its own frame, and which folds
// away when it is not the group you are working in.
//
// The Basic style draws the label directly on the frame line with no
// background, which on a dark palette reads as struck-through text. This
// version insets the frame, gives the label the panel colour behind it, and
// takes its colours from Theme.
//
// COLLAPSING. Every panel in this application is a column of these, and a
// sidebar is four hundred pixels wide: the mapping panel alone carries variable
// mapping, axis scale, the view angle, engine settings, the point cloud, the
// clarity policy and the PBR material, and the one you want is usually below
// the fold. Folding is per group and per session - click the title, and the
// group is a title strip until it is clicked again.
//
// Clicking the TITLE rather than a separate button, because the title is the
// thing already at the top of every group, and a caret beside it says which way
// it will go. Groups start open: a panel that opens folded hides its own
// contents from someone who has never seen them.
import QtQuick
import QtQuick.Controls
import GraphVis

GroupBox {
    id: control

    property color panelColor: Theme.surface
    // Set false on a group that is only ever one row, where folding costs more
    // than it saves.
    property bool collapsible: true
    property bool collapsed: false

    // Folded, the group is its own title strip. implicitContentHeight is the
    // Control's own measurement of what is inside, so the open case is the
    // default formula written out rather than a second opinion about it.
    implicitHeight: (control.collapsible && control.collapsed)
                    ? control.topPadding + 4
                    : control.implicitContentHeight + control.topPadding
                      + control.bottomPadding
    clip: true

    // Hidden, not merely clipped. A clipped item is still under the pointer -
    // it would go on taking clicks and wheel events from whatever the person
    // can actually see, which is the panel below it.
    Binding {
        target: control.contentItem
        property: "visible"
        value: !(control.collapsible && control.collapsed)
        when: control.contentItem !== null
    }

    label: Item {
        id: labelBox
        implicitHeight: caption.implicitHeight + 4
        // The title has to fit the frame it labels. It was drawn at its full
        // natural width from a fixed left inset, so in a narrow panel - a
        // three-pane layout on a small screen puts these in 184 px - a title
        // like "Point cloud and material" simply ran out past the frame and
        // over whatever was beside it. Elided, the frame is still labelled and
        // the tooltip on the title says the rest.
        readonly property real captionWidth:
            Math.max(0, Math.min(caption.implicitWidth,
                                 control.width - caret.width - 34))
        Rectangle {
            x: 10
            y: (parent.height - caption.implicitHeight) / 2
            width: caret.width + labelBox.captionWidth + 12
            height: caption.implicitHeight
            color: control.panelColor
        }
        // Which way this group will go. Nothing else in the header says the
        // title can be clicked at all.
        Label {
            id: caret
            visible: control.collapsible
            x: 15
            y: (parent.height - implicitHeight) / 2
            width: visible ? implicitWidth + 4 : 0
            text: control.collapsed ? "▸" : "▾"
            color: titleHover.hovered ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
        }
        Label {
            id: caption
            x: 15 + caret.width
            y: (parent.height - implicitHeight) / 2
            width: labelBox.captionWidth
            elide: Text.ElideRight
            text: control.title
            color: titleHover.hovered && control.collapsible
                   ? Theme.text : Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            font.bold: true
        }
        // Only over the caret and the title, so the rest of the frame's top
        // edge is not a hidden control.
        Item {
            x: 12
            y: 0
            width: caret.width + labelBox.captionWidth + 8
            height: parent.height
            HoverHandler {
                id: titleHover
                enabled: control.collapsible
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                enabled: control.collapsible
                onTapped: control.collapsed = !control.collapsed
            }
            // The full title too, for the panel narrow enough to have elided it.
            ToolTip.visible: titleHover.hovered
            ToolTip.text: control.collapsible
                          ? control.title + " — "
                            + (control.collapsed ? "show this group"
                                                 : "fold this group away")
                          : control.title
        }
    }

    background: Rectangle {
        y: control.topPadding - control.bottomPadding
        width: parent.width
        height: parent.height - control.topPadding + control.bottomPadding
        color: "transparent"
        border.color: Theme.border
        radius: Theme.radius
    }

    topPadding: 18
    bottomPadding: 10
    leftPadding: 10
    rightPadding: 10
}
