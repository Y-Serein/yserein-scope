import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config

import Frontend
import Frontend.Vehicle

// 中央顶栏：仅保留标题、工作区名与机群状态。
// 导入舞步 / 目标 IP / 网格配置已迁移到左侧 Context 面板。
Rectangle {
    id: root
    color: Theme.colors.baseBackground

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 14
        spacing: 12

        ColumnLayout {
            spacing: 1
            Layout.alignment: Qt.AlignVCenter

            Label {
                text: qsTr("YSerein Swarm Simulator")
                color: Theme.colors.primaryText
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }

            Label {
                text: qsTr("编队仿真与压力验证")
                color: Theme.colors.secondaryText
                font.pixelSize: 11
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 24
            color: Theme.colors.lightBorder
        }

        Label {
            text: qsTr("仿真工作台")
            color: Theme.colors.regularText
            font.pixelSize: 13
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            Layout.preferredHeight: 28
            Layout.preferredWidth: statusLabel.implicitWidth + 22
            radius: 6
            color: Frontend.mockVehicleController.count > 0
                   ? Theme.colors.success7 : Theme.colors.lightFill
            border.width: 1
            border.color: Frontend.mockVehicleController.count > 0
                           ? Theme.colors.success4 : Theme.colors.lightBorder

            Label {
                id: statusLabel
                anchors.centerIn: parent
                text: Frontend.mockVehicleController.count > 0
                      ? qsTr("%1 架已载入").arg(Frontend.mockVehicleController.count)
                      : qsTr("等待导入")
                color: Frontend.mockVehicleController.count > 0
                       ? Theme.colors.success2 : Theme.colors.secondaryText
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.colors.lightBorder
    }
}
