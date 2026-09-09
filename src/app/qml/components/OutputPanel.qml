pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Serein.Scope

MaterialPanel {
    id: root
    required property var workbench
    dark: workbench.darkMode
    implicitHeight: 168

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Text { text: "输出终端"; color: Theme.text(root.dark); font.pixelSize: 12; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { dark: root.dark; glyph: "×"; implicitWidth: 24; implicitHeight: 24; onClicked: root.closeRequested() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline(root.dark) }
        ListView {
            id: logView
            objectName: "qmlOutputLog"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.workbench.logLines
            clip: true
            spacing: 2
            delegate: Text {
                id: logLine
                required property string modelData
                width: logView.width
                text: logLine.modelData
                color: Theme.secondaryText(root.dark)
                font.family: "Consolas"
                font.pixelSize: 10
                elide: Text.ElideRight
            }
            onCountChanged: positionViewAtEnd()
            ScrollBar.vertical: ScrollBar { }
        }
    }
}
