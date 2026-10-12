import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.ZControls

import Frontend
import Frontend.Vehicle

Item {
    id: root
    clip: true
    implicitHeight: 380

    property var controller: Frontend.mockVehicleController
    property int vehicleId: controller.detailVehicleId
    property var vehicle: controller.detailVehicle

    ScrollView {
        id: detailScroll
        anchors.fill: parent
        clip: true
        visible: root.vehicleId !== -1 && root.vehicle
        contentWidth: availableWidth
        contentHeight: detailContent.implicitHeight + 28
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
        id: detailContent
        x: 14
        y: 14
        width: Math.max(0, detailScroll.availableWidth - 28)
        spacing: 12

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
            Layout.minimumWidth: 0
            Layout.preferredHeight: 1
            color: Theme.colors.lightBorder
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
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

        GridLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            columns: detailContent.width < 260 ? 2 : 3
            columnSpacing: 8
            rowSpacing: 6
            Label {
                Layout.preferredWidth: 54
                text: qsTr("电量")
                color: Theme.colors.regularText
                font.pixelSize: 12
            }
            TextField {
                id: batteryField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.vehicle ? root.vehicle.batteryRemaining.toString() : ""
                placeholderText: qsTr("0–100")
                validator: IntValidator { bottom: 0; top: 100 }
            }
            ZButton {
                Layout.columnSpan: parent.columns === 2 ? 2 : 1
                Layout.alignment: Qt.AlignRight
                text: qsTr("应用")
                implicitHeight: 32
                onClicked: if (root.vehicle) root.vehicle.batteryRemaining = parseInt(batteryField.text)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            columns: detailContent.width < 260 ? 2 : 3
            columnSpacing: 8
            rowSpacing: 6
            Label {
                Layout.preferredWidth: 54
                text: qsTr("航向")
                color: Theme.colors.regularText
                font.pixelSize: 12
            }
            TextField {
                id: headingField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.vehicle ? root.vehicle.heading.toFixed(1) : ""
                placeholderText: qsTr("角度")
                validator: DoubleValidator { bottom: -360; top: 360 }
            }
            ZButton {
                Layout.columnSpan: parent.columns === 2 ? 2 : 1
                Layout.alignment: Qt.AlignRight
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
            Layout.minimumWidth: 0
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            Label { text: "X"; color: Theme.colors.regularText; font.pixelSize: 12 }
            TextField {
                id: xField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.vehicle ? root.vehicle.position.x.toFixed(2) : ""
                validator: DoubleValidator {}
            }
            Label { text: "Y"; color: Theme.colors.regularText; font.pixelSize: 12 }
            TextField {
                id: yField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.vehicle ? root.vehicle.position.y.toFixed(2) : ""
                validator: DoubleValidator {}
            }
            Label { text: "Z"; color: Theme.colors.regularText; font.pixelSize: 12 }
            TextField {
                id: zField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
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

        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("这些调整只作用于模拟对象，不会修改舞步文件。")
            color: Theme.colors.placeholderText
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.max(0, Math.min(parent.width - 28, 220))
        spacing: 8
        visible: !root.vehicle || root.vehicleId === -1

        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("选择一台模拟设备")
            horizontalAlignment: Text.AlignHCenter
            color: Theme.colors.primaryText
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("从上方列表选择设备，查看或调整其模拟状态。")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.colors.secondaryText
            font.pixelSize: 12
        }
    }
}
