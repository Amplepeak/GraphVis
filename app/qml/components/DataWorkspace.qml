// Data workspace.
//
// The previous version led with "Arrow-native workspace / Python is not the
// storage layer" and put a DataFusion SQL box - defaulting to a table that
// does not exist - as the most prominent control, with no way to add a dataset
// at all. Importing lives in this tab now: a drop target, an explicit button,
// an activity log that records what happened to every file, and the list of
// what is loaded. SQL is an advanced tool, folded away.
//
// The activity log exists because the previous version reported failures with a
// status line that scrolled away: selecting a .mat with no science add-on
// installed looked exactly like nothing happening at all.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GraphVis

PanelScroll {
    id: root
    spacing: Theme.gap
    required property var app

    function stateColour(state) {
        if (state === "failed") return Theme.warning
        if (state === "loaded") return Theme.positive
        return Theme.accent
    }
    function stateGlyph(state) {
        if (state === "failed") return "✕"
        if (state === "loaded") return "✓"
        if (state === "queued") return "…"
        return "⟳"
    }

    FileDialog {
        id: addDialog
        title: "Add dataset"
        nameFilters: root.app.importNameFilters()
        onAccepted: root.app.importDataset(selectedFile)
    }


    Label {
        text: "Datasets"
        font.bold: true; font.pixelSize: Theme.fontSizeTitle
        color: Theme.text
        Layout.margins: 12
        Layout.bottomMargin: 0
    }

    // Drop target. Doubles as the empty state when nothing is loaded.
    Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12
        Layout.preferredHeight: root.app.datasets.length === 0 ? 150 : 84
        radius: Theme.radius
        color: dropArea.containsDrag ? Theme.surfaceAlt : "transparent"
        border.color: dropArea.containsDrag ? Theme.accent : Theme.border
        border.width: dropArea.containsDrag ? 2 : 1

        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 24
            spacing: Theme.gap

            Label {
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                color: dropArea.containsDrag ? Theme.accent : Theme.textSecondary
                text: dropArea.containsDrag
                      ? "Release to import"
                      : "Drag datasets here - drop as many as you like"
            }
            Button {
                Layout.alignment: Qt.AlignHCenter
                text: "Add dataset…"
                onClicked: addDialog.open()
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: root.app.datasets.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                text: "CSV, TSV, Parquet and Arrow load natively. MATLAB, HDF5, Excel, CTD casts, "
                    + "dive logs and 140-odd other formats are converted by the GraphVis Science add-on."
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        DropArea {
            id: dropArea
            anchors.fill: parent
            onDropped: (drop) => {
                if (drop.hasUrls && drop.urls.length > 0) {
                    for (var i = 0; i < drop.urls.length; ++i)
                        root.app.importDataset(drop.urls[i])
                }
            }
        }
    }

    // The add-on is what reads everything except CSV/TSV/Parquet/Arrow, so
    // when it is missing say so here rather than only at the moment of
    // failure.
    Rectangle {
        visible: !root.app.scienceServiceAvailable
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12
        Layout.preferredHeight: addonRow.implicitHeight + 20
        radius: Theme.radius
        color: Theme.surfaceAlt
        border.color: Theme.warning

        ColumnLayout {
            id: addonRow
            anchors.left: parent.left; anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 12; anchors.rightMargin: 12
            spacing: 6

            Label {
                text: "The GraphVis Science add-on is not installed"
                color: Theme.text
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                text: "Without it only CSV, TSV, Parquet and Arrow can be read. MATLAB (.mat), "
                    + "HDF5, Excel, instrument and marine formats all need it."
            }
            RowLayout {
                spacing: Theme.gap
                Button {
                    text: "Install data formats…"
                    enabled: root.app.dataFormatsInstallerAvailable
                    onClicked: root.app.installDataFormats()
                }
                Button {
                    text: "Check again"
                    onClicked: root.app.refreshScienceServiceAvailability()
                }
                Label {
                    visible: !root.app.dataFormatsInstallerAvailable
                    text: "Run INSTALL-DATA-FORMATS.bat in your GraphVis folder"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }
    }

    // What happened to every file that was added, and why.
    GvGroupBox {
        visible: root.app.importLog.length > 0
        title: "Import activity"
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12

        ColumnLayout {
            anchors.fill: parent
            spacing: 4

            Repeater {
                model: root.app.importLog
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap

                    Label {
                        text: root.stateGlyph(modelData.state)
                        color: root.stateColour(modelData.state)
                        font.bold: true
                        Layout.preferredWidth: 16
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            text: modelData.name
                            color: Theme.text
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: modelData.detail !== ""
                            text: modelData.detail
                            color: modelData.state === "failed" ? Theme.warning : Theme.textMuted
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    Label {
                        text: modelData.time
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
            Button {
                Layout.alignment: Qt.AlignRight
                flat: true
                text: "Clear"
                onClicked: root.app.clearImportLog()
            }
        }
    }

    Label {
        visible: root.app.busy
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12
        text: root.app.busyLabel
        color: Theme.accent
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }

    // What is loaded. Clicking selects the dataset every other panel uses.
    ListView {
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12
        Layout.preferredHeight: Math.max(0, Math.min(260, root.app.datasets.length * 56))
        visible: root.app.datasets.length > 0
        clip: true
        spacing: 4
        model: root.app.datasets

        delegate: Rectangle {
            id: row
            width: ListView.view.width
            height: 52
            radius: Theme.radius
            readonly property bool active: modelData.id === root.app.activeDatasetId
            color: row.active ? Theme.surfaceAlt : (rowHover.hovered ? Theme.surface : "transparent")
            border.color: row.active ? Theme.accent : Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10; anchors.rightMargin: 10
                spacing: Theme.gap

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        text: modelData.name || "dataset"
                        color: Theme.text
                        font.bold: row.active
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: (modelData.rows !== undefined ? modelData.rows + " rows" : "")
                              + (modelData.numeric_columns ? "  ·  " + modelData.numeric_columns.length + " numeric columns" : "")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                Label {
                    visible: row.active
                    text: "active"
                    color: Theme.accent
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            HoverHandler { id: rowHover }
            TapHandler { onTapped: root.app.activeDatasetId = modelData.id }
        }
    }

    // Advanced. Collapsed by default: most people never need it, and it
    // should not be the first thing this tab offers.
    CheckBox {
        id: sqlOpen
        Layout.leftMargin: 8
        text: "Query with SQL (advanced)"
        checked: false
    }

    GvGroupBox {
        visible: sqlOpen.checked
        title: "DataFusion SQL"
        Layout.fillWidth: true
        Layout.margins: 8
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gap
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                text: root.app.activeTableName
                      ? "The active dataset is registered as table \"" + root.app.activeTableName + "\"."
                      : "Add a dataset first - there is no table to query yet."
            }
            TextArea {
                id: sql
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                wrapMode: TextEdit.NoWrap
                // Default to a query that actually runs against what is loaded.
                text: "SELECT * FROM " + (root.app.activeTableName || "dataset") + " LIMIT 100"
            }
            Button {
                text: "Run query"
                enabled: !root.app.busy && root.app.activeDatasetId !== ""
                onClicked: root.app.runSql(sql.text)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                text: root.app.sqlResult
            }
        }
    }
}
