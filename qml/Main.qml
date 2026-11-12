import QtQuick
import QtQuick.Controls
import OneNode 1.0

Window {
    width: 480
    height: 520
    visible: true
    title: qsTr("One Node")
    property string welcomeMessage: "Welcome to OneNode"

    Connections {

        target: core
        function onTransferStarted(id, name, direction, total) {
            console.log("Started #", id, ": ", name, " | direction: ", direction, " | size: ", total);
        }
        function onTransferProgress(id, name, done, total) {
            console.log("Progress #", id, ": ", name, " : ", done, " / ", total );
        }
        function onTransferFinished(id, name) {
            console.log("Finished #", id, ": ", name);
        }
        function onTransferFailed(id, name, reason) {
            console.log("Failed #", id, ": ", name, " | reason: ", reason);
        }

    }

    Text {
        text: "Welcome to OneNode";
        anchors.centerIn: parent;
    }
    Button {
        width: 100;
        height: 100;
        text: "Start OneNode";
        anchors.centerIn: parent;
        onClicked: {
            console.log("Button was clicked")
        }
    }
    CoreBridge {
        id: core
    }
    Button {
        text: "Start Listening"
        onClicked: {
            let success = core.startListening();
            console.log("Server started: ", success);
            core.sendFile("127.0.0.1", "/tmp/big.bin");
        }
    }
}
