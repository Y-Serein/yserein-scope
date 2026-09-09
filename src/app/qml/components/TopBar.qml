pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Serein.Scope

Rectangle {
    id: root
    required property var workbench
    property bool dark: workbench.darkMode
    signal minimizeRequested()
    signal maximizeRequested()
    signal closeRequested()
    signal moveRequested()
    signal importRequested()
    signal aboutRequested()

    height: 48
    radius: Theme.shellRadius
    color: Theme.panelRaised(dark)
    border.width: 1
    border.color: Theme.border(dark)

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.dark ? "#414a53" : "#ffffff"
    }

    RowLayout {
        z: 1
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 8

        Text {
            text: "Serein Scope  /  仪器工作台"
            color: Theme.text(root.dark)
            font.pixelSize: 12
            font.weight: Font.DemiBold
            Layout.rightMargin: 4
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.border(root.dark) }

        Repeater {
            model: ["文件", "编辑", "视图", "工具", "帮助"]
            delegate: Button {
                id: menuButton
                required property string modelData
                implicitWidth: 42
                implicitHeight: 30
                text: modelData
                contentItem: Text {
                    text: menuButton.text
                    color: Theme.secondaryText(root.dark)
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: menuButton.hovered ? Theme.hover(root.dark) : "transparent"
                }
                onClicked: {
                    if (menuButton.modelData === "文件") root.importRequested()
                    else if (menuButton.modelData === "编辑") root.workbench.resetPanels()
                    else if (menuButton.modelData === "视图") root.workbench.inspectorVisible = !root.workbench.inspectorVisible
                    else if (menuButton.modelData === "工具") root.workbench.activatePage("__components__")
                    else if (menuButton.modelData === "帮助") root.aboutRequested()
                }
                ToolTip.visible: hovered
                ToolTip.text: menuButton.modelData === "文件" ? "导入插件包"
                              : menuButton.modelData === "编辑" ? "恢复默认面板布局"
                              : menuButton.modelData === "视图" ? "切换数据与属性面板"
                              : menuButton.modelData === "工具" ? "打开插件管理"
                              : "关于 Serein Scope"
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.border(root.dark) }

        RowLayout {
            id: tabs
            spacing: 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            Repeater {
                model: root.workbench.workspaceTabs
                delegate: Item {
                    id: tab
                    required property var modelData
                    Layout.preferredWidth: Math.min(210, Math.max(82, titleText.implicitWidth + (modelData.closable ? 38 : 24)))
                    Layout.fillHeight: true

                    Rectangle {
                        anchors.fill: parent
                        color: root.workbench.activePage === tab.modelData.id
                               ? Theme.panelRaised(root.dark)
                               : tabMouse.containsMouse ? Theme.hover(root.dark) : "transparent"
                    }
                    Text {
                        id: titleText
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: tab.modelData.closable ? -7 : 0
                        text: tab.modelData.title
                        color: root.workbench.activePage === tab.modelData.id
                               ? Theme.accent : Theme.secondaryText(root.dark)
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "×"
                        visible: tab.modelData.closable
                        color: closeMouse.containsMouse ? Theme.text(root.dark) : Theme.mutedText(root.dark)
                        font.pixelSize: 13
                        MouseArea {
                            id: closeMouse
                            anchors.fill: parent
                            anchors.margins: -7
                            hoverEnabled: true
                            onClicked: function(mouse) {
                                mouse.accepted = true
                                root.workbench.closeWorkspace(tab.modelData.id)
                            }
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: root.workbench.activePage === tab.modelData.id ? Theme.accent : "transparent"
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        anchors.rightMargin: tab.modelData.closable ? 28 : 0
                        hoverEnabled: true
                        onClicked: root.workbench.activatePage(tab.modelData.id)
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.border(root.dark) }

        IconButton { dark: root.dark; glyph: "−"; onClicked: root.minimizeRequested() }
        IconButton { dark: root.dark; glyph: "□"; onClicked: root.maximizeRequested() }
        IconButton {
            id: closeControl
            dark: root.dark
            glyph: "×"
            onClicked: root.closeRequested()
            background: Rectangle {
                radius: Theme.controlRadius
                color: closeControl.hovered ? "#b54747" : "transparent"
            }
        }
    }

    MouseArea {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: 520
        anchors.right: parent.right
        anchors.rightMargin: 120
        acceptedButtons: Qt.LeftButton
        propagateComposedEvents: true
        onPressed: root.moveRequested()
        onDoubleClicked: root.maximizeRequested()
    }
}
