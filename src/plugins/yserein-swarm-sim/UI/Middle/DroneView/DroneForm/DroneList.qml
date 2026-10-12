import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config

import Frontend
import Frontend.Vehicle

Item {
    id: root
    clip: true

    property var controller: Frontend.mockVehicleController

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44

            Label {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("模拟设备")
                color: Theme.colors.primaryText
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            Label {
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%1 架").arg(root.controller.count)
                color: Theme.colors.secondaryText
                font.pixelSize: 12
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Theme.colors.lightFill

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: qsTr("设备 ID")
                    color: Theme.colors.secondaryText
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
                Label {
                    Layout.preferredWidth: 72
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("模拟电量")
                    color: Theme.colors.secondaryText
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
            }
        }

        ListView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.controller.count
            cacheBuffer: view.height
            clip: true

            delegate: Item {
                id: rowItem
                width: (vehicle?.passFilter ?? false) ? view.width : 0
                height: (vehicle?.passFilter ?? false) ? Styles.drone_single_height : 0

                property var vehicle: root.controller.getVehiclePropertys(index + 1)
                property bool selected: vehicle
                                        && root.controller.detailVehicleId === vehicle.vehicleId

                Rectangle {
                    anchors.fill: parent
                    color: rowItem.selected ? Theme.colors.primary7
                                            : (hover.hovered ? Theme.colors.lighterFill
                                                             : Theme.colors.baseBackground)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: rowItem.vehicle?.vehicleId ?? "—"
                            color: rowItem.selected ? Theme.colors.primary : Theme.colors.primaryText
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }

                        Label {
                            Layout.preferredWidth: 72
                            horizontalAlignment: Text.AlignRight
                            text: rowItem.vehicle ? rowItem.vehicle.batteryRemaining + "%" : "—"
                            color: Theme.colors.regularText
                            font.pixelSize: 12
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.colors.extraLightBorder
                    }
                }

                HoverHandler { id: hover }
                TapHandler {
                    onTapped: {
                        if (rowItem.vehicle)
                            root.controller.setDetailVehicle(rowItem.vehicle.vehicleId)
                    }
                }
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Label {
                anchors.centerIn: parent
                visible: root.controller.count === 0
                text: qsTr("暂无模拟设备\n请生成网格编队或导入舞步")
                horizontalAlignment: Text.AlignHCenter
                color: Theme.colors.placeholderText
                font.pixelSize: 13
                lineHeight: 1.5
            }
        }
    }
}
