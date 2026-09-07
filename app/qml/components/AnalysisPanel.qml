import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

// The Analysis tab.
//
// This used to be three permanently disabled buttons whose tooltips said the
// wiring was "intentionally disabled until an operation contract is selected".
// The contract exists now - AppController.runAnalysis() reaches limits,
// forecast, fft, weibull, regression, pca, doe, advisor and domain - so the
// buttons run, and the result is shown rather than discarded.
PanelScroll {
    id: root
    required property var app

    readonly property var columns: root.app.activeColumns || []
    readonly property bool hasData: root.columns.length > 0
    readonly property bool ready: root.hasData && !root.app.busy && root.app.scienceServiceAvailable

    // Every operation, with what it needs. Kept as data so the buttons, the
    // enablement rules and the request all come from one place.
    readonly property var operations: [
        {id:"limits",     label:"Limits: plateau, asymptote, knee", needs:["x","y"]},
        {id:"forecast",   label:"Forecast with a confidence band",  needs:["x","y"]},
        {id:"fft",        label:"FFT spectrum",                     needs:["y"]},
        {id:"weibull",    label:"Weibull reliability",              needs:["y"]},
        {id:"regression", label:"Linear regression",                needs:["y","predictors"]},
        {id:"pca",        label:"Principal components (PCA)",       needs:[]},
        {id:"advisor",    label:"Recommend graphs for this data",   needs:[]},
        {id:"domain",     label:"Detect electrochemical columns",   needs:[]}
    ]

    function requestFor(op) {
        var r = {}
        if (op.needs.indexOf("x") >= 0) r.x = xBox.currentText
        if (op.needs.indexOf("y") >= 0) r.y = yBox.currentText
        if (op.needs.indexOf("predictors") >= 0) {
            // Everything numeric except the response, which is the sensible
            // default and the only one expressible without a multi-select.
            var p = []
            for (var i = 0; i < root.columns.length; ++i)
                if (root.columns[i] !== yBox.currentText) p.push(root.columns[i])
            r.predictors = p
        }
        return r
    }
Label {
        text: "Analysis & Experiment Design"
        font.pixelSize: 17; font.bold: true; color: Theme.text; Layout.margins: 12
    }
    Label {
        text: "Native DataFusion handles filtering, joins, aggregation and SQL. Fitting, statistics, reliability and design of experiments run in the science service and return here."
        wrapMode: Text.WordWrap; color: Theme.textSecondary
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
    }

    Label {
        visible: !root.app.scienceServiceAvailable
        text: "The science add-on is not installed. Run INSTALL-DATA-FORMATS.bat to enable these."
        wrapMode: Text.WordWrap; color: Theme.warning
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
    }
    Label {
        visible: root.app.scienceServiceAvailable && !root.hasData
        text: "Import or select a dataset to analyse."
        wrapMode: Text.WordWrap; color: Theme.textSecondary
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
    }

    GvGroupBox {
        title: "Columns"; Layout.fillWidth: true; Layout.margins: 8
        visible: root.hasData
        ColumnLayout {
            anchors.fill: parent; spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label { text: "X"; Layout.preferredWidth: 24; color: Theme.textSecondary }
                ComboBox { id: xBox; Layout.fillWidth: true; model: root.columns }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Y"; Layout.preferredWidth: 24; color: Theme.textSecondary }
                ComboBox {
                    id: yBox; Layout.fillWidth: true; model: root.columns
                    currentIndex: Math.min(1, root.columns.length - 1)
                }
            }
        }
    }

    GvGroupBox {
        title: "Run"; Layout.fillWidth: true; Layout.margins: 8
        ColumnLayout {
            anchors.fill: parent; spacing: 6
            Repeater {
                model: root.operations
                delegate: Button {
                    Layout.fillWidth: true
                    text: modelData.label
                    enabled: root.ready
                    onClicked: root.app.runAnalysis(modelData.id, root.requestFor(modelData))
                }
            }
        }
    }

    GvGroupBox {
        title: "Surface estimation"; Layout.fillWidth: true; Layout.margins: 8
        visible: root.hasData
        ColumnLayout {
            anchors.fill: parent; spacing: 6
            Label {
                text: "Turn scattered x/y/z into a regular grid. The result is added as a dataset, so any field engine can draw it."
                wrapMode: Text.WordWrap; color: Theme.textSecondary; Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Z"; Layout.preferredWidth: 24; color: Theme.textSecondary }
                ComboBox {
                    id: zBox; Layout.fillWidth: true; model: root.columns
                    currentIndex: Math.min(2, root.columns.length - 1)
                }
            }
            ComboBox {
                id: estimatorBox
                Layout.fillWidth: true
                model: ["Auto (data-aware)", "Delaunay Triangulation (Linear)",
                        "Clough–Tocher C1 Smooth", "Natural Neighbor (Sibson’s)",
                        "Inverse Distance Weighting (IDW)", "Modified Shepard's Method",
                        "Thin Plate Spline (TPS)", "Multiquadric RBF", "Gaussian RBF",
                        "Ordinary Kriging", "Moving Least Squares (MLS)", "LOESS / LOWESS",
                        "Nearest Neighbor", "Bilinear Interpolation", "Bicubic Interpolation",
                        "Rectangular B-Spline", "Monotone PCHIP (Structured)"]
            }
            Button {
                text: "Estimate surface"
                Layout.fillWidth: true
                enabled: root.ready
                onClicked: root.app.estimateSurface(xBox.currentText, yBox.currentText,
                                                    zBox.currentText, estimatorBox.currentText, 160)
            }
        }
    }

    GvGroupBox {
        title: "Result"; Layout.fillWidth: true; Layout.margins: 8
        visible: Object.keys(root.app.analysisResult || {}).length > 0
        ColumnLayout {
            anchors.fill: parent; spacing: 4
            Label {
                text: root.app.analysisKind + (root.app.analysisOk ? " — complete" : " — failed")
                font.bold: true
                color: root.app.analysisOk ? Theme.positive : Theme.danger
            }
            Label {
                visible: !root.app.analysisOk
                text: (root.app.analysisResult.error || "")
                wrapMode: Text.WordWrap; color: Theme.textSecondary; Layout.fillWidth: true
            }
            // Recommendations and summaries are the two readable shapes;
            // tables and curves are for the plot, not for a text panel.
            Repeater {
                model: root.app.analysisResult.recommendations || []
                delegate: Label {
                    text: "• " + modelData.engine + " — " + modelData.why
                    wrapMode: Text.WordWrap; color: Theme.textMuted
                    font.pixelSize: 11; Layout.fillWidth: true
                }
            }
            Repeater {
                model: root.app.analysisResult.summary || []
                delegate: Label {
                    text: "• " + modelData
                    wrapMode: Text.WordWrap; color: Theme.textMuted
                    font.pixelSize: 11; Layout.fillWidth: true
                }
            }
            Label {
                visible: root.app.analysisOk && (root.app.analysisResult.metrics !== undefined)
                text: JSON.stringify(root.app.analysisResult.metrics || {})
                wrapMode: Text.WrapAnywhere; color: Theme.textMuted; font.pixelSize: 11
                Layout.fillWidth: true
            }
        }
    }

    GvGroupBox {
        title: "Runtime"; Layout.fillWidth: true; Layout.margins: 8
        ColumnLayout {
            anchors.fill: parent
            Button {
                text: "Open DataFusion SQL workspace"
                Layout.fillWidth: true
                onClicked: root.app.workspaceMode = "Data"
            }
            Label {
                text: "Python · Julia · R are plugins, not the application host."
                wrapMode: Text.WordWrap; color: Theme.textSecondary; Layout.fillWidth: true
            }
            Label { text: "Python: " + root.app.pluginServicePath("python"); wrapMode: Text.WrapAnywhere; color: Theme.textMuted; Layout.fillWidth: true }
        }
    }
}
