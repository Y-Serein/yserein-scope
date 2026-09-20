import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.ZControls
import YSerein.SwarmSimulator.Middle


Rectangle {
    id      : root
    color   : Theme.colors.pageBackground

    RowLayout {
        anchors.fill: parent
        spacing     : 0

        MiddleSidebar {
            id                  : _middleSidebar
            Layout.fillWidth    : true
            Layout.minimumWidth : Styles.middle_sidebar_width_min
            Layout.maximumWidth : Styles.middle_sidebar_width_max
            Layout.fillHeight   : true
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: Theme.colors.lightBorder
        }

        MiddleContent {
            sidebarCurrentIndex : _middleSidebar.sidebarCurrentIndex

            Layout.minimumWidth : Styles.middle_content_width_min
            Layout.fillWidth    : true
            Layout.fillHeight   : true
        }

    }
}
