import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One paired device in Settings, with a Remove button that asks twice (the first press turns it into "Sure?").
// "device" is a map from CoreBridge::trustedDevices(): {name, fingerprint, shortFingerprint, pairedAt}
Rectangle {
    id: row

    required property var device
    signal removeRequested(string fingerprint)

    implicitHeight: content.implicitHeight + 2 * Theme.space3
    radius: Theme.radiusSmall
    color: Theme.card
    border.width: 1
    border.color: Theme.border

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.space3
        spacing: Theme.space3

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: row.device.name
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
                color: Theme.text
            }
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: "ID " + row.device.shortFingerprint + "  ·  paired " + Qt.formatDate(new Date(row.device.pairedAt), "d MMM yyyy")
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
            }
        }

        AppButton {
            id: removeButton
            property bool sure: false
            text: sure ? "Sure? Remove" : "Remove"
            kind: "outline"
            onClicked: {
                if (sure) {
                    sure = false
                    row.removeRequested(row.device.fingerprint)
                } else {
                    sure = true
                    sureTimer.restart()
                }
            }
            Timer { id: sureTimer; interval: 3000; onTriggered: removeButton.sure = false }
        }
    }
}
