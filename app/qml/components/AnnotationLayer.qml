// Placing and editing notes on a figure.
//
// The canvas stores the notes and draws them; this is only the part that has to
// ask a person what a note says. Deliberately separate, because a renderer that
// opens windows of its own is a renderer that cannot be used headless, and the
// same PlotCanvas draws the PDF export and the self-test.
//
// The flow is: press Annotate, click a point, type, press Enter. The editor
// appears where the click landed, with the data coordinates of that point in
// its placeholder so it is obvious what the note will be attached to.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Item {
    id: root
    required property var canvas
    anchors.fill: parent

    // Nothing here should catch a pointer except the editor itself. The canvas
    // underneath needs every press, including the one that places a note.
    visible: root.canvas.annotating || editor.visible

    // Where the editor sits, and which note it is editing (-1 for a new one).
    property real placeX: 0
    property real placeY: 0
    property real dataX: 0
    property real dataY: 0
    property int editingIndex: -1

    Connections {
        target: root.canvas
        function onAnnotationRequested(x, y) {
            root.dataX = x
            root.dataY = y
            // The pointer position, not the data position: the editor is a
            // piece of interface and belongs where the person is looking.
            root.placeX = Math.max(4, Math.min(root.width - field.width - 4,
                                               root.canvas.mapToItem(root, 0, 0).x + 0))
            root.editingIndex = -1
            editor.open()
        }
        // A click on a note that is already there. This is the only way into
        // the Edit and Delete paths below: editingIndex was never set to
        // anything but -1 before, so both were unreachable and a note could be
        // added or every note cleared, with nothing in between.
        function onAnnotationPicked(index) {
            root.editingIndex = index
            editor.open()
        }
    }

    // A hint while armed and nothing is being typed, because "Annotate is on"
    // is otherwise invisible until the first click.
    Rectangle {
        visible: root.canvas.annotating && !editor.visible
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 10
        implicitWidth: hint.implicitWidth + 20
        implicitHeight: hint.implicitHeight + 12
        radius: 6
        color: Qt.rgba(Theme.surfaceAlt.r, Theme.surfaceAlt.g, Theme.surfaceAlt.b, 0.94)
        border.color: Theme.accent
        Label {
            id: hint
            anchors.centerIn: parent
            color: Theme.text
            text: root.canvas.annotationCount > 0
                  ? "Click a point to add a note. Click a note to edit or delete it, drag it to move it. Esc to stop."
                  : "Click a point on the figure to add a note. Esc to stop."
        }
    }

    Rectangle {
        id: editor
        visible: false
        function open() {
            visible = true
            field.text = root.editingIndex >= 0
                         ? root.canvas.annotations[root.editingIndex].text : ""
            field.forceActiveFocus()
            field.selectAll()
        }
        function commit() {
            if (root.editingIndex >= 0) root.canvas.updateAnnotation(root.editingIndex, field.text)
            else root.canvas.addAnnotation(root.dataX, root.dataY, field.text)
            close()
        }
        function close() {
            visible = false
            field.text = ""
            root.editingIndex = -1
        }

        anchors.centerIn: parent
        implicitWidth: 380
        implicitHeight: box.implicitHeight + 24
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.accent

        ColumnLayout {
            id: box
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Label {
                Layout.fillWidth: true
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
                text: root.editingIndex >= 0
                      ? "Edit note"
                      : "Note at " + root.dataX.toPrecision(5) + ", " + root.dataY.toPrecision(5)
            }

            TextField {
                id: field
                Layout.fillWidth: true
                placeholderText: "What happened here?"
                onAccepted: editor.commit()
                Keys.onEscapePressed: editor.close()
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    visible: root.editingIndex >= 0
                    text: "Delete"
                    onClicked: {
                        root.canvas.removeAnnotation(root.editingIndex)
                        editor.close()
                    }
                }
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: editor.close() }
                Button {
                    text: root.editingIndex >= 0 ? "Save" : "Add"
                    enabled: field.text.trim().length > 0
                    highlighted: true
                    onClicked: editor.commit()
                }
            }
        }
    }
}
