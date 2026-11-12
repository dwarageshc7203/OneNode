import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Screen 1: Home. Shows whether this device is ready to receive, a place to drop files to send,
// and the list of transfers. Files that arrive from other devices show up here as new rows plus a toast,
// so no extra "receiver" screen is needed after the first connection.
//
// NOTE: everything on this page is PRETEND DATA for now. The real core will replace it in the next step.
Item {
    id: page

    // the page does not know about other pages: it only announces what the user wants, and Main.qml reacts
    signal sendRequested()
    signal notify(string message)

    // pretend values (later they come from the core)
    property string deviceName: "Victus"
    property string address: "192.168.1.23:45454"

    // pretend transfers. Each ListElement is one row; its names must match the "required property" names in TransferRow.
    ListModel {
        id: transfers
        ListElement { fileName: "holiday-video-final-edit-v3.mp4"; peer: "Anna's laptop"; direction: "receive"; status: "active"; done: 412000000; total: 980000000; error: "" }
        ListElement { fileName: "thesis-final.pdf"; peer: "Pixel 8"; direction: "send"; status: "done"; done: 8400000; total: 8400000; error: "" }
        ListElement { fileName: "notes.txt"; peer: "Anna's laptop"; direction: "send"; status: "failed"; done: 0; total: 1200; error: "Connection refused" }
    }

    // DEV ONLY: pretends that a device sends us a file, to see the row and the toast. Remove when the core is connected.
    function simulateIncoming() {
        if (simTimer.running) {
            return
        }
        transfers.insert(0, { fileName: "photo-from-anna.jpg", peer: "Anna's laptop", direction: "receive",
                              status: "active", done: 0, total: 4200000, error: "" })
        page.notify("Anna's laptop is sending photo-from-anna.jpg")
        simTimer.start()
    }

    Timer {
        id: simTimer
        interval: 120
        repeat: true
        onTriggered: {
            const row = transfers.get(0)
            const next = row.done + 350000
            if (next >= row.total) {
                transfers.setProperty(0, "done", row.total)
                transfers.setProperty(0, "status", "done")
                stop()
                page.notify("Received photo-from-anna.jpg")
            } else {
                transfers.setProperty(0, "done", next)
            }
        }
    }

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

                Rectangle { // the green dot
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    radius: 5
                    color: Theme.accent
                }
                Text {
                    text: "Ready to receive"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Medium
                    color: Theme.text
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    text: page.deviceName + " · " + page.address
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
            AppButton { // DEV ONLY
                text: "Simulate incoming file"
                kind: "outline"
                implicitHeight: 32
                font.pixelSize: Theme.fontSmall
                onClicked: page.simulateIncoming()
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
                model: transfers
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
