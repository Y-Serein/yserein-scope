import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config

/// All properties defined here are visible to all QML pages.
/// Note: the root is an Item (not an ApplicationWindow) so the simulator can be
/// embedded inside a QQuickView / window container owned by Scope.
Item {
    id			: mainWindow

    // 全局获取 View3D 根节点
    property var view3DRoot : undefined

    // test
    signal animationStart
    signal animationStop
    signal animationPause

    Rectangle {
        id		: rootBackground
        anchors.fill : parent
        color	: Theme.colors.baseBackground
    }

    HeadComponent {
        id      : headComponent
        anchors.top     : parent.top
        anchors.left    : parent.left
        anchors.right   : parent.right
        height  : Styles.head_height_fixed
    }

    MiddleComponent {
        anchors.top     : headComponent.bottom
        anchors.bottom  : footComponent.top
        anchors.left    : parent.left
        anchors.right   : parent.right
    }

    FootComponent {
        id      : footComponent
        anchors.left    : parent.left
        anchors.right   : parent.right
        anchors.bottom  : parent.bottom
        height  : Styles.foot_height_fixed
    }
}
