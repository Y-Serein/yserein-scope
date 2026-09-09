import QtQuick
import Serein.Scope

Rectangle {
    id: root
    required property var workbench
    property bool dark: workbench.darkMode
    color: Theme.canvas(dark)
    radius: Theme.surfaceRadius
    border.width: 1
    border.color: Theme.border(dark)
    clip: true

    WindowContainer {
        id: nativeWorkspace
        objectName: "qmlNativeWorkspaceContainer"
        anchors.fill: parent
        anchors.margins: 1
        window: root.workbench.workspaceWindow
        visible: window !== null
    }

    Text {
        anchors.centerIn: parent
        visible: nativeWorkspace.window === null
        text: "工作区正在准备"
        color: Theme.mutedText(root.dark)
        font.pixelSize: 12
    }
}
