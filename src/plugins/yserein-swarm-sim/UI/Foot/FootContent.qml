import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.ZControls
import YSerein.SwarmSimulator.Resources

import Frontend
import Frontend.Show3D
import Frontend.Vehicle

Rectangle {
    id: root
    color: Theme.colors.baseBackground

    property var playerController: Frontend.droneShowPlayerController
    property bool canPlay: playerController.canPlay
    property int playbackState: playerController.playbackState

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.colors.lightBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: root.playbackState === 1 ? Theme.colors.success
                                             : (root.canPlay ? Theme.colors.primary : Theme.colors.placeholderText)
        }

        Label {
            text: !root.canPlay ? qsTr("等待舞步")
                                : (root.playbackState === 1 ? qsTr("正在播放")
                                                            : (root.playbackState === 2 ? qsTr("已暂停") : qsTr("已就绪")))
            color: Theme.colors.regularText
            font.pixelSize: 12
            font.weight: Font.Medium
        }

        ToolButton {
            id: playPauseButton
            enabled: root.canPlay
            implicitWidth: 32
            implicitHeight: 32
            onClicked: {
                if (root.playbackState === 1)
                    root.playerController.pause()
                else
                    root.playerController.play()
            }

            contentItem: Image {
                source: root.playbackState === 1
                        ? "/qt/qml/YSerein/SwarmSimulator/Resources/images/pause_16.svg"
                        : "/qt/qml/YSerein/SwarmSimulator/Resources/images/play_16.svg"
                sourceSize.width: 16
                sourceSize.height: 16
                opacity: playPauseButton.enabled ? 1.0 : 0.4
            }

            background: Rectangle {
                radius: 6
                color: playPauseButton.down ? Theme.colors.primary5
                                             : (playPauseButton.hovered ? Theme.colors.primary7 : Theme.colors.lightFill)
                border.width: 1
                border.color: Theme.colors.lightBorder
            }
        }

        ZSlider {
            enabled: root.canPlay
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            from: 0
            to: root.playerController.duration
            value: root.playerController.position
            onMoved: root.playerController.position = value
        }

        Label {
            Layout.preferredWidth: 126
            horizontalAlignment: Text.AlignRight
            text: root.formatTime(root.playerController.position) + " / "
                  + root.formatTime(root.playerController.duration)
            color: Theme.colors.primaryText
            font.pixelSize: 12
            font.family: "Consolas"
        }

        ComboBox {
            id: playbackRate
            enabled: root.canPlay
            implicitWidth: 72
            implicitHeight: 30
            textRole: "key"
            valueRole: "value"
            model: ListModel {
                ListElement { key: "1×"; value: 1.0 }
                ListElement { key: "2×"; value: 2.0 }
                ListElement { key: "3×"; value: 3.0 }
                ListElement { key: "5×"; value: 5.0 }
                ListElement { key: "10×"; value: 10.0 }
            }
            onCurrentValueChanged: root.playerController.playbackRate = currentValue
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 22
            color: Theme.colors.lightBorder
        }

        Label {
            text: qsTr("对象 %1").arg(Frontend.mockVehicleController.count)
            color: Theme.colors.secondaryText
            font.pixelSize: 12
        }
    }

    function formatTime(timeMs) {
        const totalSeconds = Math.max(0, Math.floor(timeMs / 1000))
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor((totalSeconds % 3600) / 60)
        const seconds = totalSeconds % 60
        const pad = n => n.toString().padStart(2, "0")
        return hours > 0
                ? `${pad(hours)}:${pad(minutes)}:${pad(seconds)}`
                : `${pad(minutes)}:${pad(seconds)}`
    }
}
