import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OneNode 1.0

// The application window: a top bar with navigation, and below it the three screens (Home, Send, Settings),
// of which exactly one is visible at a time. Toast messages appear on top of whichever one it is.
ApplicationWindow {
    id: window

    width: 960
    height: 680
    minimumWidth: 400
    minimumHeight: 560
    visible: true
    title: qsTr("OneNode")
    color: Theme.page

    // which screen is shown: 0 = Home, 1 = Send, 2 = Settings
    property int page: 0

    // ---- the real core ----
    // For now it only listens for incoming files and writes what happens to the Qt Creator output.
    // The screens still show pretend data; connecting them to the core is the next step.
    CoreBridge { id: core }
    Component.onCompleted: core.startListening()

    Connections {
        target: core

        function onTransferStarted(id, name, direction, total) {
            console.log("Started #", id, ": ", name, " | direction: ", direction, " | size: ", total);
        }

        function onTransferProgress(id, name, done, total) {
            console.log("Progress #", id, ": ", name, " : ", done, " / ", total);
        }

        function onTransferFinished(id, name) {
            console.log("Finished #", id, ": ", name);
        }

        function onTransferFailed(id, name, reason) {
            console.log("Failed #", id, ": ", name, " | reason: ", reason);
        }
    }

    // ---- top bar ----
    header: Rectangle {
        height: 56
        color: Theme.card

        Rectangle { // thin line along the bottom edge
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space4
            anchors.rightMargin: Theme.space3
            spacing: Theme.space1

            Rectangle { // placeholder logo: replace with the real one later
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: 8
                color: Theme.accent

                Text {
                    anchors.centerIn: parent
                    text: "O"
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    color: Theme.textOnAccent
                }
            }
            Text {
                Layout.leftMargin: Theme.space2
                text: "OneNode"
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                color: Theme.text
            }

            Item { Layout.fillWidth: true } // pushes the buttons to the right

            AppButton { text: "Home"; kind: "text"; active: window.page === 0; onClicked: window.page = 0 }
            AppButton { text: "Send"; kind: "text"; active: window.page === 1; onClicked: window.page = 1 }
            AppButton { text: "Settings"; kind: "text"; active: window.page === 2; onClicked: window.page = 2 }
        }
    }

    // ---- the three screens ----
    StackLayout {
        anchors.fill: parent
        currentIndex: window.page

        HomePage {
            objectName: "homePage"
            onSendRequested: window.page = 1
            onNotify: (message) => toast.show(message)
        }
        SendPage {
            objectName: "sendPage"
            onDone: window.page = 0
            onNotify: (message) => toast.show(message)
        }
        SettingsPage {
            onNotify: (message) => toast.show(message)
        }
    }

    // declared after the screens, so it is drawn on top of them
    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space5
    }
}
