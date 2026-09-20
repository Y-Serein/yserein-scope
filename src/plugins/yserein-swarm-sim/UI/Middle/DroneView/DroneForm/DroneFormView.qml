import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.ZControls

Item {
    id  : root
    clip    : true
    
    ColumnLayout {
        anchors.fill    : parent
        anchors.margins : 0

        DroneList {
            Layout.fillWidth    : true
            Layout.fillHeight   : true
        }

    }

}
