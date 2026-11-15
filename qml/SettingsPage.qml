import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Screen 3: Settings. Only what really works is here: this device's name, where received files are saved,
// and the list of paired devices (each can be removed, and then has to be paired again).
Item {
    id: page

    required property var core            // the CoreBridge, given by Main.qml
    property string folder: core.receiveFolder() // the folder shown on screen
    property string errorText: ""
    property string nameError: ""
    property var paired: core.trustedDevices()   // the paired devices (CoreBridge::trustedDevices)

    signal notify(string message)

    Connections {
        target: page.core
        function onTrustedDevicesChanged() { page.paired = page.core.trustedDevices() }
    }

    FolderDialog {
        id: folderDialog
        title: "Choose where received files are saved"
        currentFolder: page.core.receiveFolderUrl() // the dialog opens in the current folder
        onAccepted: {
            const problem = page.core.changeReceiveFolder(selectedFolder) // "" means it worked
            page.errorText = problem
            page.folder = page.core.receiveFolder()
            if (problem === "") {
                page.notify("Received files will now be saved to " + page.folder)
            }
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentHeight: column.implicitHeight + 2 * Theme.space5
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: column
            width: Math.min(scroller.width - 2 * Theme.space5, Theme.maxContentWidth)
            x: (scroller.width - width) / 2
            y: Theme.space5
            spacing: Theme.space4

            Text {
                Layout.fillWidth: true
                text: "Settings"
                font.pixelSize: Theme.fontHeading
                font.weight: Font.Bold
                color: Theme.text
            }

            // ---- this device ----
            Card {
                Layout.fillWidth: true

                Text {
                    text: "This device"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    color: Theme.text
                }
                Text {
                    text: "Name shown to other devices"
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space3

                    TextField {
                        id: nameField
                        objectName: "nameField"
                        Layout.fillWidth: true
                        implicitHeight: 40
                        text: page.core.deviceName()
                        maximumLength: 64
                        font.pixelSize: Theme.fontBody
                        color: Theme.text
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.textOnAccent
                        leftPadding: Theme.space3
                        rightPadding: Theme.space3
                        onTextEdited: page.nameError = ""
                        onAccepted: saveName.save()
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.card
                            border.width: nameField.activeFocus ? 2 : 1
                            border.color: nameField.activeFocus ? Theme.accent : Theme.border
                        }
                    }
                    AppButton {
                        id: saveName
                        text: "Save"
                        kind: "outline"
                        enabled: nameField.text.trim() !== "" && nameField.text.trim() !== page.core.deviceName()
                        function save() {
                            if (!enabled) { return }
                            page.nameError = page.core.changeDeviceName(nameField.text)
                            nameField.text = page.core.deviceName() // shows the cleaned name
                            if (page.nameError === "") {
                                page.notify("Device name saved")
                            }
                        }
                        onClicked: save()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.nameError !== ""
                    wrapMode: Text.Wrap
                    text: page.nameError
                    font.pixelSize: Theme.fontSmall
                    color: Theme.danger
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: "Device ID  " + page.core.myFingerprint().substring(0, 8) + "\u2026" + page.core.myFingerprint().slice(-4)
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
            }

            // ---- paired devices ----
            Card {
                Layout.fillWidth: true

                Text {
                    text: "Paired devices"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    color: Theme.text
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "These devices can send files to this one without asking. Remove one and it has to be paired again."
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
                Repeater {
                    model: page.paired
                    delegate: PairedRow {
                        required property var modelData
                        Layout.fillWidth: true
                        device: modelData
                        onRemoveRequested: (fingerprint) => {
                            const problem = page.core.removeTrustedDevice(fingerprint)
                            if (problem !== "") {
                                page.notify(problem)
                            } else {
                                page.notify("Removed " + modelData.name)
                            }
                        }
                    }
                }
                Text {
                    objectName: "pairedEmpty"
                    Layout.fillWidth: true
                    visible: page.paired.length === 0
                    wrapMode: Text.Wrap
                    text: "No paired devices yet."
                    font.pixelSize: Theme.fontBody
                    color: Theme.textMuted
                }
            }

            // ---- receiving ----
            Card {
                Layout.fillWidth: true

                Text {
                    text: "Receiving"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    color: Theme.text
                }
                Text {
                    text: "Received files are saved to"
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space3

                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: page.folder
                        font.pixelSize: Theme.fontBody
                        color: Theme.text
                    }
                    AppButton {
                        text: "Change…"
                        kind: "outline"
                        onClicked: folderDialog.open()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.errorText !== ""
                    wrapMode: Text.Wrap
                    text: page.errorText
                    font.pixelSize: Theme.fontSmall
                    color: Theme.danger
                }
            }

            // ---- about ----
            Card {
                Layout.fillWidth: true

                Text {
                    text: "About"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    color: Theme.text
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "OneNode 0.1 (early development)"
                    font.pixelSize: Theme.fontBody
                    color: Theme.text
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Files go straight between devices on your network, with no server in the middle. "
                        + "Every transfer is encrypted (TLS 1.3), and a new device must be paired once with a PIN before it can send you anything."
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
            }
        }
    }
}
