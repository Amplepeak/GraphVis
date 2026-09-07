// Qt 6 delegate scoping. Without this, an id from the enclosing file is not
// legally visible inside a delegate or an inline Component - it resolves only
// because the old unbound context lookup walks out of the component, which is
// slower at every evaluation and silently breaks the moment a model role
// shares a name. Bound resolves ids at compile time; delegates declare what
// they take from the model with `required property`.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

// The Analysis tab.
//
// This panel used to carry its own hard-coded list of eight operations. Six of
// the eleven the dispatch behind it advertised did not exist - `psd`,
// `spectrogram`, `autocorrelation`, `cluster`, `classify`, `regress` were
// looked up on engines that have no such method - and a list written by hand in
// a second place is exactly how that survived: nothing compares the buttons
// against what can actually be run.
//
// So the panel is built from the service's own catalogue. Every operation the
// registry declares appears here, grouped, with the column choices it reads and
// the values it cannot run without; an operation that is not registered has no
// button, and one that is registered cannot be forgotten.
PanelScroll {
    id: root
    required property var app

    readonly property var columns: root.app.activeColumns || []
    readonly property bool hasData: root.columns.length > 0
    readonly property bool ready: root.hasData && !root.app.busy && root.app.scienceServiceAvailable

    readonly property var catalogue: root.app.analysisCatalogue || []

    // The group names, in the order the registry declares them, so related
    // operations stay together rather than being alphabetised apart.
    readonly property var groups: {
        var seen = []
        for (var i = 0; i < root.catalogue.length; ++i) {
            var g = root.catalogue[i].group || "Analysis"
            if (seen.indexOf(g) < 0) seen.push(g)
        }
        return seen
    }

    property string activeGroup: ""
    property string filterText: ""
    property var selected: null
    // Values typed for an operation's required fields, keyed by request key.
    // Reassigned rather than mutated: a bound property does not re-evaluate
    // when the object it points at is changed in place.
    property var fieldValues: ({})
    // Columns ticked for the operations that take a set rather than one.
    property var chosenColumns: []

    readonly property var visibleOperations: {
        var out = []
        var needle = root.filterText.toLowerCase()
        for (var i = 0; i < root.catalogue.length; ++i) {
            var op = root.catalogue[i]
            if (root.activeGroup !== "" && (op.group || "Analysis") !== root.activeGroup)
                continue
            if (needle !== "" && (op.label || "").toLowerCase().indexOf(needle) < 0
                    && (op.id || "").toLowerCase().indexOf(needle) < 0)
                continue
            out.push(op)
        }
        return out
    }

    // What the selected operation still needs before it can be run: an empty
    // list means the button is honest.
    readonly property var unmet: {
        var out = []
        if (!root.selected) return out
        var needs = root.selected.needs || []
        for (var i = 0; i < needs.length; ++i) {
            var n = needs[i]
            if ((n === "columns" || n === "predictors" || n === "factors")
                    && root.chosenColumns.length === 0)
                out.push(n === "columns" ? "at least one ticked column"
                                         : "at least one ticked column for " + n)
        }
        var fields = root.selected.fields || []
        for (var j = 0; j < fields.length; ++j) {
            var key = fields[j].key
            var value = root.fieldValues[key]
            if (value === undefined || String(value).trim() === "") out.push(key)
        }
        return out
    }

    function setField(key, value) {
        var next = {}
        for (var k in root.fieldValues) next[k] = root.fieldValues[k]
        next[key] = value
        root.fieldValues = next
    }

    function toggleColumn(name, on) {
        var next = []
        for (var i = 0; i < root.chosenColumns.length; ++i)
            if (root.chosenColumns[i] !== name) next.push(root.chosenColumns[i])
        if (on) next.push(name)
        root.chosenColumns = next
    }

    // A field's text as the value the service should receive. Anything that
    // parses as JSON - {"a": {"kind": "normal"}}, [0, 1], 12.5 - is sent as
    // that; anything else is sent as the string it is, which is what a unit, a
    // DOI or an expression should be.
    function valueOf(text) {
        var trimmed = String(text).trim()
        if (trimmed === "") return trimmed
        var first = trimmed.charAt(0)
        if (first === "{" || first === "[" || first === "-" || (first >= "0" && first <= "9")) {
            try { return JSON.parse(trimmed) } catch (e) { return trimmed }
        }
        return trimmed
    }

    // Every column choice is sent at once. An operation takes the keys it
    // declared and ignores the rest, so one request shape serves all 80-odd of
    // them - and a new operation needs no change here at all.
    function requestFor(op) {
        var r = {}
        var needs = op.needs || []
        if (needs.indexOf("x") >= 0) r.x = xBox.currentText
        if (needs.indexOf("y") >= 0) r.y = yBox.currentText
        if (needs.indexOf("y2") >= 0) r.y2 = y2Box.currentText
        if (needs.indexOf("z") >= 0) r.z = zBox.currentText
        if (needs.indexOf("group") >= 0) r.group = groupBox.currentText
        if (needs.indexOf("columns") >= 0) r.columns = root.chosenColumns
        if (needs.indexOf("predictors") >= 0) r.predictors = root.chosenColumns
        if (needs.indexOf("factors") >= 0) r.factors = root.chosenColumns
        var fields = op.fields || []
        for (var i = 0; i < fields.length; ++i)
            r[fields[i].key] = root.valueOf(root.fieldValues[fields[i].key] || "")
        return r
    }

    function shortNumber(v) {
        if (typeof v !== "number") return String(v)
        if (!isFinite(v)) return "—"
        if (v === Math.round(v) && Math.abs(v) < 1e6) return String(v)
        return v.toPrecision(6).replace(/0+$/, "").replace(/\.$/, "")
    }

    function describe(value, depth) {
        if (value === null || value === undefined) return "—"
        if (typeof value === "number") return root.shortNumber(value)
        if (typeof value === "boolean" || typeof value === "string") return String(value)
        if (Array.isArray(value)) {
            if (value.length === 0) return "(none)"
            var head = []
            for (var i = 0; i < Math.min(6, value.length); ++i)
                head.push(root.describe(value[i], depth + 1))
            return head.join(", ") + (value.length > 6 ? " … (" + value.length + " values)" : "")
        }
        if (value.columns !== undefined && value.rows !== undefined)
            return value.columns.length + " columns × "
                    + (value.columns.length ? (value.rows[value.columns[0]] || []).length : 0) + " rows"
        if (depth > 1) return "{…}"
        var parts = []
        for (var k in value) {
            parts.push(k + ": " + root.describe(value[k], depth + 1))
            if (parts.length >= 4) break
        }
        return parts.join("; ")
    }

    // Any reply, as readable lines. The registry returns eighty-odd different
    // shapes and this panel knows none of them by name - which is the point:
    // the old renderer showed `recommendations`, `summary` and `metrics` and
    // discarded everything else, so most operations appeared to return nothing.
    readonly property var resultRows: {
        var out = []
        var reply = root.app.analysisResult || {}
        var hide = ["ok", "op", "operation", "error", "path"]
        for (var key in reply) {
            if (hide.indexOf(key) >= 0) continue
            var value = reply[key]
            // A list of sentences or of small records - a recommendation, a
            // contribution, a detected limit - reads as one line each rather
            // than as a paragraph of semicolons.
            var expand = Array.isArray(value) && value.length > 0 && value.length <= 12
                    && (typeof value[0] === "string"
                        || (typeof value[0] === "object" && value[0] !== null
                            && !Array.isArray(value[0])))
            if (expand) {
                for (var i = 0; i < value.length; ++i) {
                    out.push({ name: (i === 0 ? key : ""), text: root.describe(value[i], 1) })
                    if (out.length >= 40) break
                }
            } else {
                out.push({ name: key, text: root.describe(value, 0) })
            }
            if (out.length >= 40) break
        }
        return out
    }

    Component.onCompleted: root.app.refreshAnalysisCatalogue()

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
    Label {
        visible: root.app.scienceServiceAvailable && root.catalogue.length === 0
        text: "Reading the list of operations from the science service…"
        wrapMode: Text.WordWrap; color: Theme.textMuted
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
    }

    GvGroupBox {
        title: "Columns"; Layout.fillWidth: true; Layout.margins: 8
        visible: root.hasData
        ColumnLayout {
            anchors.fill: parent; spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label { text: "X"; Layout.preferredWidth: 46; color: Theme.textSecondary }
                ComboBox { id: xBox; Layout.fillWidth: true; model: root.columns }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Y"; Layout.preferredWidth: 46; color: Theme.textSecondary }
                ComboBox {
                    id: yBox; Layout.fillWidth: true; model: root.columns
                    currentIndex: Math.min(1, root.columns.length - 1)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Y₂"; Layout.preferredWidth: 46; color: Theme.textSecondary }
                ComboBox {
                    id: y2Box; Layout.fillWidth: true; model: root.columns
                    currentIndex: Math.min(2, root.columns.length - 1)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Z"; Layout.preferredWidth: 46; color: Theme.textSecondary }
                ComboBox {
                    id: zBox; Layout.fillWidth: true; model: root.columns
                    currentIndex: Math.min(2, root.columns.length - 1)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Group"; Layout.preferredWidth: 46; color: Theme.textSecondary }
                ComboBox { id: groupBox; Layout.fillWidth: true; model: root.columns }
            }
        }
    }

    GvGroupBox {
        title: "Column set"; Layout.fillWidth: true; Layout.margins: 8
        visible: root.hasData
        ColumnLayout {
            anchors.fill: parent; spacing: 2
            Label {
                text: "Ticked columns are the set for ANOVA and Tukey, the predictors for regression and machine learning, and the factors for a factorial design."
                wrapMode: Text.WordWrap; color: Theme.textSecondary
                font.pixelSize: 11; Layout.fillWidth: true; Layout.bottomMargin: 4
            }
            Repeater {
                model: root.columns
                delegate: CheckBox {
                    required property string modelData
                    text: modelData
                    Layout.fillWidth: true
                    checked: root.chosenColumns.indexOf(modelData) >= 0
                    onToggled: root.toggleColumn(modelData, checked)
                }
            }
        }
    }

    GvGroupBox {
        title: "Operations"; Layout.fillWidth: true; Layout.margins: 8
        visible: root.catalogue.length > 0
        ColumnLayout {
            anchors.fill: parent; spacing: 6
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: groupPicker
                    Layout.fillWidth: true
                    model: ["All groups"].concat(root.groups)
                    onActivated: root.activeGroup = (currentIndex === 0 ? "" : currentText)
                }
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: "Search " + root.catalogue.length + " operations…"
                onTextChanged: root.filterText = text
            }
            Repeater {
                model: root.visibleOperations
                delegate: ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    highlighted: root.selected && root.selected.id === modelData.id
                    text: modelData.label
                    onClicked: {
                        root.selected = modelData
                        root.fieldValues = ({})
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.group + " · " + modelData.id
                }
            }
            Label {
                visible: root.visibleOperations.length === 0
                text: "No operation matches that."
                color: Theme.textMuted; font.pixelSize: 11
            }
        }
    }

    GvGroupBox {
        title: root.selected ? root.selected.label : "Run"
        Layout.fillWidth: true; Layout.margins: 8
        visible: root.selected !== null
        ColumnLayout {
            anchors.fill: parent; spacing: 6
            Label {
                text: root.selected ? (root.selected.group + " · " + root.selected.id
                                       + ((root.selected.needs || []).length
                                          ? " · uses " + root.selected.needs.join(", ") : ""))
                                    : ""
                wrapMode: Text.WordWrap; color: Theme.textMuted
                font.pixelSize: 11; Layout.fillWidth: true
            }
            Repeater {
                model: root.selected ? (root.selected.fields || []) : []
                delegate: ColumnLayout {
                    id: field
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: field.modelData.key
                        color: Theme.textSecondary; font.pixelSize: 11
                    }
                    TextField {
                        Layout.fillWidth: true
                        placeholderText: field.modelData.hint
                        text: root.fieldValues[field.modelData.key] || ""
                        onTextChanged: root.setField(field.modelData.key, text)
                    }
                }
            }
            Button {
                text: "Run"
                Layout.fillWidth: true
                enabled: root.ready && root.unmet.length === 0
                onClicked: root.app.runAnalysis(root.selected.id, root.requestFor(root.selected))
            }
            Label {
                visible: root.unmet.length > 0
                text: "Still needed: " + root.unmet.join(", ")
                wrapMode: Text.WordWrap; color: Theme.warning
                font.pixelSize: 11; Layout.fillWidth: true
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
            ComboBox {
                id: imputationBox
                Layout.fillWidth: true
                // A sweep with failed cells in the middle of it leaves holes the
                // estimator will not fill, because filling them is a claim about
                // data that is missing. Chosen here, never assumed - and cells
                // outside the data's own footprint stay masked either way.
                model: ["Leave failed cells empty", "Nearest Neighbor Fill",
                        "Local Mean Imputation", "Baseline Clamp (colour-scale minimum)",
                        "Symmetric Mirror Fill"]
            }
            Button {
                text: "Estimate surface from X, Y and Z"
                Layout.fillWidth: true
                enabled: root.ready
                onClicked: root.app.estimateSurface(xBox.currentText, yBox.currentText,
                                                    zBox.currentText, estimatorBox.currentText, 160,
                                                    imputationBox.currentIndex === 0
                                                        ? "" : imputationBox.currentText)
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
            Repeater {
                model: root.app.analysisOk ? root.resultRows : []
                delegate: RowLayout {
                    id: line
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: line.modelData.name
                        color: Theme.textSecondary; font.pixelSize: 11
                        Layout.preferredWidth: 96; Layout.alignment: Qt.AlignTop
                        wrapMode: Text.WrapAnywhere
                    }
                    Label {
                        text: line.modelData.text
                        wrapMode: Text.WordWrap; color: Theme.textMuted
                        font.pixelSize: 11; Layout.fillWidth: true
                    }
                }
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
