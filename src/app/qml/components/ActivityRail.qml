pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Serein.Scope

Rectangle {
    id: root
    required property var workbench
    property bool dark: workbench.darkMode
    width: 48
    radius: Theme.shellRadius
    color: Theme.panel(dark)
    border.width: 1
    border.color: Theme.border(dark)

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 4

        Repeater {
            model: ["grid.svg", "bars.svg", "layers.svg", "wallet.svg"]
            delegate: Item {
                id: templateItem
                required property string modelData
                required property int index
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                IconButton {
                    anchors.centerIn: parent
                    width: 34
                    height: 34
                    dark: root.dark
                    source: "qrc:/serein/" + templateItem.modelData
                    active: templateItem.index === 0
                    iconExtent: templateItem.index === 0 ? 20 : 18
                    onClicked: root.workbench.selectTemplate(templateItem.index)
                }
                Rectangle {
                    visible: templateItem.index === 0
                    anchors.centerIn: parent
                    width: 31
                    height: 32
                    radius: Theme.surfaceRadius
                    color: "transparent"
                    border.width: 1
                    border.color: root.dark ? "#7c8791" : "#aab6c0"
                }
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 26
            Layout.preferredHeight: 1
            color: Theme.hairline(root.dark)
        }

        IconButton {
            Layout.alignment: Qt.AlignHCenter
            dark: root.dark
            source: "qrc:/serein/channels.svg"
            active: root.workbench.navigationVisible
            onClicked: root.workbench.navigationVisible = !root.workbench.navigationVisible
            ToolTip.visible: hovered
            ToolTip.text: "设备连接"
        }
        IconButton {
            Layout.alignment: Qt.AlignHCenter
            dark: root.dark
            source: "qrc:/serein/terminal.svg"
            active: root.workbench.outputVisible
            onClicked: root.workbench.outputVisible = !root.workbench.outputVisible
            ToolTip.visible: hovered
            ToolTip.text: "输出终端"
        }
        IconButton {
            Layout.alignment: Qt.AlignHCenter
            dark: root.dark
            source: "qrc:/serein/bars.svg"
            active: root.workbench.inspectorVisible
            onClicked: root.workbench.inspectorVisible = !root.workbench.inspectorVisible
            ToolTip.visible: hovered
            ToolTip.text: "数据与属性"
        }
        IconButton {
            Layout.alignment: Qt.AlignHCenter
            dark: root.dark
            source: "qrc:/serein/grid.svg"
            active: root.workbench.componentLibraryVisible
            onClicked: root.workbench.componentLibraryVisible = !root.workbench.componentLibraryVisible
            ToolTip.visible: hovered
            ToolTip.text: "组件库"
        }

        Item { Layout.fillHeight: true }

        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            Rectangle {
                anchors.fill: parent
                radius: Theme.controlRadius
                color: sleepMouse.pressed || root.workbench.darkMode
                       ? Theme.selected(root.dark)
                       : sleepMouse.containsMouse ? Theme.hover(root.dark) : "transparent"
                border.width: root.workbench.darkMode ? 1 : 0
                border.color: "#aac5ff"
            }
            Item {
                anchors.centerIn: parent
                width: 18
                height: 18
                Rectangle {
                    x: 2
                    y: 2
                    width: 14
                    height: 14
                    radius: 7
                    color: root.workbench.darkMode ? "#8fb3ff" : Theme.secondaryText(root.dark)
                }
                Rectangle {
                    x: 7
                    y: -1
                    width: 13
                    height: 13
                    radius: 6.5
                    color: root.workbench.darkMode ? Theme.selected(root.dark) : Theme.panel(root.dark)
                }
            }
            MouseArea {
                id: sleepMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.workbench.darkMode = !root.workbench.darkMode
            }
            ToolTip.visible: sleepMouse.containsMouse
            ToolTip.text: "Sleep"
        }
    }
}
