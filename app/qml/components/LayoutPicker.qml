// The window's shape, chosen from the toolbar.
//
// Twenty-one shapes in five families, each taken from an application that is
// known to work rather than invented here - Origin, Tableau, Prism, VS Code,
// Figma, Blender, DaVinci Resolve, RStudio, Obsidian. A list of twenty-one
// names in a combo box would be worse than the six it replaces, so the picker
// is a menu grouped by family, and every entry says what it is for and where
// its shape comes from. An interface someone has already learned somewhere else
// is one they do not have to learn again, and saying WHERE is half of that.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ToolButton {
    id: root
    required property var app

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

    readonly property var current: {
        var all = root.app.uiLayoutList
        for (var i = 0; i < all.length; ++i)
            if (all[i].index === root.app.uiLayout) return all[i]
        return all.length > 0 ? all[0] : null
    }

    text: root.current ? (root.current.icon + "  " + root.current.name) : "Layout"
    ToolTip.visible: root.hovered
    ToolTip.text: root.current
                  ? (root.current.groupName + " · " + root.current.name
                     + " — " + root.current.description
                     + "\n(after " + root.current.from + ")")
                  : "Choose the window's shape"
    onClicked: menu.open()

    // One submenu per family, built from the group table.
    //
    // These were written out by hand, one Menu per family, because a Repeater
    // CANNOT create a Menu - its delegate has to be an Item and Menu is not
    // one, and doing it that way produced five submenus that were built and
    // never added to anything, so the picker opened onto an empty panel.
    //
    // Hand-writing them has its own failure, and it happened: a sixth family
    // was added to the table and there was no sixth Menu here, so five layouts
    // existed, were selectable from a script, were persisted, and could not be
    // reached from the interface at all. The picker showed twenty-one of
    // twenty-six shapes and looked complete.
    //
    // An Instantiator is the thing that does what the Repeater cannot: it
    // creates non-Item objects and hands each one back, and insertMenu puts it
    // in. Adding a family is now a row in the table and nothing here.
    Menu {
        id: menu
        y: root.height

        Instantiator {
            model: root.app.uiLayoutGroupList
            onObjectAdded: (index, object) => menu.insertMenu(index, object)
            onObjectRemoved: (index, object) => menu.removeMenu(object)
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
}
