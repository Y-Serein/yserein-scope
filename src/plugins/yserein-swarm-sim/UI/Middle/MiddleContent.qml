import QtQuick
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.ZControls

import YSerein.SwarmSimulator.Middle.Debug
import YSerein.SwarmSimulator.Middle.Drone
import YSerein.SwarmSimulator.Middle.Settings


Rectangle
{
    id      : root

    color   : Theme.colors.baseBackground
    
    property alias sidebarCurrentIndex: stackLayout.currentIndex

    StackLayout {
        id		: stackLayout
        anchors.fill: parent

        // World3DViewInstance {}
        DroneView {
            Layout.fillHeight   : true
            Layout.fillWidth    : true
        }

        SettingsView {
            Layout.fillHeight   : true
            Layout.fillWidth    : true
        }
        
        DebugView {
            Layout.fillHeight   : true
            Layout.fillWidth    : true
        }
        
    }

    
    // Map3DTest {}
}