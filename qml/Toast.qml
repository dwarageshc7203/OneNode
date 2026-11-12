import QtQuick

// A small dark message that fades in at the bottom of the window, stays a few seconds, then fades out.
// This is how the app tells you "a file arrived" while you are on any page.
// Use it by calling toast.show("some text").
Rectangle {
    id: toast

    function show(message) {
        label.text = message
        opacity = 1
        hideTimer.restart() // start counting again, so a second message is not hidden early
    }

    opacity: 0
    visible: opacity > 0 // an invisible toast must not catch mouse clicks
    radius: Theme.radiusSmall
    color: Theme.text
    // as wide as the text, but never wider than the window minus a margin
    width: Math.min(label.implicitWidth, parent.width - 4 * Theme.space4) + 2 * Theme.space4
    height: label.implicitHeight + 2 * Theme.space3

    Behavior on opacity { NumberAnimation { duration: 180 } }

    Text {
        id: label
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.space4
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: "#FFFFFF"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Timer {
        id: hideTimer
        interval: 3500
        onTriggered: toast.opacity = 0
    }
}
