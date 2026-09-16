// A panel that can be torn out into its own resizable window and docked back.
//
// GraphVis 17 let any dock or toolbar be popped out; that flexibility was lost
// in the rewrite. This restores it generically: wrap content in a DockPanel and
// it gains a compact header with a pop-out control. The content item is
// reparented rather than recreated, so state and bindings survive the trip out
// and back.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import GraphVis

Item {
    id: root

    property string title: ""
    // Some content cannot travel: a viewport that embeds a native window is
    // reparented across QWindows by tearing it out, which is not something to
    // do behind the user's back. Such a panel keeps its header and loses only
    // the pop-out control.
    property bool floatable: true
    // Zen hides the chrome. The header is the panel's title, its grip and its
    // pop-out button, and in a layout whose whole point is that there is
    // nothing but the figure, a 26 px strip saying "Canvas" is the one thing
    // spoiling it.
    property bool headerVisible: true
    property bool floating: false
    onFloatableChanged: if (!root.floatable) root.floating = false
    // FOLDED TO ITS OWN TITLE STRIP.
    //
    // GvGroupBox has folded since it was written, because a sidebar is four
    // hundred pixels wide and the group you want is usually below the fold.
    // The panels those groups sit INSIDE could not fold at all - so the way to
    // get "Dataset and colours" out of the way of the Graph Library was to drag
    // a splitter shut, which does not stay shut when the window is resized, or
    // to tear the panel into a window, which is a bigger commitment than
    // "not now".
    //
    // Same rule as the group boxes, one level up: click the caret and the panel
    // is its header until it is clicked again.
    property bool collapsible: true
    property bool collapsed: false
    // Restored rather than remembered as a number: a panel that is folded while
    // floating would otherwise dock back at 26 pixels tall.
    onFloatingChanged: if (root.floating) root.collapsed = false
    readonly property int headerHeight: !root.headerVisible ? 0
                                      : (headerActionRow.children.length > 0 ? 34 : 26)
    // Smaller than the window it came out of, and small enough to sit beside
    // it rather than over it. 820x620 was most of a laptop screen for a panel
    // holding six controls, so a torn-out dock buried the figure it was there
    // to change.
    property int floatingWidth: 420
    property int floatingHeight: 480
    // Where the floating window opens. Set by a tear-off drag so the window
    // appears under the pointer instead of wherever the compositor puts it;
    // -1 means "let the window manager decide".
    property int floatingX: -1
    property int floatingY: -1

    function tearOutAtPointer() {
        var here = root.mapToGlobal(0, 0)
        root.floatingX = Math.round(here.x)
        root.floatingY = Math.round(here.y)
        root.floating = true
    }
    default property alias content: contentHost.data
    // Controls that belong to the panel rather than to what is inside it, laid
    // out in the header to the left of the pop-out button.
    //
    // Before this, the canvas's own controls had nowhere to go but a floating
    // row along the bottom of the figure, where they sat on top of the plot,
    // covered whatever was drawn near that corner, and grew sideways over each
    // other as more of them appeared. A panel header is where a panel's buttons
    // belong, and it is the one strip of the canvas that is not the figure.
    property alias headerActions: headerActionRow.data

    implicitWidth: 240

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Compact chrome, close to GraphVis 17's 22-24 px panel headers.
        Rectangle {
            Layout.fillWidth: true
            // Tall enough for a combo box when the header carries controls, and
            // back to GraphVis 17's 26 px strip when it does not.
            visible: root.headerVisible
            implicitHeight: root.headerHeight
            color: Theme.surfaceAlt
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 6

                // A grip that grips. This was a label of the braille-dots
                // character and nothing else - the one thing in the header that
                // LOOKS draggable was the only thing that could not be dragged,
                // so people pulled at it, nothing moved, and the conclusion was
                // that the panel is fixed. Dragging it now tears the panel out
                // and puts the new window under the pointer.
                Item {
                    implicitWidth: 14
                    implicitHeight: 18
                    Label {
                        anchors.centerIn: parent
                        text: "⠿"
                        color: gripHover.hovered ? Theme.text : Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    HoverHandler {
                        id: gripHover
                        cursorShape: Qt.OpenHandCursor
                    }
                    ToolTip.visible: gripHover.hovered
                    ToolTip.text: root.floating
                                  ? "Drag the window by its own title bar"
                                  : "Drag out into a window of its own"
                    DragHandler {
                        id: tearOff
                        target: null
                        enabled: root.floatable && !root.floating
                        // Far enough that a wobble while clicking the header
                        // does not throw the panel into a window.
                        onActiveChanged: if (!active) tearOff.armed = false
                        property bool armed: false
                        onTranslationChanged: {
                            if (tearOff.armed || !tearOff.active) return
                            if (Math.abs(tearOff.translation.x) + Math.abs(tearOff.translation.y) < 24)
                                return
                            tearOff.armed = true
                            root.tearOutAtPointer()
                        }
                    }
                }
                Label {
                    text: root.title
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    elide: Text.ElideRight
                    // Gives way to the controls rather than pushing them off the
                    // end: a button that has been shoved past the edge cannot be
                    // clicked, and a title that has been elided can still be
                    // read from what is left of it.
                    // Narrower still once there are controls to make room for.
                    Layout.maximumWidth: Math.max(90, root.width
                        * (headerActionRow.children.length > 0 ? 0.22 : 0.34))
                }

                // The actions, in something that can be scrolled when they do
                // not all fit.
                //
                // They used to sit in a bare row after a stretching spacer, so
                // on a narrow canvas the ones at the far end were simply not on
                // screen - no scroll bar, no overflow, no indication that
                // anything was missing. Export PDF and Annotate disappeared and
                // the header looked complete without them.
                //
                // Scrolled rather than folded into an overflow menu, because
                // headerActions takes arbitrary items - a status pill, three
                // selectors, some buttons - and arbitrary items cannot be moved
                // into a Menu. The arrows below say when there is more.
                Flickable {
                    id: actionScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: headerActionRow.implicitWidth
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick
                    readonly property bool overflowing: contentWidth > width + 1

                    RowLayout {
                        id: headerActionRow
                        spacing: 6
                        height: actionScroll.height
                        // Hard right while everything fits, which is where these
                        // have always been; pinned left once they do not, so
                        // scrolling starts at the beginning of the row.
                        x: Math.max(0, actionScroll.width - implicitWidth)
                    }
                }
                // Nothing may be hidden silently. These appear only when there
                // is something past the edge, and point at where it is.
                Label {
                    visible: actionScroll.overflowing && actionScroll.contentX > 1
                    text: "‹"
                    color: Theme.textSecondary
                }
                Label {
                    visible: actionScroll.overflowing
                             && actionScroll.contentX < actionScroll.contentWidth
                                                        - actionScroll.width - 1
                    text: "›"
                    color: Theme.textSecondary
                }
                ToolButton {
                    id: foldButton
                    // Not while floating: a window folded to a title bar is a
                    // window you cannot get anything back out of.
                    visible: root.collapsible && !root.floating
                    implicitWidth: 22
                    implicitHeight: 22
                    ToolTip.visible: foldButton.hovered
                    ToolTip.text: root.collapsed ? "Unfold " + root.title
                                                 : "Fold " + root.title + " away"
                    onClicked: root.collapsed = !root.collapsed
                    contentItem: Label {
                        // Points the way it will go, which is the only thing
                        // that tells a caret from a decoration.
                        text: root.collapsed ? "▸" : "▾"
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                ToolButton {
                    visible: root.floatable
                    implicitWidth: 26
                    implicitHeight: 22
                    ToolTip.visible: hovered
                    ToolTip.text: root.floating ? "Dock back into the main window"
                                                : "Tear out into a floating window"
                    onClicked: root.floating = !root.floating
                    contentItem: Label {
                        text: root.floating ? "▣" : "⧉"
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // The slot the content sits in while docked.
        Item {
            id: dockedSlot
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Hidden rather than merely zero-height: a clipped item is still
            // under the pointer and goes on taking clicks and wheel events from
            // whatever the person can actually see - the same reason
            // GvGroupBox hides its content item rather than trusting clip.
            visible: !root.collapsed

            Label {
                anchors.centerIn: parent
                visible: root.floating
                text: root.title + " is in a floating window"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }

    Window {
        id: floater
        width: root.floatingWidth
        height: root.floatingHeight
        // Small enough to be useful. The old floor of 320x240 is fine for the
        // library; a colour picker torn out should be allowed to be a strip.
        minimumWidth: 240
        minimumHeight: 140
        visible: root.floating
        title: "GraphVis · " + root.title
        color: Theme.background
        onClosing: root.floating = false
        // Only on the way out: binding x and y to the properties would fight
        // the person the moment they moved the window.
        onVisibleChanged: {
            if (!visible || root.floatingX < 0) return
            floater.x = root.floatingX
            floater.y = root.floatingY
        }

        Item { id: floatSlot; anchors.fill: parent }
    }

    // FOLDING HAS TO REACH THE LAYOUT, or the panel keeps its share of the
    // split and folds into empty space.
    //
    // Through Binding with `when`, not by assigning the attached properties
    // directly: every use site already binds its own SplitView.preferredHeight
    // and minimumHeight, and a plain assignment here would break those
    // bindings permanently - the panel would never get its size back on
    // unfolding. RestoreBindingOrValue puts the use site's binding back the
    // moment this one stops applying.
    //
    // Only in a VERTICAL split. Folding is a vertical idea - the header runs
    // across the top - so in a horizontal split the panel gives up its content
    // and keeps its column, which is the honest result rather than a title
    // squashed into a 26 px column.
    readonly property bool foldingTheSplit: root.collapsed && !root.floating
        && root.SplitView.view !== null
        && root.SplitView.view.orientation === Qt.Vertical
    Binding {
        target: root
        property: "SplitView.maximumHeight"
        value: root.headerHeight
        when: root.foldingTheSplit
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: root
        property: "SplitView.minimumHeight"
        value: root.headerHeight
        when: root.foldingTheSplit
        restoreMode: Binding.RestoreBindingOrValue
    }
    // The same for a plain ColumnLayout, where the panel is sized by Layout
    // attached properties instead.
    Binding {
        target: root
        property: "Layout.maximumHeight"
        value: root.headerHeight
        when: root.collapsed && !root.floating && root.SplitView.view === null
        restoreMode: Binding.RestoreBindingOrValue
    }
    implicitHeight: root.collapsed && !root.floating ? root.headerHeight : 240

    // The content lives in exactly one place at a time. anchors.fill re-resolves
    // against whichever slot is the current parent, so no manual geometry.
    Item {
        id: contentHost
        parent: root.floating ? floatSlot : dockedSlot
        anchors.fill: parent
        // A panel may not paint outside itself.
        //
        // A column layout whose children's MINIMUM heights add up to more than
        // the height it is given does not shrink them - it lays them out past
        // its own bottom edge, and with nothing clipping, that overflow paints
        // straight over whatever is below. That is the reported "overlapping
        // text": in the Activity rail layout the Graph Library needed 250 px,
        // the sidebar had 182 to give it, and the last 68 px of the library
        // were drawn on top of the Datasets strip along the bottom of the
        // window. The panels underneath were laid out correctly the whole time
        // - the geometry was right and the painting was wrong.
        //
        // Clipping here is the containment; the panels that could overflow have
        // also had their floors lowered so they shrink instead of being cut.
        // Popups, menus and tooltips are unaffected: those render in the
        // window's overlay, not in this item.
        clip: true
    }
}
