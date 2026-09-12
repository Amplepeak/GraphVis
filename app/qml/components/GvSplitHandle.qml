// The divider between two panes, made big enough to hit.
//
// SplitView's default handle is one pixel of nothing. Every split in this
// application WAS resizable and nobody could tell: the only clue that the
// divider could be dragged was that the cursor changed, over a target one pixel
// wide - and the two splits that did have a visible handle had it because
// ControlSidebar declared one privately, so the main sidebar/figure divider and
// the Literature panes still had the invisible one. Reported as "I can't drag
// the bars to make the graph window bigger", which is exactly what it looks
// like from the outside.
//
// Eight pixels, a grip drawn on it, a colour that answers the pointer, and the
// resize cursor over the whole strip rather than over the two pixels of grip.
//
// It reads its own geometry to decide which way it lies: a SplitView stretches
// its handle across the cross axis, so a taller-than-wide handle is the upright
// bar between two side-by-side panes and the grip runs down it.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import GraphVis

Rectangle {
    id: body

    // SplitHandle is attached to the HANDLE item, not to its children: reading
    // SplitHandle.pressed from inside the Repeater below would silently attach
    // a second, unconnected instance to the dot and the dot would never change
    // colour. Read once here, use body.pressed everywhere else.
    readonly property bool pressed: SplitHandle.pressed
    readonly property bool hovered: SplitHandle.hovered
    readonly property bool upright: body.height >= body.width

    implicitWidth: 8
    implicitHeight: 8
    color: body.pressed ? Theme.accent
         : (body.hovered ? Theme.borderStrong : Theme.surfaceAlt)

    Grid {
        anchors.centerIn: parent
        columns: body.upright ? 1 : 3
        rows: body.upright ? 3 : 1
        spacing: 3
        Repeater {
            model: 3
            delegate: Rectangle {
                width: 2; height: 2; radius: 1
                color: body.pressed ? Theme.onAccent : Theme.textMuted
            }
        }
    }

    HoverHandler {
        cursorShape: body.upright ? Qt.SplitHCursor : Qt.SplitVCursor
    }
    ToolTip.visible: body.hovered
    ToolTip.text: body.upright ? "Drag sideways to give one side more room"
                               : "Drag up or down to give one side more room"
}
