import QtQuick
import QtQuick.Controls
import Serein.Scope

Button {
    id: control
    property bool dark: false
    property string kind: "quiet"

    implicitHeight: 34
    leftPadding: 13
    rightPadding: 13
    font.pixelSize: 12
    font.weight: kind === "primary" ? Font.DemiBold : Font.Normal

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.mutedText(control.dark)
              : control.kind === "primary" ? "#ffffff"
              : Theme.text(control.dark)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.controlRadius
        color: !control.enabled ? Theme.hover(control.dark)
             : control.kind === "primary" ? (control.down ? "#245bc5" : Theme.accent)
             : control.down ? Theme.selected(control.dark)
             : control.hovered ? Theme.hover(control.dark)
             : Theme.panelRaised(control.dark)
        border.width: 1
        border.color: control.kind === "primary" ? color
                    : control.hovered ? Theme.border(control.dark)
                    : Theme.hairline(control.dark)
        Behavior on color { ColorAnimation { duration: 110 } }
    }
}
