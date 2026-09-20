import QtQuick
import QtQuick.Controls

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.ZControls

Rectangle {
    implicitHeight	: Styles.defaultSpacing
    color: SplitHandle.pressed ? Theme.colors.pressBorder
        : (SplitHandle.hovered ? Theme.colors.hoverBorder : Theme.colors.transparent)
}