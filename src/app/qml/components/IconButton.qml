import QtQuick
import QtQuick.Controls
import Serein.Scope

Button {
    id: control
    property bool dark: false
    property url source
    property string glyph: ""
    property bool active: false
    property int iconExtent: 18

    implicitWidth: 34
    implicitHeight: 34
    padding: 7

    contentItem: Item {
        Image {
            anchors.centerIn: parent
            width: control.iconExtent
            height: control.iconExtent
            source: control.source
            visible: control.source.toString().length > 0
            fillMode: Image.PreserveAspectFit
            opacity: control.enabled ? (control.active ? 0.95 : 0.58) : 0.28
        }
        Text {
            anchors.centerIn: parent
            text: control.glyph
            visible: control.source.toString().length === 0
            color: control.active ? Theme.accent : Theme.secondaryText(control.dark)
            font.pixelSize: 15
        }
    }

    background: Rectangle {
        radius: Theme.controlRadius
        color: control.down ? Theme.selected(control.dark)
             : control.active ? Theme.selected(control.dark)
             : control.hovered ? Theme.hover(control.dark)
             : "transparent"
        border.width: control.active ? 1 : 0
        border.color: control.active ? "#aac5ff" : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
