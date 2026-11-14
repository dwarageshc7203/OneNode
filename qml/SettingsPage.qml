import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Screen 3: Settings. Only what really works is here: the folder where received files are saved.
// More settings come back when the logic behind them exists (device name with discovery, trusted devices with pairing).
Item {
    id: page

    required property var core            // the CoreBridge, given by Main.qml
    property string folder: core.receiveFolder() // the folder shown on screen
    property string errorText: ""

    signal notify(string message)

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
                        + "Transfers are not encrypted yet, so use OneNode only on networks you trust."
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
            }
        }
    }
}
