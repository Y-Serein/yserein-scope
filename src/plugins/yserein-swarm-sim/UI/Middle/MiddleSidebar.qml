import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config

Rectangle {
    id: root
    color: Theme.colors.baseBackground

    property alias sidebarCurrentIndex: sidebarTabBar.currentIndex

    component SidebarTabBar: Container {
        id: tabBarComponent
        Layout.fillWidth: true

        ButtonGroup {
            id: buttonGroup
            buttons: tabBarComponent.contentChildren
            onCheckedButtonChanged: {
                const index = Math.max(0, buttons.indexOf(checkedButton))
                tabBarComponent.setCurrentIndex(index)
            }
        }

        contentItem: ColumnLayout {
            spacing: 4
            Repeater { model: tabBarComponent.contentModel }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 14
        anchors.bottomMargin: 12
        spacing: 6

        Label {
            text: qsTr("工作区")
            color: Theme.colors.secondaryText
            font.pixelSize: 11
            font.weight: Font.Medium
            Layout.leftMargin: 8
            Layout.bottomMargin: 4
        }

        SidebarTabBar {
            id: sidebarTabBar
            Component.onCompleted: setCurrentIndex(0)

            MiddleSidebarButton {
                text: qsTr("仿真工作台")
                Layout.fillWidth: true
                checkable: true
                checked: true
            }

            MiddleSidebarButton {
                text: qsTr("轨迹分析")
                Layout.fillWidth: true
                checkable: true
            }

            MiddleSidebarButton {
                text: qsTr("运行诊断")
                Layout.fillWidth: true
                checkable: true
            }
        }

        Item { Layout.fillHeight: true }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colors.lightBorder
        }

        Label {
            text: qsTr("Simulator 0.1")
            color: Theme.colors.placeholderText
            font.pixelSize: 11
            Layout.leftMargin: 8
            Layout.topMargin: 4
        }
    }
}
