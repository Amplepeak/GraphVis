// The scrolling frame every sidebar panel uses.
//
// This preamble - ScrollView, clip, the two scrollbar policies, and a
// ColumnLayout bound to availableWidth - was copy-pasted into six panels, four
// of them carrying the same explanatory comment word for word. One copy now.
//
// Content declared inside a PanelScroll goes straight into the column:
//
//     PanelScroll {
//         Label { text: "..." }
//         GvGroupBox { ... }
//     }
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    default property alias content: column.data
    property alias spacing: column.spacing

    clip: true
    // The column binds to availableWidth, so nothing should ever need
    // horizontal scrolling; showing the bar only hid clipped text.
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    ColumnLayout {
        id: column
        width: root.availableWidth
        spacing: 8
    }
}
