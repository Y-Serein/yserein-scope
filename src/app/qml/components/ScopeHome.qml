pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Serein.Scope

Rectangle {
    id: root
    required property var workbench
    property bool management: false
    property bool dark: workbench.darkMode
    signal importRequested()

    color: Theme.canvas(dark)
    radius: Theme.surfaceRadius
    border.width: 1
    border.color: Theme.border(dark)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: root.management ? "插件管理" : "Scope"; color: Theme.text(root.dark); font.pixelSize: 18; font.weight: Font.DemiBold }
                Text { text: root.management ? "安装、加载和卸载独立设备插件" : "选择一个已安装插件进入仪器工作区"; color: Theme.secondaryText(root.dark); font.pixelSize: 11 }
            }
            SereinButton { dark: root.dark; text: "导入插件包"; onClicked: root.importRequested() }
        }

        GridView {
            id: grid
            objectName: "qmlComponentGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: width >= 760 ? width / 2 : width
            cellHeight: 180
            model: root.workbench.components
            delegate: Item {
                id: cardHost
                required property string componentId
                required property string name
                required property string description
                required property string version
                required property string category
                required property var capabilities
                required property bool loaded
                required property string errorText
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 6
                    radius: Theme.surfaceRadius
                    color: Theme.panelRaised(root.dark)
                    border.width: 1
                    border.color: cardMouse.containsMouse ? Theme.border(root.dark) : Theme.hairline(root.dark)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: cardHost.name; color: Theme.text(root.dark); font.pixelSize: 15; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                            Rectangle {
                                implicitWidth: stateText.implicitWidth + 14
                                implicitHeight: 22
                                radius: Theme.surfaceRadius
                                color: cardHost.loaded ? "#eaf6f0" : Theme.hover(root.dark)
                                border.width: 1
                                border.color: cardHost.loaded ? "#c9e8d8" : Theme.hairline(root.dark)
                                Text { id: stateText; anchors.centerIn: parent; text: cardHost.loaded ? "已加载" : "已安装"; color: cardHost.loaded ? Theme.success : Theme.secondaryText(root.dark); font.pixelSize: 9 }
                            }
                        }
                        Text { text: cardHost.description; color: Theme.secondaryText(root.dark); font.pixelSize: 11; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: cardHost.category.toUpperCase() + "  ·  v" + cardHost.version + "  ·  ABI 2"; color: Theme.mutedText(root.dark); font.pixelSize: 9 }
                        Text { text: cardHost.capabilities.join("  ·  "); color: Theme.secondaryText(root.dark); font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                        Item { Layout.fillHeight: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            SereinButton {
                                visible: root.management && cardHost.loaded
                                dark: root.dark
                                text: "卸载"
                                onClicked: root.workbench.unloadComponent(cardHost.componentId)
                            }
                            SereinButton {
                                dark: root.dark
                                kind: "primary"
                                text: cardHost.loaded ? "打开工作区" : "加载并打开"
                                onClicked: root.workbench.openComponent(cardHost.componentId)
                            }
                        }
                    }
                    MouseArea { id: cardMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                }
            }
            ScrollBar.vertical: ScrollBar { }
        }
    }
}
