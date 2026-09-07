// The solver bridge.
//
// GraphVis 17 could run an external computational engine - MATLAB, ANSYS MAPDL,
// COMSOL, AQUASIM, or any command-line solver - and then pick up whatever files
// the run produced, so a parameter sweep bound to the mapping controls without
// anyone opening a file dialog afterwards.
//
// It is engine-agnostic on purpose. The command is a template with {script} and
// {workdir} substituted, editable in place, so a solver nobody anticipated is a
// line of text rather than a code change.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GraphVis

PanelScroll {
    id: root
    contentSpacing: 10

    required property var app

    readonly property var presets: root.app.solverPresets()
    property int presetIndex: 0
    readonly property var preset: root.presets[root.presetIndex]

    // The MATLAB Engine API runs the script in a live session and reads the
    // workspace back with no files in between, so it has no command template.
    readonly property bool engineApi: engineApiCheck.checked

    function applyPreset(index) {
        root.presetIndex = index
        commandField.text = root.presets[index].command
        patternField.text = root.presets[index].outputs
    }

    FileDialog {
        id: scriptDialog
        title: "Choose the model or script to run"
        onAccepted: {
            scriptField.text = selectedFile.toString().replace("file:///", "")
            if (workdirField.text === "")
                workdirField.text = scriptField.text.replace(/[\\/][^\\/]*$/, "")
        }
    }
    FolderDialog {
        id: workdirDialog
        title: "Working directory for the run"
        onAccepted: workdirField.text = selectedFolder.toString().replace("file:///", "")
    }

    Component.onCompleted: root.applyPreset(0)

    Label {
        text: "Run a Solver"
        font.pixelSize: 17; font.bold: true; color: Theme.text
        Layout.margins: 12
    }
    Label {
        text: "Run an external engine and bring its results straight back in. "
            + "Whatever files the run writes are imported through the ordinary reader, "
            + "so all 149 formats apply rather than a special case per engine."
        wrapMode: Text.WordWrap
        color: Theme.textSecondary
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12
    }

    GvGroupBox {
        title: "Engine"
        Layout.fillWidth: true
        Layout.margins: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap

            ComboBox {
                id: presetBox
                Layout.fillWidth: true
                model: root.presets.map(function (p) { return p.name })
                onActivated: (index) => root.applyPreset(index)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                text: root.preset ? root.preset.hint : ""
            }
            CheckBox {
                id: engineApiCheck
                text: "Run through the MATLAB engine instead of a command"
                ToolTip.visible: hovered
                ToolTip.text: "Runs the .m file in a live MATLAB session and reads every numeric "
                            + "workspace variable directly. Needs the MATLAB Engine API for Python."
            }
        }
    }

    GvGroupBox {
        title: "What to run"
        Layout.fillWidth: true
        Layout.margins: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                TextField {
                    id: scriptField
                    Layout.fillWidth: true
                    placeholderText: "Model or script file"
                }
                Button { text: "Browse…"; onClicked: scriptDialog.open() }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                TextField {
                    id: workdirField
                    Layout.fillWidth: true
                    placeholderText: "Working directory (defaults to the script's folder)"
                }
                Button { text: "Browse…"; onClicked: workdirDialog.open() }
            }
            TextField {
                id: commandField
                Layout.fillWidth: true
                enabled: !root.engineApi
                placeholderText: "Command template — {script} and {workdir} are substituted"
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                Label { text: "Collect"; color: Theme.textSecondary }
                TextField {
                    id: patternField
                    Layout.fillWidth: true
                    enabled: !root.engineApi
                    placeholderText: "*.csv;*.mat"
                    ToolTip.visible: hovered
                    ToolTip.text: "Only files written by this run are collected, so an old CSV "
                                + "sitting in the folder is not mistaken for a result."
                }
                SpinBox {
                    id: timeoutBox
                    enabled: !root.engineApi
                    from: 1; to: 1440; value: 60
                    textFromValue: function (v) { return v + " min" }
                    valueFromText: function (t) { return parseInt(t) }
                    ToolTip.visible: hovered
                    ToolTip.text: "Time limit. The engine is stopped if it runs longer."
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                Button {
                    Layout.fillWidth: true
                    text: root.app.solverRunning ? "Running…" : "Run"
                    enabled: !root.app.solverRunning && scriptField.text !== ""
                             && (root.engineApi || !root.app.busy)
                    onClicked: {
                        if (root.engineApi)
                            root.app.runMatlabScript("file:///" + scriptField.text,
                                                     workdirField.text ? "file:///" + workdirField.text : "")
                        else
                            root.app.runSolver(commandField.text,
                                               workdirField.text ? "file:///" + workdirField.text : "",
                                               "file:///" + scriptField.text,
                                               patternField.text,
                                               timeoutBox.value * 60)
                    }
                }
                Button {
                    text: "Stop"
                    enabled: root.app.solverRunning
                    onClicked: root.app.cancelSolver()
                }
            }
            Label {
                visible: root.engineApi && !root.app.scienceServiceAvailable
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.warning
                font.pixelSize: Theme.fontSizeSmall
                text: "The MATLAB engine path runs through the science add-on, which is not installed."
            }
        }
    }

    GvGroupBox {
        title: "Engine output"
        Layout.fillWidth: true
        Layout.margins: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                color: root.app.solverResult.error ? Theme.warning
                     : (root.app.solverResult.cancelled ? Theme.warning : Theme.textMuted)
                text: {
                    var r = root.app.solverResult
                    if (r.error) return String(r.error)
                    if (root.app.solverRunning) return "Running…"
                    if (r.cancelled) return "Stopped before it finished."
                    if (r.exitCode !== undefined) {
                        var files = r.files ? r.files.length : 0
                        return "Exit code " + r.exitCode + " · " + files + " output file(s) collected"
                             + (r.skipped && r.skipped.length
                                ? " · skipped: " + r.skipped.join(", ") : "")
                    }
                    return "Nothing has been run yet."
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                visible: root.app.solverOutput !== ""
                TextArea {
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    font.family: "Consolas"
                    font.pixelSize: 12
                    text: root.app.solverOutput
                    // A long run is read at its end, not its beginning.
                    onTextChanged: cursorPosition = length
                }
            }
        }
    }
}
