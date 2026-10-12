import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config

import Frontend
import Frontend.Show3D
import Frontend.Vehicle

Rectangle {
    id: root
    readonly property var mockController: Frontend.mockVehicleController
    readonly property var playerController: Frontend.droneShowPlayerController
    readonly property bool useBoxDance: mockController.danceViewActive
    readonly property int playbackState: useBoxDance ? mockController.dancePlaybackState
                                                    : playerController.playbackState
    readonly property bool canPlay: useBoxDance ? mockController.danceCanPlay
                                              : playerController.canPlay
    readonly property string playbackStatus: mockController.clearing ? qsTr("正在清除…")
            : playbackState === 1 ? qsTr("正在播放")
            : playbackState === 2 ? qsTr("已暂停")
            : canPlay ? qsTr("已就绪")
            : useBoxDance ? qsTr("等待模拟机就绪") : qsTr("等待舞步")

    color: Theme.colors.pageBackground

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Label {
            text: qsTr("运行诊断")
            color: Theme.colors.primaryText
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Label {
            text: qsTr("检查模拟对象、播放状态和固定网络契约。")
            color: Theme.colors.secondaryText
            font.pixelSize: 13
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 210
            radius: Styles.defaultPanelRadius
            color: Theme.colors.baseBackground
            border.width: 1
            border.color: Theme.colors.lightBorder

            GridLayout {
                anchors.fill: parent
                anchors.margins: 18
                columns: 2
                columnSpacing: 36
                rowSpacing: 18

                Label { text: qsTr("模拟对象"); color: Theme.colors.secondaryText; font.pixelSize: 12 }
                Label { text: Frontend.mockVehicleController.count; color: Theme.colors.primaryText; font.pixelSize: 13; font.weight: Font.Medium }
                Label { text: qsTr("播放状态"); color: Theme.colors.secondaryText; font.pixelSize: 12 }
                Label {
                    text: root.playbackStatus
                    color: Theme.colors.primaryText
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                Label { text: qsTr("命令接收"); color: Theme.colors.secondaryText; font.pixelSize: 12 }
                Label { text: "0.0.0.0:37880"; color: Theme.colors.primaryText; font.pixelSize: 13; font.family: "Consolas" }
                Label { text: qsTr("遥测目标"); color: Theme.colors.secondaryText; font.pixelSize: 12 }
                Label { text: "192.168.20.251:37890"; color: Theme.colors.primaryText; font.pixelSize: 13; font.family: "Consolas" }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
