import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Screen 3: Settings. For now the values are pretend and are not saved anywhere;
// saving them (QSettings) comes later.
Item {
    id: page

    signal notify(string message)

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
                    text: "The name other devices see when you connect"
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    implicitHeight: 40
                    text: "Victus"
                    font.pixelSize: Theme.fontBody
                    color: Theme.text
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.textOnAccent
                    leftPadding: Theme.space3
                    rightPadding: Theme.space3
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.card
                        border.width: nameField.activeFocus ? 2 : 1
                        border.color: nameField.activeFocus ? Theme.accent : Theme.border
                    }
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
                        text: "~/Downloads/OneNode"
                        font.pixelSize: Theme.fontBody
                        color: Theme.text
                    }
                    AppButton {
                        text: "Change…"
                        kind: "outline"
                        onClicked: page.notify("The folder picker comes in a later step")
                    }
                }
                AppSwitch {
                    Layout.fillWidth: true
                    text: "Show a notification when a file arrives"
                    checked: true
                }
                AppSwitch {
                    Layout.fillWidth: true
                    text: "Ask before accepting a device for the first time"
                    checked: true
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
