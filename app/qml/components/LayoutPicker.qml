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

    Menu {
        id: menu
        y: root.height

        Repeater {
            model: root.app.uiLayoutGroupList
            delegate: Menu {
                id: groupMenu
                required property var modelData
                title: groupMenu.modelData.name

                Repeater {
                    // Only this family's layouts. The list is small enough that
                    // filtering it here beats a second property per group on
                    // the controller.
                    model: {
                        var out = []
                        var all = root.app.uiLayoutList
                        for (var i = 0; i < all.length; ++i)
                            if (all[i].group === groupMenu.modelData.index) out.push(all[i])
                        return out
                    }
                    delegate: MenuItem {
                        id: entry
                        required property var modelData
                        text: entry.modelData.icon + "   " + entry.modelData.name
                        checkable: true
                        checked: root.app.uiLayout === entry.modelData.index
                        ToolTip.visible: entry.hovered
                        ToolTip.text: entry.modelData.description
                                      + "\n(after " + entry.modelData.from + ")"
                        onTriggered: root.app.uiLayout = entry.modelData.index
                    }
                }
            }
        }
    }
}
