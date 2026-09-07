// Add-ons.
//
// The heavy or rarely-wanted parts of GraphVis are a choice at install time and
// a choice afterwards. Nothing here is required to plot a CSV; each entry says
// what it gives you, what it costs to download, and what it costs to use, so
// the decision is an informed one rather than a guess at a checkbox.
//
// The list is read from tools/optional-components.json by the same script the
// installer uses, so a component added there appears here with no code change.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

PanelScroll {
    id: root
    spacing: 10

    required property var app

    // Selected for the next Install or Remove, keyed by component.
    property var picked: ({})

    function togglePicked(key) {
        var next = {}
        for (var k in root.picked) next[k] = root.picked[k]
        next[key] = !next[key]
        root.picked = next
    }
    function pickedKeys() {
        var out = []
        for (var k in root.picked) if (root.picked[k]) out.push(k)
        return out
    }
    readonly property int pickedCount: root.pickedKeys().length

    Component.onCompleted: root.app.refreshComponents()

    Label {
        text: "Add-ons"
        font.pixelSize: 17; font.bold: true; color: Theme.text
        Layout.margins: 12
    }
    Label {
        text: "GraphVis plots CSV, Parquet and Arrow on its own. Everything below is "
            + "optional and can be added or dropped at any time without reinstalling "
            + "anything else."
        wrapMode: Text.WordWrap
        color: Theme.textSecondary
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12
    }

    Repeater {
        model: root.app.optionalComponents
        delegate: GvGroupBox {
            required property var modelData
            title: modelData.name
            Layout.fillWidth: true
            Layout.margins: 8

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.gap

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    CheckBox {
                        checked: root.picked[modelData.key] === true
                        enabled: !root.app.componentsBusy
                        onToggled: root.togglePicked(modelData.key)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.installed ? "Installed" : "Not installed"
                        color: modelData.installed ? Theme.positive : Theme.textMuted
                        font.bold: true
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    Label {
                        text: "~" + modelData.sizeMb + " MB"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeSmall
                    text: modelData.summary
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    text: "Cost: " + modelData.cost
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 8; Layout.rightMargin: 8
        spacing: Theme.gap
        Button {
            Layout.fillWidth: true
            text: root.app.componentsBusy ? "Working…"
                 : (root.pickedCount > 0 ? "Install " + root.pickedCount + " selected"
                                         : "Install selected")
            enabled: !root.app.componentsBusy && root.pickedCount > 0
            onClicked: root.app.installComponents(root.pickedKeys())
        }
        Button {
            text: "Remove selected"
            enabled: !root.app.componentsBusy && root.pickedCount > 0
            ToolTip.visible: hovered
            ToolTip.text: "Packages shared with a component you are keeping are left alone."
            onClicked: root.app.removeComponents(root.pickedKeys())
        }
        Button {
            text: "Refresh"
            enabled: !root.app.componentsBusy
            onClicked: root.app.refreshComponents()
        }
    }

    Label {
        visible: root.app.optionalComponents.length === 0 && !root.app.componentsBusy
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12
        wrapMode: Text.WordWrap
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeSmall
        text: "No add-on list yet. This reads tools/optional-components.json beside the "
            + "application; running from a build tree without it, there is nothing to show."
    }

    GvGroupBox {
        visible: root.app.componentsOutput !== ""
        title: "Install log"
        Layout.fillWidth: true
        Layout.margins: 8

        ColumnLayout {
            anchors.fill: parent
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                TextArea {
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    font.family: "Consolas"
                    font.pixelSize: 12
                    text: root.app.componentsOutput
                    onTextChanged: cursorPosition = length
                }
            }
        }
    }
}
