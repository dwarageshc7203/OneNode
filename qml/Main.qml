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
    // main.cpp creates the core (CoreBridge) and the transfer list (TransferModel) and hands them to this window.
    // "required" means: the window refuses to start without them.
    required property var core
    required property var transfers

    property bool listening: false // did the core manage to open its port?
    property string address: ""    // "192.168.1.23:45454": what the other device must type in

    Component.onCompleted: {
        listening = core.startListening()
        address = (core.localAddress() || "no network") + ":" + core.listeningPort()
    }

    // The toast ("notification") for files that arrive. Sending shows no toast: you started it yourself.
    // The transfer list itself is NOT updated here: TransferModel listens to the core by itself.
    property var receiving: ({}) // ids of the transfers that are coming IN, so "finished" can tell which kind it was

    Connections {
        target: window.core

        function onTransferStarted(id, name, direction, total, peer) {
            if (direction === CoreBridge.Receive) {
                window.receiving[id] = true
                toast.show(peer + " is sending " + name)
            }
        }

        function onTransferFinished(id, name) {
            if (window.receiving[id]) {
                delete window.receiving[id]
                toast.show("Received " + name)
            }
        }

        function onTransferFailed(id, name, reason) {
            delete window.receiving[id]
            toast.show("Failed: " + name + " (" + reason + ")")
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
            transfers: window.transfers
            listening: window.listening
            address: window.address
            onSendRequested: {
                window.page = 1
                sendPage.chooseFiles()
            }
            onFilesDropped: (urls) => {
                sendPage.files = urls // the files wait on the Send page until you type where to send them
                sendPage.errorText = ""
                window.page = 1
            }
        }
        SendPage {
            id: sendPage
            objectName: "sendPage"
            core: window.core
            onDone: window.page = 0
            onNotify: (message) => toast.show(message)
        }
        SettingsPage {
            core: window.core
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
