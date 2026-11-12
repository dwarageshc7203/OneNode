import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Screen 2: the send page, which has two faces.
//   incomingMode false: pick a device, pick a file, press Send.
//   incomingMode true:  a device we have never talked to wants to send us something (first-time connection):
//                       we are asked once whether to accept it. After that, files from that device just arrive
//                       on the Home page with a toast.
//
// NOTE: pretend data again. The device list will come from discovery later.
Item {
    id: page

    property bool incomingMode: false   // which face is shown
    property int selectedDevice: -1     // index of the chosen device, -1 = none
    property bool fileChosen: false     // pretend: a file has been picked

    signal done()                       // "take me back to Home"
    signal notify(string message)

    ListModel {
        id: devices
        ListElement { name: "Anna's laptop"; address: "192.168.1.40"; kind: "Laptop" }
        ListElement { name: "Pixel 8"; address: "192.168.1.52"; kind: "Phone" }
        ListElement { name: "Studio PC"; address: "192.168.1.61"; kind: "Desktop" }
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
                text: page.incomingMode ? "New device wants to connect" : "Send a file"
                font.pixelSize: Theme.fontHeading
                font.weight: Font.Bold
                color: Theme.text
                wrapMode: Text.Wrap
            }

            // ================= face 1: sending =================
            Card {
                Layout.fillWidth: true
                visible: !page.incomingMode

                Text {
                    text: "1  Choose a device"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    color: Theme.text
                }

                // one selectable row per device. autoExclusive means: selecting one un-selects the others.
                Repeater {
                    model: devices
                    delegate: AbstractButton {
                        id: deviceButton
                        required property int index
                        required property string name
                        required property string address
                        required property string kind

                        Layout.fillWidth: true
                        implicitHeight: 56
                        padding: Theme.space3
                        checkable: true
                        autoExclusive: true
                        onToggled: page.selectedDevice = index // only fires when the USER toggles it

                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: deviceButton.checked ? Theme.accentSoft : (deviceButton.hovered ? Theme.page : Theme.card)
                            border.width: deviceButton.checked ? 2 : 1
                            border.color: deviceButton.checked ? Theme.accent : Theme.border

                            Rectangle { // ring when reached with the keyboard
                                anchors.fill: parent
                                anchors.margins: -3
                                radius: Theme.radiusSmall + 3
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.accent
                                visible: deviceButton.visualFocus
                            }
                        }

                        contentItem: RowLayout {
                            spacing: Theme.space3

                            Rectangle { // the round "radio" mark
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                radius: 10
                                color: "transparent"
                                border.width: 2
                                border.color: deviceButton.checked ? Theme.accent : Theme.border

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: Theme.accent
                                    visible: deviceButton.checked
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text {
                                    Layout.fillWidth: true
                                    text: deviceButton.name
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.Medium
                                    color: Theme.text
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: deviceButton.kind + " · " + deviceButton.address
                                    font.pixelSize: Theme.fontSmall
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }
                }

                Text {
                    Layout.topMargin: Theme.space2
                    text: "2  Choose a file"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    color: Theme.text
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space3

                    AppButton {
                        text: "Choose file…"
                        kind: "outline"
                        onClicked: page.fileChosen = true // pretend
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: page.fileChosen ? "holiday-video-final-edit-v3.mp4 · 935 MB" : "No file chosen"
                        font.pixelSize: Theme.fontBody
                        color: page.fileChosen ? Theme.text : Theme.textMuted
                    }
                }
            }

            AppButton {
                Layout.fillWidth: true
                visible: !page.incomingMode
                text: "Send"
                // only possible when both a device and a file are chosen
                enabled: page.selectedDevice >= 0 && page.fileChosen
                onClicked: {
                    page.notify("Sending to " + devices.get(page.selectedDevice).name)
                    page.done()
                }
            }

            // ================= face 2: first-time receive =================
            Card {
                Layout.fillWidth: true
                visible: page.incomingMode

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Anna's laptop (192.168.1.40) wants to send you photo-from-anna.jpg (4.2 MB)."
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Medium
                    color: Theme.text
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "This device has never connected to yours before. Accept only if you recognise it. "
                        + "After you accept, files from it arrive on the Home page without asking again."
                    font.pixelSize: Theme.fontBody
                    color: Theme.textMuted
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space2
                    spacing: Theme.space3

                    AppButton {
                        text: "Accept"
                        onClicked: {
                            page.incomingMode = false
                            page.notify("Accepted Anna's laptop")
                            page.done()
                        }
                    }
                    AppButton {
                        text: "Decline"
                        kind: "outline"
                        onClicked: {
                            page.incomingMode = false
                            page.notify("Declined Anna's laptop")
                            page.done()
                        }
                    }
                }
            }

            // DEV ONLY: switch between the two faces, so both can be looked at
            AppButton {
                Layout.alignment: Qt.AlignHCenter
                text: page.incomingMode ? "Preview: show the send view" : "Preview: show the first-time receive view"
                kind: "text"
                font.pixelSize: Theme.fontSmall
                onClicked: page.incomingMode = !page.incomingMode
            }
        }
    }
}
