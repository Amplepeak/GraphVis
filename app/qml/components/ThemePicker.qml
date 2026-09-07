// Theme picker.
//
// A hundred themes will not fit in a ComboBox. This is a button showing the
// current theme's swatch and name, opening a list grouped by
// section, where each row previews the palette it will apply - surface,
// secondary surface, border, text and accent - so the choice is made by
// looking rather than by reading a name.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Item {
    id: root
    required property var app
    property bool compact: false

    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight

    // Flattened rows: a group header followed by its themes when that group is
    // open. Collapsed by default - a hundred and eight entries listed at once is
    // one long scroll with headings in it, not a set of groups. Only the group
    // holding the current theme starts open.
    //
    // There is no search box. Four groups of at most fifty-six, one open at a
    // time, is a list you scan rather than one you query, and a search field
    // above it was one more thing to read past.
    //
    // Every group starts closed, including the one holding the current theme.
    // Opening one of them for you is a guess about what you came to do, and it
    // made the list start half-unrolled instead of showing the four choices.
    property string openGroup: ""

    function toggleGroup(name) {
        root.openGroup = (root.openGroup === name) ? "" : name
    }

    readonly property var rows: {
        var out = []
        for (var g = 0; g < Theme.groups.length; ++g) {
            var groupName = Theme.groups[g]
            var members = Theme.indicesInGroup(groupName)
            if (members.length === 0) continue
            var expanded = (root.openGroup === groupName)
            out.push({ header: true, label: groupName, count: members.length,
                       group: groupName, expanded: expanded, index: -1 })
            if (!expanded) continue
            for (var m = 0; m < members.length; ++m)
                out.push({ header: false, label: Theme.themeNameAt(members[m]),
                           count: 0, group: groupName, expanded: true, index: members[m] })
        }
        return out
    }

    Button {
        id: button
        anchors.fill: parent
        padding: 6
        onClicked: popup.opened ? popup.close() : popup.open()
        ToolTip.visible: hovered
        ToolTip.text: Theme.cvd !== ""
                      ? "Theme: " + Theme.name + " - designed for " + Theme.cvdLabelCurrent
                      : "Theme - " + Theme.themeCount + " available in " + Theme.groups.length + " groups"

        contentItem: RowLayout {
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 30; Layout.preferredHeight: 16
                radius: 3
                color: Theme.surfaceAlt
                border.color: Theme.borderStrong
                Rectangle {
                    anchors.right: parent.right; anchors.rightMargin: 3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10; height: 10; radius: 5
                    color: Theme.accent
                }
                Rectangle {
                    anchors.left: parent.left; anchors.leftMargin: 3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12; height: 3; radius: 1.5
                    color: Theme.text
                }
            }
            Label {
                visible: !root.compact
                text: Theme.name
                color: Theme.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // A colour vision theme announces itself on the button, not only
            // inside the picker.
            Rectangle {
                visible: Theme.cvd !== "" && !root.compact
                Layout.preferredWidth: cvdTag.implicitWidth + 8
                Layout.preferredHeight: 15
                radius: 3
                color: Theme.accent
                Label {
                    id: cvdTag
                    anchors.centerIn: parent
                    text: Theme.cvd.substring(0, 6).toUpperCase()
                    color: Theme.onAccent
                    font.pixelSize: 9
                    font.bold: true
                }
            }
            Label { text: "▾"; color: Theme.textMuted }
        }
    }

    Popup {
        id: popup
        // Parented to the picker itself, not to Overlay.overlay: the overlay is
        // null until the item is in a window, and binding geometry to it threw
        // on every load. `margins` is what keeps the popup inside the window.
        x: root.width - width
        y: root.height + 6
        margins: 8
        width: 330
        height: Math.min(520, Math.max(240, (root.Window.height || 800) - 150))
        padding: 8
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.borderStrong
            radius: 8
        }

        onOpened: root.openGroup = ""

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.rows
                spacing: 2
                currentIndex: -1
                ScrollBar.vertical: ScrollBar {}

                // One delegate for both row kinds. A Loader with two Components
                // would need parent-chain lookups to reach the model row, which
                // breaks silently the moment the item tree changes shape.
                delegate: Rectangle {
                    id: entry
                    required property var modelData
                    width: ListView.view.width
                    height: modelData.header ? 32 : 34
                    radius: 5
                    readonly property int themeIndex: modelData.index
                    readonly property bool active: !modelData.header && themeIndex === Theme.currentIndex
                    color: entry.active
                           ? Theme.surfaceAlt
                           : (hover.hovered
                              ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                              : "transparent")
                    border.color: entry.active ? Theme.accent : "transparent"

                    // Group header - a row you click, not a caption.
                    RowLayout {
                        visible: entry.modelData.header
                        anchors.fill: parent
                        anchors.leftMargin: 6; anchors.rightMargin: 6
                        spacing: 6
                        Label {
                            text: entry.modelData.expanded ? "▾" : "▸"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Label {
                            text: entry.modelData.label
                            color: Theme.text
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Label {
                            text: entry.modelData.count
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }

                    // Theme row, with a live preview of the palette it applies.
                    RowLayout {
                        visible: !entry.modelData.header
                        anchors.fill: parent
                        anchors.leftMargin: 6; anchors.rightMargin: 6
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 54; Layout.preferredHeight: 22
                            radius: 3
                            color: entry.modelData.header ? "transparent"
                                                          : Theme.themeSurfaceAt(entry.themeIndex)
                            border.color: Theme.borderStrong
                            Row {
                                anchors.centerIn: parent
                                spacing: 3
                                Rectangle {
                                    width: 16; height: 3; radius: 1.5
                                    color: entry.modelData.header ? "transparent"
                                                                  : Theme.themeTextAt(entry.themeIndex)
                                }
                                Rectangle {
                                    width: 10; height: 10; radius: 5
                                    color: entry.modelData.header ? "transparent"
                                                                  : Theme.themeAccentAt(entry.themeIndex)
                                }
                            }
                        }
                        Label {
                            text: entry.modelData.label
                            color: Theme.text
                            font.bold: entry.active
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        // Says which colour vision deficiency a theme is for,
                        // if any - the whole point of the group is that a user
                        // can tell without opening documentation.
                        Label {
                            visible: !entry.modelData.header
                                     && Theme.themeCvdAt(entry.themeIndex) !== ""
                            text: {
                                var k = Theme.themeCvdAt(entry.themeIndex)
                                if (k === "protanopia")    return "PROTAN"
                                if (k === "deuteranopia")  return "DEUTAN"
                                if (k === "tritanopia")    return "TRITAN"
                                if (k === "achromatopsia") return "MONO"
                                return ""
                            }
                            color: Theme.accent
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Label {
                            text: entry.modelData.header ? ""
                                  : (Theme.themeIsLightAt(entry.themeIndex) ? "\u2600" : "\u263e")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }

                    HoverHandler { id: hover }
                    TapHandler {
                        onTapped: {
                            if (entry.modelData.header) {
                                root.toggleGroup(entry.modelData.group)
                            } else {
                                Theme.currentIndex = entry.themeIndex
                                popup.close()
                            }
                        }
                    }
                }
            }

        }
    }
}
