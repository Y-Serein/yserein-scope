import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.Middle.Drone

// Scope 宿主 Inspector 槽（右 dock）：机群列表与设备状态编辑。
Rectangle {
    id: root
    color: Theme.colors.baseBackground

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        DroneFormView {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colors.lightBorder
        }

        DroneDetailView {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
