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
            text: view3D.showImported
                  ? qsTr("3D 舞步预览") : qsTr("3D 模拟机编队")
            color: Theme.colors.primaryText
            font.pixelSize: 13
            font.weight: Font.DemiBold
            z: 2
        }

        Label {
            id: aircraftCount
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 14
            anchors.topMargin: 13
            text: view3D.showImported
                  ? qsTr("%1 个舞步对象").arg(Frontend.droneShowPlayerController.drones.count)
                  : (Frontend.mockVehicleController.count > 0
                     ? qsTr("%1 台模拟机").arg(Frontend.mockVehicleController.count)
                     : qsTr("尚未载入"))
            color: Theme.colors.secondaryText
            font.pixelSize: 12
            z: 2
        }

        Button {
            anchors.right: aircraftCount.left
            anchors.rightMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 5
            text: qsTr("重置视角")
            implicitHeight: 30
            z: 2
            onClicked: view3D.resetCamera()
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
            id: view3D
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 43
        }
    }
}
