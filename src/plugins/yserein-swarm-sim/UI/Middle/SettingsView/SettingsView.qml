import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config

import Frontend
import Frontend.Show3D

Rectangle {
    color: Theme.colors.pageBackground

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Label {
            text: qsTr("轨迹分析")
            color: Theme.colors.primaryText
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Label {
            text: qsTr("快速调整回放倍率，观察起飞、表演和降落阶段。")
            color: Theme.colors.secondaryText
            font.pixelSize: 13
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: Styles.defaultPanelRadius
            color: Theme.colors.baseBackground
            border.width: 1
            border.color: Theme.colors.lightBorder

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Label {
                    text: qsTr("回放设置")
                    color: Theme.colors.primaryText
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("播放倍率")
                        color: Theme.colors.regularText
                        font.pixelSize: 13
                    }
                    ComboBox {
                        enabled: Frontend.droneShowPlayerController.canPlay
                        implicitWidth: 110
                        model: ["1×", "2×", "3×", "5×", "10×"]
                        onActivated: {
                            const rates = [1, 2, 3, 5, 10]
                            Frontend.droneShowPlayerController.playbackRate = rates[currentIndex]
                        }
                    }
                }

                Label {
                    text: qsTr("倍率只改变仿真时间轴，不改变舞步轨迹数据。")
                    color: Theme.colors.placeholderText
                    font.pixelSize: 11
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
