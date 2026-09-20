import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import YSerein.SwarmSimulator.Config

import Frontend
import Frontend.DroneShow
import Frontend.Vehicle

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

        Label {
            text: qsTr("目标 192.168.20.251:37890")
            color: Theme.colors.secondaryText
            font.pixelSize: 12
        }

        Button {
            id: loadFile
            text: qsTr("导入舞步")
            implicitWidth: 92
            implicitHeight: 32
            onClicked: fileDialog.open()

            contentItem: Label {
                text: loadFile.text
                color: "#FFFFFF"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 13
                font.weight: Font.Medium
            }

            background: Rectangle {
                radius: 6
                color: loadFile.down ? Theme.colors.primary2
                                      : (loadFile.hovered ? Theme.colors.primary2 : Theme.colors.primary)
            }
        }

        RowLayout {
            spacing: 6

            Label {
                text: qsTr("网格")
                color: Theme.colors.regularText
                font.pixelSize: 12
            }

            SpinBox {
                id: gridRows
                from: 1
                to: 5000
                value: 10
                editable: true
                implicitWidth: 84
                implicitHeight: 30
                textFromValue: function(value) { return value }
                valueFromText: function(text) { return Number(text) }
            }

            Label {
                text: "×"
                color: Theme.colors.secondaryText
            }

            SpinBox {
                id: gridCols
                from: 1
                to: 5000
                value: 10
                editable: true
                implicitWidth: 84
                implicitHeight: 30
                textFromValue: function(value) { return value }
                valueFromText: function(text) { return Number(text) }
            }

            Label {
                text: qsTr("间距(m)")
                color: Theme.colors.regularText
                font.pixelSize: 12
            }

            SpinBox {
                id: gridSpacing
                from: 1
                to: 500
                value: 12
                editable: true
                implicitWidth: 84
                implicitHeight: 30
            }

            Button {
                id: genGrid
                text: qsTr("一键生成")
                implicitWidth: 92
                implicitHeight: 32
                onClicked: Frontend.mockVehicleController.generateGridVehicles(
                               Number(gridRows.text),
                               Number(gridCols.text),
                               Number(gridSpacing.text))

                contentItem: Label {
                    text: genGrid.text
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }

                background: Rectangle {
                    radius: 6
                    color: genGrid.down ? Theme.colors.primary2
                                         : (genGrid.hovered ? Theme.colors.primary2 : Theme.colors.primary)
                }
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

    FileDialog {
        id: fileDialog
        title: qsTr("选择舞步文件")
        nameFilters: ["YSerein 舞步文件 (*.yds *.zds)", "所有文件 (*.*)"]
        currentFolder: StandardPaths.standardLocations(StandardPaths.DocumentsLocation)[0]
        onAccepted: Frontend.droneShowController.droneShowFileLoad(selectedFile)
    }
}
