import QtQuick
import QtQuick.Layouts

// One row of the transfers list: an arrow icon, the file name, who it is with, and a progress bar while it runs.
Rectangle {
    id: row

    // The data of one transfer. A ListView fills these in by itself from the columns ("roles") of its model,
    // matching them by name. "required" means: the row cannot exist without them.
    required property string fileName
    required property string peer       // the other device's name
    required property string direction  // "send" or "receive"
    required property string status     // "active", "done" or "failed"
    required property double done       // bytes so far. "double", not "int": a QML int is 32-bit and overflows at 2 GB
    required property double total      // bytes in the whole file
    required property string error      // the reason, when status is "failed"

    // values worked out from the data above; they update by themselves whenever done or total change
    readonly property double progress: total > 0 ? done / total : 0
    readonly property bool failed: status === "failed"

    // 1536 -> "1.5 KB", 980000000 -> "935 MB"
    function formatBytes(n) {
        if (n < 1024) {
            return n + " B"
        }
        const units = ["KB", "MB", "GB", "TB"]
        let i = -1
        do {
            n = n / 1024
            i++
        } while (n >= 1024 && i < units.length - 1)
        return n.toFixed(n < 10 ? 1 : 0) + " " + units[i]
    }

    implicitHeight: content.implicitHeight + 2 * Theme.space3
    radius: Theme.radius
    color: Theme.card
    border.width: 1
    border.color: Theme.border

    // what a screen reader says for this row
    Accessible.role: Accessible.ListItem
    Accessible.name: fileName + ", " + (direction === "send" ? "sending to " : "receiving from ") + peer + ", " + status

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.space3
        spacing: Theme.space3

        // round icon: arrow up for sending, arrow down for receiving, red when failed
        Rectangle {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            Layout.alignment: Qt.AlignTop
            radius: 20
            color: row.failed ? Theme.dangerSoft : Theme.accentSoft

            Text {
                anchors.centerIn: parent
                text: row.direction === "send" ? "↑" : "↓"
                font.pixelSize: 20
                font.weight: Font.Medium
                color: row.failed ? Theme.danger : Theme.accentText
            }
        }

        // name, details and progress bar, stacked
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: row.fileName
                elide: Text.ElideMiddle // long names are shortened in the middle: "holiday-vi...final.mp4"
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
                color: Theme.text
            }

            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSmall
                color: row.failed ? Theme.danger : Theme.textMuted
                text: row.failed ? row.error
                                 : (row.direction === "send" ? "To " : "From ") + row.peer + " · " + row.formatBytes(row.total)
            }

            // the progress bar: a grey track with a green bar on top whose width is a fraction of the track
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space1
                Layout.preferredHeight: 6
                radius: 3
                color: Theme.track
                visible: row.status === "active"

                Rectangle {
                    width: parent.width * row.progress
                    height: parent.height
                    radius: 3
                    color: Theme.accent
                }
            }
        }

        // status on the right: a percentage while running, then "Done" or "Failed"
        Text {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: Theme.fontBody
            font.weight: Font.Medium
            color: row.failed ? Theme.danger : (row.status === "done" ? Theme.accentText : Theme.text)
            text: row.status === "active" ? Math.round(row.progress * 100) + "%"
                : row.status === "done" ? "Done" : "Failed"
        }
    }
}
