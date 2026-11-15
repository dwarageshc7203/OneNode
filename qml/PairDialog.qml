import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The receiver's side of pairing: shows the PIN for a new device to type in, with a countdown.
// "Pair a new device" on the Home page calls start(). The core ends pairing mode by itself when the PIN is used,
// fails, or runs out; this dialog then closes (Main.qml shows what happened as a toast).
Popup {
    id: dialog

    required property var core        // the CoreBridge, given by Main.qml

    property string pin: ""           // "482 913"
    property double deadline: 0       // when the PIN runs out (milliseconds since 1970)
    property int secondsLeft: 0

    // Opens pairing mode in the core. Returns false when that is not possible (no identity).
    function start() {
        pin = ""
        core.startPairing()
        return core.pairingActive()
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 2 * Theme.space5, 420)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: Theme.space5

    background: Rectangle {
        radius: Theme.radius
        color: Theme.card
        border.width: 1
        border.color: Theme.border
    }

    Connections {
        target: dialog.core

        function onPairingStarted(newPin) {
            dialog.pin = newPin
            dialog.deadline = Date.now() + dialog.core.pairingSeconds() * 1000
            dialog.secondsLeft = dialog.core.pairingSeconds()
            dialog.open()
        }
        function onPairingEnded(reason) {
            dialog.close()
        }
    }

    // pressing Esc or Cancel closes pairing mode, so a forgotten PIN does not stay valid
    onClosed: {
        if (core.pairingActive()) {
            core.cancelPairing()
        }
    }

    Timer {
        interval: 250
        repeat: true
        running: dialog.visible
        onTriggered: dialog.secondsLeft = Math.max(0, Math.ceil((dialog.deadline - Date.now()) / 1000))
    }

    contentItem: ColumnLayout {
        spacing: Theme.space3

        Text {
            Layout.fillWidth: true
            text: "Pair a new device"
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
            color: Theme.text
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "On the other device, choose Send, pick this device, and type this PIN. "
                + "The PIN works once. Only do this when you are the one who started it."
            font.pixelSize: Theme.fontBody
            color: Theme.textMuted
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 84
            radius: Theme.radiusSmall
            color: Theme.accentSoft

            Text {
                objectName: "pairPin"
                anchors.centerIn: parent
                text: dialog.pin
                font.pixelSize: 44
                font.weight: Font.Bold
                font.letterSpacing: 4
                color: Theme.accentText
            }
        }

        // the time that is left
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 6
            radius: 3
            color: Theme.track

            Rectangle {
                width: parent.width * Math.min(1, dialog.secondsLeft / Math.max(1, dialog.core.pairingSeconds()))
                height: parent.height
                radius: 3
                color: dialog.secondsLeft <= 20 ? Theme.danger : Theme.accent
            }
        }
        Text {
            objectName: "pairCountdown"
            Layout.fillWidth: true
            text: "Expires in " + Math.floor(dialog.secondsLeft / 60) + ":" + String(dialog.secondsLeft % 60).padStart(2, "0")
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton {
                text: "Cancel"
                kind: "outline"
                onClicked: dialog.close()
            }
        }
    }
}
