// The formula, for the engines that plot one instead of a dataset.
//
// Seven catalogue engines - Function Plot, Function Surface, Function Mesh,
// Function Contour, Function 3D Parametric, Implicit Function, Implicit Surface
// - take an expression rather than columns. `PlotSpec::expression` has carried
// it since they were written, and until now nothing anywhere could set it: no
// property on the canvas, no field, no menu. So all seven drew the hard-coded
// demonstration formula in the renderer's fallback branch, for ever. An
// expression compiler, an RPN evaluator and a table of twenty-five functions,
// reachable only as a fixed picture.
//
// It compiles as you type. A formula field whose only failure mode is an empty
// plot is worse than no field: an empty plot is also what a missing column, an
// unsupported engine and an all-NaN dataset look like, so a typo is
// indistinguishable from the program being broken. "Unknown name 'sni'" is the
// difference.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

ColumnLayout {
    id: root
    required property var canvas
    spacing: 6

    // Which letters this engine binds, so the hint names the ones that will
    // actually resolve rather than a general truth about the parser.
    readonly property string variables: {
        if (!root.canvas) return "x"
        const e = String(root.canvas.engine || "")
        if (e === "Function 3D Parametric") return "t"
        if (e === "Function Surface" || e === "Function Mesh" || e === "Function Contour"
            || e === "Implicit Surface" || e === "Implicit Function") return "x and y"
        return "x"
    }
    // !!( ), because `canvas` is a var that starts null and `a && b` yields A
    // when A is falsy - so before the canvas is wired this assigned null into a
    // bool. Third instance of that class; the first was found by the build
    // report from the running program, this one by the passive audit.
    readonly property bool parametric:
        !!(root.canvas
           && String(root.canvas.engine || "") === "Function 3D Parametric")

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Label {
            text: "Formula"
            color: Theme.textSecondary
            font.pixelSize: 10
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            text: root.parametric
                  ? "three formulas in " + root.variables + ", separated by semicolons"
                  : "in " + root.variables
            color: Theme.textMuted
            font.pixelSize: 10
            elide: Text.ElideRight
        }
        ToolButton {
            id: helpButton
            text: "ƒ"
            flat: true
            font.pixelSize: 12
            implicitHeight: 20
            ToolTip.visible: helpButton.hovered
            ToolTip.text: "What this understands"
            onClicked: helpPopup.opened ? helpPopup.close() : helpPopup.open()
        }
    }

    TextField {
        id: field
        Layout.fillWidth: true
        font.family: "Consolas, monospace"
        font.pixelSize: 12
        placeholderText: root.parametric ? "cos(t); sin(t); t/6" : "sin(x)*exp(-x/6)"
        // Bound only while not being edited, so a rebuild triggered by the
        // formula itself cannot rewrite the box under the cursor.
        text: root.canvas ? String(root.canvas.expression || "") : ""
        onActiveFocusChanged: if (!activeFocus && root.canvas)
                                  text = String(root.canvas.expression || "")
        // As you type. The canvas compiles it and says what is wrong; it is
        // cheap, and waiting for Enter means the error arrives after you have
        // stopped looking at what caused it.
        onTextEdited: if (root.canvas) root.canvas.expression = field.text

        // Declared INSIDE the field rather than in the column.
        //
        // A Popup is its own window and is not laid out by anything, but a
        // ColumnLayout counts it as a child and qmllint is right to object to
        // y and width on a managed item. A TextField's default property takes
        // objects without positioning them, and this belongs to the field
        // anyway - it is where it opens.
        Popup {
            id: helpPopup
            parent: field
            y: field.height + 4
            width: Math.min(360, field.width)
            padding: 12
            modal: false
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            background: Rectangle {
                color: Theme.surface
                border.color: Theme.borderStrong
                radius: Theme.radius
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 6
                Label {
                    text: "Functions"
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    font.family: "Consolas, monospace"
                    font.pixelSize: 11
                    color: Theme.text
                    // Asked of the parser, not written out here. A second list would
                    // be a list that drifts, and this one is exactly what compiles.
                    text: root.canvas ? root.canvas.expressionFunctions().join("   ") : ""
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 10
                    color: Theme.textMuted
                    text: "Operators + − * / ^ and brackets. Variables here are "
                        + root.variables + "."
                }
            }
        }
    }

    // What is wrong, from the compiler rather than from a guess here.
    Label {
        Layout.fillWidth: true
        visible: !!(root.canvas && String(root.canvas.expressionError || "") !== "")
        text: root.canvas ? String(root.canvas.expressionError || "") : ""
        color: Theme.warning
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        visible: !!(root.canvas && String(root.canvas.expressionError || "") === ""
                    && String(root.canvas.expression || "").trim() === "")
        text: "Empty draws the engine's example formula, so there is always "
            + "something on screen to edit."
        color: Theme.textMuted
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }

}
