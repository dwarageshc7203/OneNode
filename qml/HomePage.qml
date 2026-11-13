import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Screen 1: Home. Shows whether this device is ready to receive, a place to drop files to send,
// and the list of transfers. Files that arrive from other devices show up here as new rows plus a toast,
// so no extra "receiver" screen is needed after the first connection.
//
// The transfer list is real: it comes from TransferModel (see main.cpp). The drop zone and "Choose files" still lead
// to the pretend Send page: they become real in the next step.
Item {
    id: page

    // the page does not know about other pages: it only announces what the user wants, and Main.qml reacts
    signal sendRequested()
    signal notify(string message)

    // given by Main.qml
    required property var transfers // the list of transfers (a TransferModel)
    property bool listening: false  // is the core waiting for incoming files?
    property string address: ""     // "192.168.1.23:45454": what the other device must type in

    // The content column. On a wide window it stops growing at Theme.maxContentWidth and stays centered;
    // on a narrow window it shrinks to fit, keeping a margin at each side.
    ColumnLayout {
        width: Math.min(page.width - 2 * Theme.space5, Theme.maxContentWidth)
        height: page.height - 2 * Theme.space5
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.space5
        spacing: Theme.space4

        // ---- status: is this device ready to receive? ----
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space3

                Rectangle { // green dot when listening, red when the port could not be opened
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    radius: 5
                    color: page.listening ? Theme.accent : Theme.danger
                }
                Text {
                    text: page.listening ? "Ready to receive" : "Not receiving: the port is already in use"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Medium
                    color: Theme.text
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    visible: page.listening
                    text: "Your address: " + page.address
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
            }
        }

        // ---- drop zone: drag files from the file manager onto it, or click the button ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: Theme.radius
            color: dropArea.containsDrag ? Theme.accentSoft : Theme.card
            border.width: dropArea.containsDrag ? 2 : 1
            border.color: dropArea.containsDrag ? Theme.accent : Theme.border

            DropArea {
                id: dropArea
                anchors.fill: parent
                onDropped: (drop) => {
                    if (drop.hasUrls) {
                        page.notify(drop.urls.length + " file(s) dropped")
                        page.sendRequested() // go to the Send page to choose the device
                    }
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.space2

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: dropArea.containsDrag ? "Release to send" : "Drop files here to send"
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Medium
                    color: Theme.text
                }
                AppButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Choose files"
                    onClicked: page.sendRequested()
                }
            }
        }

        // ---- transfers ----
        RowLayout {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: "Transfers"
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                color: Theme.text
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                spacing: Theme.space2
                model: page.transfers
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                delegate: TransferRow {
                    width: ListView.view.width
                }
            }

            // the "nothing here yet" state
            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "No transfers yet"
                font.pixelSize: Theme.fontBody
                color: Theme.textMuted
            }
        }
    }
}
