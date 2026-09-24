import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config

import Frontend
import Frontend.Vehicle

// 中央主场景视图：仅展示 3D 舞步预览。机群列表与详情已迁移到右侧 Inspector 面板。
Rectangle {
    id: root
    color: Theme.colors.pageBackground

    Rectangle {
        anchors.fill: parent
        anchors.margins: 10
        radius: Styles.defaultPanelRadius
        color: Theme.colors.baseBackground
        border.width: 1
        border.color: Theme.colors.lightBorder
        clip: true

        Label {
            id: previewTitle
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 14
            anchors.topMargin: 12
            text: qsTr("3D 舞步预览")
            color: Theme.colors.primaryText
            font.pixelSize: 13
            font.weight: Font.DemiBold
            z: 2
        }

        Label {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 14
            anchors.topMargin: 13
            text: Frontend.mockVehicleController.count > 0
                  ? qsTr("%1 个实例").arg(Frontend.mockVehicleController.count)
                  : qsTr("尚未载入")
            color: Theme.colors.secondaryText
            font.pixelSize: 12
            z: 2
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 42
            height: 1
            color: Theme.colors.lightBorder
            z: 2
        }

        World3DViewInstance {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 43
        }
    }
}
