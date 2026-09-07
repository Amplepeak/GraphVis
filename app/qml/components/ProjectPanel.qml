// Qt 6 delegate scoping. Without this, an id from the enclosing file is not
// legally visible inside a delegate or an inline Component - it resolves only
// because the old unbound context lookup walks out of the component, which is
// slower at every evaluation and silently breaks the moment a model role
// shares a name. Bound resolves ids at compile time; delegates declare what
// they take from the model with `required property`.
pragma ComponentBehavior: Bound
// Project data and context.
//
// A port of the panel GraphVis 17 put at the top of its sidebar, and the reason
// that version could tell you what you had: a project is a folder that gets
// rescanned on every start, so a dataset you added in March is still listed in
// September whether or not it is loaded.
//
// Two shapes, chosen when the project is made. A managed project is a folder
// GraphVis creates and copies files into - somewhere to point a backup at. An
// adopted project is a folder you already have, scanned where it stands, with
// nothing copied or moved.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GraphVis

PanelScroll {
    id: root
    contentSpacing: Theme.gap
    required property var app
    readonly property var project: root.app.project

    // Multi-select, held here rather than in the model so a rescan does not
    // silently drop what the user had picked.
    property var selectedDatasets: ({})
    function toggleSelected(path) {
        var next = {}
        for (var k in root.selectedDatasets) next[k] = root.selectedDatasets[k]
        if (next[path]) delete next[path]
        else next[path] = true
        root.selectedDatasets = next
    }
    function selectedList() {
        var out = []
        for (var k in root.selectedDatasets) if (root.selectedDatasets[k]) out.push(k)
        return out
    }
    function clearSelection() { root.selectedDatasets = ({}) }

    FolderDialog {
        id: newProjectLocation
        title: "Where should this project live?"
        onAccepted: root.project.createProject(projectName.text, selectedFolder)
    }
    FolderDialog {
        id: openFolder
        title: "Open a GraphVis project folder"
        onAccepted: root.project.openProject(selectedFolder)
    }
    FolderDialog {
        id: adoptDialog
        title: "Use an existing folder as a project"
        onAccepted: root.project.adoptFolder(selectedFolder)
    }
    FileDialog {
        id: addDatasetDialog
        title: "Add dataset to project"
        nameFilters: root.app.importNameFilters()
        onAccepted: root.project.addDataset(selectedFile)
    }
    FileDialog {
        id: addLiteratureDialog
        title: "Add literature to project"
        nameFilters: ["Scientific papers (*.pdf)", "All files (*)"]
        onAccepted: root.project.addLiterature(selectedFile)
    }
    FileDialog {
        id: addScriptDialog
        title: "Add analysis script or parameter file"
        nameFilters: ["Analysis code (*.m *.py *.r *.jl *.ipynb *.txt)", "All files (*)"]
        onAccepted: root.project.addScript(selectedFile)
    }


    // ---------------------------------------------------- no project yet
    GvGroupBox {
        visible: !root.project.hasProject
        title: "Start a project"
        Layout.fillWidth: true
        Layout.margins: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                text: "A project is a folder GraphVis rescans every time it starts, so the "
                    + "papers, datasets and scripts that belong together stay together "
                    + "between sessions."
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                TextField {
                    id: projectName
                    Layout.fillWidth: true
                    placeholderText: "New project name"
                    onAccepted: if (text.trim() !== "") root.project.createProject(text, "")
                }
                Button {
                    text: "Create"
                    enabled: projectName.text.trim() !== ""
                    onClicked: root.project.createProject(projectName.text, "")
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                text: "Created in " + root.project.defaultProjectsRoot
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                Button {
                    Layout.fillWidth: true
                    text: "Choose a location…"
                    enabled: projectName.text.trim() !== ""
                    onClicked: newProjectLocation.open()
                }
                Button {
                    Layout.fillWidth: true
                    text: "Open a project…"
                    onClicked: openFolder.open()
                }
            }
            Button {
                Layout.fillWidth: true
                text: "Use a folder I already have…"
                ToolTip.visible: hovered
                ToolTip.text: "Point GraphVis at an existing folder - a thesis directory, say. "
                            + "It is scanned where it stands and nothing is copied or moved."
                onClicked: adoptDialog.open()
            }

            Label {
                visible: root.project.recentProjects.length > 0
                text: "Recent"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                Layout.topMargin: 4
            }
            Repeater {
                model: root.project.recentProjects
                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    flat: true
                    text: modelData.name + "   ·   " + modelData.mode
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.path
                    onClicked: root.project.openProjectPath(modelData.path)
                }
            }
        }
    }

    // ------------------------------------------------- project is open
    GvGroupBox {
        visible: root.project.hasProject
        title: "Project data & context"
        Layout.fillWidth: true
        Layout.margins: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                text: "Read the papers that explain the project, add datasets once, then link "
                    + "them in a named group. GraphVis rescans these folders on later starts."
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: root.project.name
                    font.bold: true
                    color: Theme.text
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Rectangle {
                    Layout.preferredWidth: modeTag.implicitWidth + 10
                    Layout.preferredHeight: 17
                    radius: 3
                    color: Theme.surfaceAlt
                    border.color: Theme.border
                    Label {
                        id: modeTag
                        anchors.centerIn: parent
                        text: root.project.mode === "adopted" ? "in place" : "managed"
                        color: Theme.textSecondary
                        font.pixelSize: 9
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: root.project.root
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideMiddle
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                Label { text: "Group"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                ComboBox {
                    Layout.fillWidth: true
                    model: root.project.groupNames
                    currentIndex: root.project.groupNames.indexOf(root.project.activeGroup)
                    displayText: root.project.activeGroup === "" ? "No group yet" : root.project.activeGroup
                    onActivated: root.project.activeGroup = root.project.groupNames[currentIndex]
                }
                Button {
                    text: "+"
                    ToolTip.visible: hovered
                    ToolTip.text: "New group"
                    onClicked: groupPrompt.open()
                }
                Button {
                    text: "−"
                    enabled: root.project.activeGroup !== ""
                    ToolTip.visible: hovered
                    ToolTip.text: "Remove this group. No files are deleted."
                    onClicked: root.project.deleteGroup(root.project.activeGroup)
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.gap
                rowSpacing: 6
                Button {
                    Layout.fillWidth: true
                    text: "Add Literature…"
                    onClicked: addLiteratureDialog.open()
                }
                Button {
                    Layout.fillWidth: true
                    text: "Add Dataset…"
                    onClicked: addDatasetDialog.open()
                }
                Button {
                    Layout.fillWidth: true
                    text: "Add Analysis Script…"
                    ToolTip.visible: hovered
                    ToolTip.text: "Context only. GraphVis reads variable assignments and plotting "
                                + "hints; it never runs the script."
                    onClicked: addScriptDialog.open()
                }
                Button {
                    Layout.fillWidth: true
                    text: "↻ Rescan"
                    onClicked: root.project.rescan()
                }
            }
            Button {
                Layout.fillWidth: true
                flat: true
                text: "Open the project folder"
                onClicked: root.project.revealFolder("")
            }
        }
    }

    // ------------------------------------------------- scanned datasets
    GvGroupBox {
        visible: root.project.hasProject
        title: "Project datasets (auto-scanned)"
        Layout.fillWidth: true
        Layout.leftMargin: 8; Layout.rightMargin: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: root.project.scanSummary
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }

            Label {
                Layout.fillWidth: true
                visible: root.project.datasets.length === 0
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                text: root.project.mode === "adopted"
                      ? "Nothing readable found in this folder yet."
                      : "No datasets yet. Add Dataset… copies one into the project."
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, Math.max(0, root.project.datasets.length * 44))
                visible: root.project.datasets.length > 0
                clip: true
                spacing: 2
                model: root.project.datasets
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    height: 42
                    radius: 4
                    readonly property bool picked: root.selectedDatasets[row.modelData.path] === true
                    color: row.picked ? Theme.surfaceAlt
                                      : (rowHover.hovered ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                                                          : "transparent")
                    border.color: row.picked ? Theme.accent : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8; anchors.rightMargin: 8
                        spacing: Theme.gap

                        Label {
                            text: row.modelData.linked ? "◉" : (row.picked ? "◍" : "○")
                            color: row.modelData.linked ? Theme.accent : Theme.textMuted
                            ToolTip.visible: linkHover.hovered
                            ToolTip.text: row.modelData.linked
                                          ? "Linked to the active group"
                                          : "Not linked to the active group"
                            HoverHandler { id: linkHover }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                text: row.modelData.name
                                color: Theme.text
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                text: "." + row.modelData.suffix + "  ·  " + row.modelData.sizeText
                                      + "  ·  " + row.modelData.modifiedText
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeSmall
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Button {
                            text: "Load"
                            flat: true
                            ToolTip.visible: hovered
                            ToolTip.text: "Import this file into the workspace"
                            onClicked: root.app.importDatasetPath(row.modelData.path)
                        }
                    }

                    HoverHandler { id: rowHover }
                    TapHandler { onTapped: root.toggleSelected(row.modelData.path) }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.project.datasets.length > 0
                spacing: Theme.gap
                Button {
                    Layout.fillWidth: true
                    text: "Link selected"
                    enabled: root.selectedList().length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Associate the selected datasets with \"" 
                                + (root.project.activeGroup || "Default") + "\""
                    onClicked: {
                        root.project.linkToActiveGroup(root.selectedList(), "datasets")
                        root.clearSelection()
                    }
                }
                Button {
                    text: "Load selected"
                    enabled: root.selectedList().length > 0
                    onClicked: {
                        var picked = root.selectedList()
                        for (var i = 0; i < picked.length; ++i)
                            root.app.importDatasetPath(picked[i])
                        root.clearSelection()
                    }
                }
                Button {
                    text: "Remove"
                    enabled: root.selectedList().length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: root.project.mode === "adopted"
                                  ? "Unlink from the group. The file itself is left alone."
                                  : "Move out of the scanned folder into detached/. Nothing is deleted."
                    onClicked: {
                        var picked = root.selectedList()
                        for (var i = 0; i < picked.length; ++i)
                            root.project.detachDataset(picked[i])
                        root.clearSelection()
                    }
                }
            }
        }
    }

    // ------------------------------------------------- papers and scripts
    GvGroupBox {
        visible: root.project.hasProject
                 && (root.project.literature.length > 0 || root.project.scripts.length > 0)
        title: "Papers and scripts"
        Layout.fillWidth: true
        Layout.leftMargin: 8; Layout.rightMargin: 8
        Layout.bottomMargin: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: 2

            Repeater {
                model: root.project.literature
                delegate: RowLayout {
                    id: paper
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    Label { text: "▣"; color: Theme.accent }
                    Label {
                        text: paper.modelData.name
                        color: Theme.text
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Button {
                        flat: true
                        text: "Read"
                        onClicked: root.app.openLiteraturePath(paper.modelData.path)
                    }
                }
            }
            Repeater {
                model: root.project.scripts
                delegate: RowLayout {
                    id: script
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    Label { text: "⌗"; color: Theme.textMuted }
                    Label {
                        text: script.modelData.name
                        color: Theme.textSecondary
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: script.modelData.sizeText
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }
    }

    Label {
        visible: root.project.status !== ""
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 10
        text: root.project.status
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }

    Dialog {
        id: groupPrompt
        title: "New project group"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            root.project.createGroup(groupNameField.text)
            groupNameField.text = ""
        }
        ColumnLayout {
            spacing: Theme.gap
            Label {
                text: "Name a group for the papers, datasets and scripts that belong together."
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                Layout.preferredWidth: 320
            }
            TextField {
                id: groupNameField
                Layout.fillWidth: true
                placeholderText: "e.g. thesis, chapter 4, calibration run"
            }
        }
    }
}
