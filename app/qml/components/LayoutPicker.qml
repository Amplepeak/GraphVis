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

    // The five families are written out rather than repeated over, because a
    // Repeater CANNOT create a Menu: its delegate has to be an Item and Menu is
    // not one. Doing it the other way produced five submenus that were built
    // and never added to anything, so the picker opened onto an empty panel.
    Menu {
        id: menu
        y: root.height

        Menu {
            title: root.groupName(0)
            Repeater {
                model: root.layoutsIn(0)
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
        Menu {
            title: root.groupName(1)
            Repeater {
                model: root.layoutsIn(1)
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
        Menu {
            title: root.groupName(2)
            Repeater {
                model: root.layoutsIn(2)
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
        Menu {
            title: root.groupName(3)
            Repeater {
                model: root.layoutsIn(3)
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
        Menu {
            title: root.groupName(4)
            Repeater {
                model: root.layoutsIn(4)
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
