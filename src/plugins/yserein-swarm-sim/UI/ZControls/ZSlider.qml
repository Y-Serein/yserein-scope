import QtQuick
import QtQuick.Controls.Basic

import YSerein.SwarmSimulator.ZControls
import YSerein.SwarmSimulator.Config

Slider
{
    id      : control
    value   : 0.5

    background: Rectangle {
        x   : control.leftPadding
        y   : control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth   : 200
        implicitHeight  : 3
        width           : control.availableWidth
        height          : implicitHeight
        radius          : 2
        color           : Theme.colors.primary4

        Rectangle {
            width       : control.visualPosition * parent.width
            height      : parent.height
            color       : Theme.colors.primary
            radius      : 2
        }
    }

    handle: Rectangle {
        x   : control.leftPadding + control.visualPosition * (control.availableWidth  - width)
        y   : control.topPadding  + control.availableHeight  / 2 - height / 2
        implicitWidth   : 16
        implicitHeight  : 16
        radius          : 8
        color           : Theme.colors.primary 
        // border.width    : Styles.defaultPanelBorder
        // border.color    : control.pressed ? Theme.colors.borderPressed :Theme.colors.border
    }

}