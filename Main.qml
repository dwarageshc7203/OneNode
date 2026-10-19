import QtQuick
import QtQuick.Controls
import OneNode 1.0

Window {
    width: 480
    height: 520
    visible: true
    title: qsTr("One Node")
    property string welcomeMessage: "Welcome to OneNode"
    Text {
        text: "Welcome to OneNode";
        anchors.centerIn: parent;
    }
    Button {
        width: 100;
        height: 100;
        // color: yellow;
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
        }
    }
}
