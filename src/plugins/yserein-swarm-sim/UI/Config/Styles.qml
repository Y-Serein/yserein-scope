pragma Singleton

import QtQuick

QtObject{

    /// default size =====================================

    readonly property int defaultFontPixelSize   : 14

    readonly property real smallRatio     : 0.75
    readonly property real mediumRatio    : 1.25
    readonly property real largeRatio     : 1.5

    readonly property real defaultHeight     : 10
    readonly property real defaultWidth      : 10
    readonly property real defaultSpacing    : 8

    /// default radius ===================================

    readonly property int defaultPanelRadius    : 8
    readonly property int defaultStatusRadius   : 10

    /// default border
    readonly property int defaultPanelBorder    : 1
    readonly property int defaultStatusBorder   : 2

    /// app size =========================================
    property int app_width_min      : 1100
    property int app_height_min     : 680

    property int app_width_default  : 1360
    property int app_height_default : 820
    

    /// component size
    // head 和 foot 都作为透明组件悬浮在 middle 之上

    /// app Z-index ======================================

    property int head_z             : 100
    property int foot_z             : 100

    property int middle_3DView_z    : 0

    // head ===============================================
    property int head_height_fixed   : 56

    // foot ===============================================
    property int foot_height_fixed   : 48

    // middle =============================================

    // ------------------------------------  middle side bar
    property int middle_sidebar_width_min  : 144
    property int middle_sidebar_width_max  : 144
    property int middle_sidebar_button_height_fixed  : 40
    
    // -------------------------------------- middle content
    property int middle_content_width_min  : 20 * defaultWidth
    
    property int drone_single_height       : 38

    // ------------------------------------- middle 3D View


}
