import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Screen 2: Send. A small form, like addressing a parcel:
//   1. where to send it (the other device's address, shown on ITS Home page)
//   2. what to send (one or more files)
//   then the Send button, which stays greyed out until both are filled in.
// Files can also arrive here from the Home page (drag and drop, or its "Choose files" button).
Item {
    id: page

    required property var core        // the CoreBridge, given by Main.qml
    property var files: []            // the chosen files, as urls
    property string errorText: ""     // a problem to show under the form (empty = none)

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
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Type the address that the other device shows on its Home page, for example 192.168.1.23:45454."
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
                    onTextEdited: page.errorText = "" // the old complaint no longer applies
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
                enabled: addressField.text.trim() !== "" && page.files.length > 0

                function send() {
                    if (!enabled) {
                        return
                    }
                    // the core checks the address and the files; it returns "" when it started the sending
                    const problem = page.core.sendTo(addressField.text, page.files)
                    if (problem !== "") {
                        page.errorText = problem
                        return
                    }
                    const count = page.files.length
                    page.files = [] // the form is ready for the next one; the address stays, you will probably reuse it
                    page.notify(count === 1 ? "Sending 1 file" : "Sending " + count + " files")
                    page.done()
                }
                onClicked: send()
            }
        }
    }
}
