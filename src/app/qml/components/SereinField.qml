import QtQuick
import QtQuick.Controls
import Serein.Scope

TextField {
    id: control
    property bool dark: false

    implicitHeight: 36
    leftPadding: 10
    rightPadding: 10
    color: Theme.text(dark)
    placeholderTextColor: Theme.mutedText(dark)
    selectionColor: Theme.accent
    selectedTextColor: "#ffffff"
    font.pixelSize: 12

    background: Rectangle {
        radius: Theme.controlRadius
        color: Theme.input(control.dark)
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.border(control.dark)
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }
}
