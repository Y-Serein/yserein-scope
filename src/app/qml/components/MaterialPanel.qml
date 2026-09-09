import QtQuick
import Serein.Scope

Rectangle {
    id: root
    property bool dark: false
    property string title: ""
    property bool showHeader: title.length > 0
    signal closeRequested()

    color: Theme.panel(dark)
    radius: Theme.surfaceRadius
    border.width: 1
    border.color: Theme.border(dark)
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.dark ? "#394149" : "#ffffff"
        opacity: 0.9
    }
}
