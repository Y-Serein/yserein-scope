import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config

import Frontend
import Frontend.Vehicle

Rectangle {
    id: root
    color: Theme.colors.pageBackground

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 292
            Layout.minimumWidth: 250
            Layout.fillHeight: true
            radius: Styles.defaultPanelRadius
            color: Theme.colors.baseBackground
            border.width: 1
            border.color: Theme.colors.lightBorder

            DroneFormView { anchors.fill: parent }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 380
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
                text: qsTr("3D 舞步预览")
                color: Theme.colors.primaryText
                font.pixelSize: 13
                font.weight: Font.DemiBold
                z: 2
            }

            Label {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 14
                anchors.topMargin: 13
                text: Frontend.mockVehicleController.count > 0
                      ? qsTr("%1 个实例").arg(Frontend.mockVehicleController.count)
                      : qsTr("尚未载入")
                color: Theme.colors.secondaryText
                font.pixelSize: 12
                z: 2
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
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: 43
            }
        }

        Rectangle {
            Layout.preferredWidth: 304
            Layout.minimumWidth: 270
            Layout.fillHeight: true
            radius: Styles.defaultPanelRadius
            color: Theme.colors.baseBackground
            border.width: 1
            border.color: Theme.colors.lightBorder

            DroneDetailView { anchors.fill: parent }
        }
    }
}
