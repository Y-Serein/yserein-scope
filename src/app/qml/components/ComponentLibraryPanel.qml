pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Serein.Scope

MaterialPanel {
    id: root
    required property var workbench
    dark: workbench.darkMode
    property var componentIds: {
        let key = workbench.activePage + workbench.connectionState
        return workbench.availableLibraryComponents()
    }
    implicitWidth: 190

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Text { text: "组件库"; color: Theme.text(root.dark); font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { dark: root.dark; glyph: "×"; implicitWidth: 26; implicitHeight: 26; onClicked: root.closeRequested() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline(root.dark) }
        Text {
            visible: root.componentIds.length === 0
            text: "当前页面没有可添加组件"
            color: Theme.mutedText(root.dark)
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Repeater {
            model: root.componentIds
            delegate: SereinButton {
                id: libraryButton
                required property string modelData
                dark: root.dark
                text: root.workbench.libraryComponentTitle(libraryButton.modelData)
                Layout.fillWidth: true
                onClicked: root.workbench.showLibraryComponent(libraryButton.modelData)
            }
        }
        Item { Layout.fillHeight: true }
    }
}
