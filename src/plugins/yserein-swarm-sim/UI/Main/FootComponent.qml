import QtQuick
import QtQuick.Controls

import YSerein.SwarmSimulator.Foot
import YSerein.SwarmSimulator.Config

Control {
    id      : root
    z       : Styles.foot_z

    contentItem: FootContent {
        anchors.fill    : parent
    }

    background: Rectangle {
        height  : root.height
        width   : root.width
        color   :  Theme.colors.lightFill
    }
}