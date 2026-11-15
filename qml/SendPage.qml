import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Screen 2: Send. A small form, like addressing a parcel:
//   1. where to send it (a device from the nearby list, or an address typed in)
//   2. what to send (one or more files)
//   then the Send button, which stays greyed out until both are filled in.
// Files can also arrive here from the Home page (drag and drop, or its "Choose files" button).
Item {
    id: page

    required property var core        // the CoreBridge, given by Main.qml
    property var files: []            // the chosen files, as urls
    property string errorText: ""     // a problem to show under the form (empty = none)
    property var nearby: []           // devices found on the network (CoreBridge::nearbyDevices)
    property string selectedFp: ""    // the fingerprint of the nearby device that was clicked ("" = none)
    readonly property bool hasTarget: selectedFp !== "" || addressField.text.trim() !== ""

    function refreshNearby() {
        nearby = core.nearbyDevices()
        // a device that went away can no longer be the target
        if (selectedFp !== "" && !nearby.some(d => d.fingerprint === selectedFp)) {
            selectedFp = ""
        }
    }
    Component.onCompleted: refreshNearby()
    Connections {
        target: page.core
        function onNearbyDevicesChanged() { page.refreshNearby() }
    }

    signal done()                     // "take me back to Home"
    signal notify(string message)

    // called from outside (Main.qml) when the Home page's "Choose files" is pressed
    function chooseFiles() {
        fileDialog.open()
    }

    // "file:///home/me/My%20Photo.jpg" -> "My Photo.jpg"
    function nameOf(url) {
        return decodeURIComponent(url.toString().split("/").pop())
    }

    FileDialog {
        id: fileDialog
        title: "Choose files to send"
        fileMode: FileDialog.OpenFiles // several files at once
        onAccepted: {
            page.files = selectedFiles
            page.errorText = ""
        }
    }

    // The page can be taller than a short window, so it scrolls
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
                text: "Send files"
                font.pixelSize: Theme.fontHeading
                font.weight: Font.Bold
                color: Theme.text
            }

            // ---- 1. where ----
            Card {
                Layout.fillWidth: true

                Text {
                    text: "1  Where to send"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    color: Theme.text
                }

                // ---- devices found on the network ----
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Nearby devices"
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
                Repeater {
                    model: page.nearby
                    delegate: NearbyRow {
                        required property var modelData
                        Layout.fillWidth: true
                        device: modelData
                        selected: page.selectedFp === modelData.fingerprint
                        onClicked: {
                            page.selectedFp = selected ? "" : modelData.fingerprint
                            if (page.selectedFp !== "") {
                                addressField.text = "" // one target at a time
                            }
                            page.errorText = ""
                        }
                    }
                }
                Text {
                    objectName: "nearbyEmpty"
                    Layout.fillWidth: true
                    visible: page.nearby.length === 0
                    wrapMode: Text.Wrap
                    text: "No other device found yet. Both devices need OneNode open on the same Wi-Fi or network."
                    font.pixelSize: Theme.fontBody
                    color: Theme.textMuted
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.nearby.length > 0
                    wrapMode: Text.Wrap
                    text: "Not paired yet? On that device press \u201cPair a new device\u201d on Home, pick it here, and type the PIN it shows."
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }

                // ---- or an address typed in ----
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Or type the address shown on the other device\u2019s Home page"
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
                TextField {
                    id: addressField
                    objectName: "addressField"
                    Layout.fillWidth: true
                    implicitHeight: 40
                    placeholderText: "192.168.1.23:45454"
                    font.pixelSize: Theme.fontBody
                    color: Theme.text
                    placeholderTextColor: Theme.textMuted
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.textOnAccent
                    leftPadding: Theme.space3
                    rightPadding: Theme.space3
                    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    onTextEdited: {
                        page.errorText = ""   // the old complaint no longer applies
                        page.selectedFp = ""  // one target at a time
                    }
                    onAccepted: sendButton.send()     // the Enter key
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.card
                        border.width: addressField.activeFocus ? 2 : 1
                        border.color: addressField.activeFocus ? Theme.accent : Theme.border
                    }
                }
            }

            // ---- 2. what ----
            Card {
                Layout.fillWidth: true

                Text {
                    text: "2  Files"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    color: Theme.text
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space3

                    AppButton {
                        text: page.files.length > 0 ? "Choose other files…" : "Choose files…"
                        kind: "outline"
                        onClicked: fileDialog.open()
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: page.files.length === 0 ? "No files chosen"
                            : page.files.length === 1 ? "1 file"
                            : page.files.length + " files"
                        font.pixelSize: Theme.fontBody
                        color: page.files.length > 0 ? Theme.text : Theme.textMuted
                    }
                }

                // the chosen files, one line each
                Repeater {
                    model: page.files
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: page.nameOf(modelData)
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                }
            }

            // a problem with the form, in red
            Text {
                Layout.fillWidth: true
                visible: page.errorText !== ""
                wrapMode: Text.Wrap
                text: page.errorText
                font.pixelSize: Theme.fontBody
                color: Theme.danger
            }

            AppButton {
                id: sendButton
                objectName: "sendButton"
                Layout.fillWidth: true
                text: "Send"
                // only possible when both parts are filled in
                enabled: page.hasTarget && page.files.length > 0

                function send() {
                    if (!enabled) {
                        return
                    }
                    // the core checks the address and the files; it returns "" when it started the sending
                    const problem = page.selectedFp !== ""
                        ? page.core.sendToNearby(page.selectedFp, page.files)
                        : page.core.sendTo(addressField.text, page.files)
                    if (problem !== "") {
                        page.errorText = problem
                        return
                    }
                    const count = page.files.length
                    page.files = [] // the form is ready for the next one; the target stays, you will probably reuse it
                    page.notify(count === 1 ? "Sending 1 file" : "Sending " + count + " files")
                    page.done()
                }
                onClicked: send()
            }
        }
    }
}
