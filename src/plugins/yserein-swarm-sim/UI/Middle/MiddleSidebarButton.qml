import QtQuick
import QtQuick.Controls.Basic

import YSerein.SwarmSimulator.Config

Button {
    id: root
    implicitHeight: Styles.middle_sidebar_button_height_fixed

    contentItem: Label {
        text: root.text
        color: root.checked ? Theme.colors.primary : Theme.colors.regularText
        font.pixelSize: 13
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        leftPadding: 14
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 6
        color: root.down ? Theme.colors.primary5
                         : (root.checked ? Theme.colors.primary7
                                         : (root.hovered ? Theme.colors.lightFill : Theme.colors.transparent))

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 20
            radius: 2
            visible: root.checked
            color: Theme.colors.primary
        }
    }
}
