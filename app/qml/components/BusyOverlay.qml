// What a long job looks like while it is running, and when it has finished.
//
// Reported after clicking "Extract figures and tables": no indication of it
// loading, and no indication of it being done. Both halves of that matter and
// they are different failures. A program with no progress cannot be told apart
// from a program that has hung - so the person waits, then clicks again, then
// gives up. A program with no completion signal makes them keep checking.
//
// Extraction of a scanned paper is minutes of OCR. Minutes is far past the
// point where a static window reads as broken.
//
// Three things, in the order they answer the question "is it working":
//
//   the mark      something moving, so the window is visibly alive even when
//                 the step underneath cannot say how far along it is
//   the step      what it is DOING - "running OCR", "parsing tables" - which
//                 is what tells someone whether four minutes is reasonable
//   the clock     how long it has been, which is the only honest measure of
//                 progress on a step with no percentage
//
// The bar is determinate only when the service actually said a number. A bar
// that invents its own progress is worse than an indeterminate one, because a
// person calibrates their patience against it and then it lies.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GraphVis

Item {
    id: root
    required property var app

    // What finished, said in the caller's own words - "3 figures and 2 tables"
    // rather than a generic "Done", because the useful question after an
    // extraction is what came out of it.
    property string doneSummary: ""
    // Whether this overlay's operation is the one running. The controller has
    // one busy flag for every science job, so a panel that showed it whenever
    // anything was busy would light up during an unrelated import.
    property bool active: false

    readonly property bool working: root.active && root.app.busy
    readonly property real percent: root.app.busyPercent
    readonly property bool determinate: root.percent >= 0

    // Elapsed, in words. A ticking number is also the thing that makes an
    // indeterminate wait feel finite.
    property string elapsedText: ""
    Timer {
        running: root.working
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            const started = root.app.busySince
            if (!started || isNaN(started.getTime())) { root.elapsedText = ""; return }
            const s = Math.max(0, Math.floor((Date.now() - started.getTime()) / 1000))
            root.elapsedText = s < 60
                ? s + "s"
                : Math.floor(s / 60) + "m " + (s % 60 < 10 ? "0" : "") + (s % 60) + "s"
        }
    }

    visible: root.working || root.doneSummary !== ""

    // FINISHED is a banner, not a curtain.
    //
    // The first version dimmed the whole pane for both states, which meant the
    // reward for a four-minute extraction was a screen covering the figures it
    // had just found. What a person wants at that moment is to see them - so
    // the completion notice takes a strip along the top and the work it
    // announces is visible underneath it immediately.
    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
        height: doneRow.implicitHeight + 16
        visible: !root.working && root.doneSummary !== ""
        radius: Theme.radius
        color: Qt.rgba(Theme.positive.r, Theme.positive.g, Theme.positive.b, 0.13)
        border.color: Theme.positive

        RowLayout {
            id: doneRow
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 10
            Label {
                text: "✓"
                color: Theme.positive
                font.pixelSize: 18
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: root.doneSummary
                color: Theme.text
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            ToolButton {
                text: "✕"
                flat: true
                implicitWidth: 26
                onClicked: root.doneSummary = ""
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.working
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.92)

        // Nothing underneath is clickable while a job is running: every control
        // there acts on the thing being built, and the controller would refuse
        // them anyway with "a job is already running", which reads as an error
        // rather than as an explanation.
        MouseArea { anchors.fill: parent; enabled: root.working }

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 420)
            spacing: 14

            // The mark. It turns while the work runs and stops when it does, so
            // "is it still going" is answered from across the room.
            Item {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 64
                implicitHeight: 64

                Image {
                    id: mark
                    anchors.fill: parent
                    source: "qrc:/qt/qml/GraphVis/assets/graphvis_icon.png"
                    sourceSize.width: 64
                    sourceSize.height: 64
                    fillMode: Image.PreserveAspectFit
                    opacity: root.working ? 1.0 : 0.9
                }

                // A ring around it rather than a spinner replacing it: the mark
                // says which program is busy, which matters when the window is
                // one of several on screen.
                Rectangle {
                    anchors.centerIn: parent
                    width: 76; height: 76
                    radius: 38
                    color: "transparent"
                    border.color: Theme.accent
                    border.width: 2
                    opacity: 0.30
                    visible: root.working
                }
                Rectangle {
                    id: sweep
                    anchors.centerIn: parent
                    width: 76; height: 76
                    radius: 38
                    color: "transparent"
                    border.color: Theme.accent
                    border.width: 2
                    visible: root.working
                    // A quarter of the ring, turning. Drawn by clipping the
                    // circle with a rotating opacity mask would need a shader;
                    // a rotating dot on the ring says the same thing and costs
                    // one item.
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: Theme.accent
                        x: parent.width / 2 - 4
                        y: -4
                    }
                    RotationAnimation on rotation {
                        running: root.working
                        loops: Animation.Infinite
                        from: 0; to: 360
                        duration: 1400
                    }
                }

            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.app.busyLabel === "" ? "Working…" : root.app.busyLabel
                color: Theme.text
                font.pixelSize: 15
            }

            ProgressBar {
                Layout.fillWidth: true
                indeterminate: !root.determinate
                from: 0; to: 100
                value: root.determinate ? root.percent : 0
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: (root.determinate ? Math.round(root.percent) + "%  ·  " : "")
                      + (root.elapsedText === "" ? "" : root.elapsedText + " so far")
                color: Theme.textMuted
                font.pixelSize: 11
            }

            // Said once, while it is slow, rather than as a permanent notice.
            // A scanned paper with no text layer goes through OCR and that is
            // genuinely minutes - a person who knows that waits, and a person
            // who does not assumes it has crashed.
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: root.app.busyLabel.indexOf("OCR") >= 0
                text: "A scanned page has no text layer, so every page is being read "
                    + "as an image. This is the slow part."
                color: Theme.textMuted
                font.pixelSize: 10
            }

        }
    }
}
