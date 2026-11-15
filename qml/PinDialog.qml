import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The sender's side of pairing: "type the 6-digit PIN that the other device shows".
// It opens by itself when the core says pinNeeded (see Main.qml). If several devices ask at once, they wait in a line.
Popup {
    id: dialog

    required property var core        // the CoreBridge, given by Main.qml

    property var waiting: []          // [{id, peerName}, ...] the requests that have not been shown yet
    property double currentId: 0      // the send that this dialog is asking about
    property string peerName: ""
    property bool answered: false     // true once a PIN was accepted: closing is then not a cancel

    // Called by Main.qml for every pinNeeded signal.
    function ask(id, name) {
        if (visible) {
            waiting.push({ id: id, peerName: name })
        } else {
            show(id, name)
        }
    }

    // Called by Main.qml when a send ended (failed) while its request is on screen or in the line.
    function forget(id) {
        waiting = waiting.filter(w => w.id !== id)
        if (visible && currentId === id) {
            answered = true // nothing to cancel any more
            close()
        }
    }

    function show(id, name) {
        currentId = id
        peerName = name
        answered = false
        pinField.text = ""
        errorLabel.text = ""
        open()
        pinField.forceActiveFocus()
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

    onClosed: {
        if (!answered) {
            core.cancelPin(currentId) // the user gave up: the send stops, the other device's PIN stays alive
        }
        if (waiting.length > 0) {
            const next = waiting.shift()
            waiting = waiting
            show(next.id, next.peerName)
        }
    }

    function submit() {
        const problem = core.submitPin(currentId, pinField.text)
        if (problem !== "") {
            errorLabel.text = problem
            return
        }
        answered = true
        close()
    }

    contentItem: ColumnLayout {
        spacing: Theme.space3

        Text {
            Layout.fillWidth: true
            text: "Pair with " + dialog.peerName
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
            color: Theme.text
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "This is the first time you send to this device. On " + dialog.peerName
                + ", open Home and press “Pair a new device”, then type the 6-digit PIN it shows."
            font.pixelSize: Theme.fontBody
            color: Theme.textMuted
        }

        TextField {
            id: pinField
            objectName: "pinField"
            Layout.fillWidth: true
            implicitHeight: 56
            horizontalAlignment: Text.AlignHCenter
            placeholderText: "000 000"
            font.pixelSize: 28
            font.weight: Font.DemiBold
            font.letterSpacing: 3
            color: Theme.text
            placeholderTextColor: "#B4C2B9"
            selectionColor: Theme.accent
            selectedTextColor: Theme.textOnAccent
            maximumLength: 7 // 6 digits and the space in the middle
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhNoPredictiveText
            validator: RegularExpressionValidator { regularExpression: /^[0-9 ]*$/ }
            onTextEdited: errorLabel.text = ""
            onAccepted: dialog.submit()
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.card
                border.width: pinField.activeFocus ? 2 : 1
                border.color: pinField.activeFocus ? Theme.accent : Theme.border
            }
        }

        Text {
            id: errorLabel
            objectName: "pinError"
            Layout.fillWidth: true
            visible: text !== ""
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.danger
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3

            Item { Layout.fillWidth: true }
            AppButton {
                text: "Cancel"
                kind: "outline"
                onClicked: dialog.close()
            }
            AppButton {
                objectName: "pinSubmit"
                text: "Pair"
                enabled: pinField.text.replace(/ /g, "").length === 6
                onClicked: dialog.submit()
            }
        }
    }
}
