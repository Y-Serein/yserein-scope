import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.Middle.Debug

// Scope 宿主 Output 槽（下 dock）：运行诊断与状态输出。
Rectangle {
    id: root
    color: Theme.colors.baseBackground

    DebugView {
        anchors.fill: parent
    }
}
