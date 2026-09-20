import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.ZControls

import Frontend
import Frontend.Vehicle

Item {
    id: root

    property var controller: Frontend.mockVehicleController
    property int vehicleId: controller.detailVehicleId
    property var vehicle: controller.detailVehicle

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12
        visible: root.vehicleId !== -1 && root.vehicle

        Label {
            text: qsTr("设备配置")
            color: Theme.colors.primaryText
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        Label {
            text: qsTr("模拟设备 %1").arg(root.vehicleId)
            color: Theme.colors.secondaryText
            font.pixelSize: 12
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colors.lightBorder
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("模拟链路中断")
                color: Theme.colors.regularText
                font.pixelSize: 13
            }
            CheckBox {
                checked: root.vehicle?.commLost ?? false
                onToggled: if (root.vehicle) root.vehicle.commLost = checked
            }
        }

        Label {
            text: qsTr("状态参数")
            color: Theme.colors.secondaryText
            font.pixelSize: 11
            font.weight: Font.Medium
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.preferredWidth: 54
                text: qsTr("电量")
                color: Theme.colors.regularText
                font.pixelSize: 12
            }
            TextField {
                id: batteryField
                Layout.fillWidth: true
                text: root.vehicle ? root.vehicle.batteryRemaining.toString() : ""
                placeholderText: qsTr("0–100")
                validator: IntValidator { bottom: 0; top: 100 }
            }
            ZButton {
                text: qsTr("应用")
                implicitHeight: 32
                onClicked: if (root.vehicle) root.vehicle.batteryRemaining = parseInt(batteryField.text)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.preferredWidth: 54
                text: qsTr("航向")
                color: Theme.colors.regularText
                font.pixelSize: 12
            }
            TextField {
                id: headingField
                Layout.fillWidth: true
                text: root.vehicle ? root.vehicle.heading.toFixed(1) : ""
                placeholderText: qsTr("角度")
                validator: DoubleValidator { bottom: -360; top: 360 }
            }
            ZButton {
                text: qsTr("应用")
                implicitHeight: 32
                onClicked: if (root.vehicle) root.vehicle.heading = parseFloat(headingField.text)
            }
        }

        Label {
            text: qsTr("局部坐标（米）")
            color: Theme.colors.secondaryText
            font.pixelSize: 11
            font.weight: Font.Medium
            Layout.topMargin: 4
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            Label { text: "X"; color: Theme.colors.regularText; font.pixelSize: 12 }
            TextField {
                id: xField
                Layout.fillWidth: true
                text: root.vehicle ? root.vehicle.position.x.toFixed(2) : ""
                validator: DoubleValidator {}
            }
            Label { text: "Y"; color: Theme.colors.regularText; font.pixelSize: 12 }
            TextField {
                id: yField
                Layout.fillWidth: true
                text: root.vehicle ? root.vehicle.position.y.toFixed(2) : ""
                validator: DoubleValidator {}
            }
            Label { text: "Z"; color: Theme.colors.regularText; font.pixelSize: 12 }
            TextField {
                id: zField
                Layout.fillWidth: true
                text: root.vehicle ? root.vehicle.position.z.toFixed(2) : ""
                validator: DoubleValidator {}
            }
        }

        ZButton {
            Layout.alignment: Qt.AlignRight
            text: qsTr("应用坐标")
            onClicked: {
                if (root.vehicle) {
                    root.vehicle.position = Qt.vector3d(parseFloat(xField.text),
                                                        parseFloat(yField.text),
                                                        parseFloat(zField.text))
                }
            }
        }

        Item { Layout.fillHeight: true }

        Label {
            Layout.fillWidth: true
            text: qsTr("这些调整只作用于模拟对象，不会修改舞步文件。")
            color: Theme.colors.placeholderText
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 220)
        spacing: 8
        visible: !root.vehicle || root.vehicleId === -1

        Label {
            Layout.fillWidth: true
            text: qsTr("选择一台模拟设备")
            horizontalAlignment: Text.AlignHCenter
            color: Theme.colors.primaryText
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("从左侧列表选择设备，查看或调整其模拟状态。")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.colors.secondaryText
            font.pixelSize: 12
        }
    }
}
