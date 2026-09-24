import QtQuick
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.ZControls

import YSerein.SwarmSimulator.Middle.Drone
import YSerein.SwarmSimulator.Middle.Settings


Rectangle
{
    id      : root

    color   : Theme.colors.baseBackground

    property alias sidebarCurrentIndex: stackLayout.currentIndex

    StackLayout {
        id		: stackLayout
        anchors.fill: parent

        // 中央主工作区：3D 舞步预览 / 轨迹分析。
        // 运行诊断已迁移到宿主 Output 槽（下面板）。
        DroneView {
            Layout.fillHeight   : true
            Layout.fillWidth    : true
        }

        SettingsView {
            Layout.fillHeight   : true
            Layout.fillWidth    : true
        }
    }
}
