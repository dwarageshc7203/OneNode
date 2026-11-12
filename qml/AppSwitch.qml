import QtQuick
import QtQuick.Controls.Basic

// An on/off switch in the app's green. The label is written to the right of the switch.
Switch {
    id: control

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    spacing: Theme.space3

    // the pill-shaped track with the round knob inside it
    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.track

        Rectangle {
            // the knob slides left or right, and the Behavior makes the slide smooth
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: 18
            height: 18
            radius: 9
            color: "#FFFFFF"
            Behavior on x { NumberAnimation { duration: 120 } }
        }

        // ring for keyboard focus
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: control.visualFocus
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        leftPadding: control.indicator.width + control.spacing
    }
}
