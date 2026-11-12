import QtQuick
import QtQuick.Layouts

// A white rounded box with a thin border. Whatever you put inside a Card { ... } is stacked top to bottom
// inside it, with padding, and the card grows to fit.
Rectangle {
    id: card

    // "default property alias" means: children written between the braces of a Card go into the inner column
    default property alias content: inner.data

    color: Theme.card
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border
    implicitHeight: inner.implicitHeight + 2 * Theme.space4

    ColumnLayout {
        id: inner
        anchors.fill: parent
        anchors.margins: Theme.space4
        spacing: Theme.space3
    }
}
