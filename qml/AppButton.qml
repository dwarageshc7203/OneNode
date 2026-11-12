import QtQuick
import QtQuick.Controls.Basic

// The one button style of the app. Three looks, chosen with the "kind" property:
//   "primary"  green and filled      (the main action on a screen)
//   "outline"  white with a green edge (a secondary action)
//   "text"     no box, only a label    (navigation and small actions)
Button {
    id: control

    property string kind: "primary"
    property bool active: false // for kind "text": marks the current page in the top navigation

    implicitHeight: 40
    leftPadding: Theme.space4
    rightPadding: Theme.space4
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    font.weight: Font.Medium

    // what is written on the button
    contentItem: Text {
        text: control.text
        font: control.font
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        opacity: control.enabled ? 1.0 : 0.45
        color: control.kind === "primary" ? Theme.textOnAccent
             : control.kind === "outline" ? Theme.accent
             : (control.active ? Theme.accentText : Theme.textMuted)
    }

    // the box behind the label
    background: Rectangle {
        radius: Theme.radiusSmall
        opacity: control.enabled ? 1.0 : 0.45
        color: control.kind === "primary" ? ((control.down || control.hovered) ? Theme.accentHover : Theme.accent)
             : control.kind === "outline" ? (control.hovered ? Theme.accentSoft : Theme.card)
             : (control.active ? Theme.accentSoft : (control.hovered ? Theme.accentSoft : "transparent"))
        border.width: control.kind === "outline" ? 1 : 0
        border.color: Theme.accent

        // a visible ring when the button is reached with the keyboard (Tab)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: Theme.radiusSmall + 3
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: control.visualFocus
        }
    }
}
