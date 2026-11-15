import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One device of the nearby list. Click it to choose it as the place to send to.
// "device" is a map from CoreBridge::nearbyDevices(): {name, fingerprint, shortFingerprint, address, port, trusted, pairing}
Rectangle {
    id: row

    required property var device
    property bool selected: false
    signal clicked()

    implicitHeight: content.implicitHeight + 2 * Theme.space3
    radius: Theme.radiusSmall
    color: selected ? Theme.accentSoft : (mouse.containsMouse ? Theme.page : Theme.card)
    border.width: selected ? 2 : 1
    border.color: selected ? Theme.accent : Theme.border

    Accessible.role: Accessible.RadioButton
    Accessible.name: device.name
    Accessible.checked: selected
    Accessible.onPressAction: row.clicked()
    activeFocusOnTab: true
    Keys.onSpacePressed: row.clicked()
    Keys.onReturnPressed: row.clicked()

    // a ring when reached with the keyboard
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: Theme.radiusSmall + 3
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        visible: row.activeFocus && !mouse.containsMouse
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.clicked()
    }

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
                text: row.device.address + "  ·  ID " + row.device.shortFingerprint
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
            }
        }

        // what the list knows about it
        Rectangle {
            implicitWidth: badge.implicitWidth + 2 * Theme.space2
            implicitHeight: badge.implicitHeight + 6
            radius: height / 2
            color: row.device.trusted ? Theme.accentSoft : (row.device.pairing ? Theme.accent : Theme.page)
            border.width: row.device.trusted || row.device.pairing ? 0 : 1
            border.color: Theme.border

            Text {
                id: badge
                anchors.centerIn: parent
                text: row.device.trusted ? "Paired" : (row.device.pairing ? "Ready to pair" : "Not paired")
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Medium
                color: row.device.trusted ? Theme.accentText : (row.device.pairing ? Theme.textOnAccent : Theme.textMuted)
            }
        }
    }
}
