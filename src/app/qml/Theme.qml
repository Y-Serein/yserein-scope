pragma Singleton

import QtQuick

QtObject {
    readonly property int shellRadius: 12
    readonly property int surfaceRadius: 8
    readonly property int controlRadius: 6
    readonly property int sidePanelWidth: 220

    readonly property color accent: "#2f6feb"
    readonly property color success: "#1c8c62"
    readonly property color warning: "#b7791f"

    function shell(dark) { return dark ? "#20252a" : "#e3e6e8" }
    function panel(dark) { return dark ? "#272d33" : "#fbfcfd" }
    function panelRaised(dark) { return dark ? "#2d343b" : "#ffffff" }
    function canvas(dark) { return dark ? "#1d2227" : "#e7eaec" }
    function border(dark) { return dark ? "#3c454e" : "#c8d1da" }
    function hairline(dark) { return dark ? "#353d45" : "#e1e7ee" }
    function text(dark) { return dark ? "#edf2f6" : "#1d2939" }
    function secondaryText(dark) { return dark ? "#aab5bf" : "#667085" }
    function mutedText(dark) { return dark ? "#7f8b96" : "#8c99aa" }
    function hover(dark) { return dark ? "#343c44" : "#f4f6f8" }
    function selected(dark) { return dark ? "#263c62" : "#eaf1ff" }
    function input(dark) { return dark ? "#22282e" : "#ffffff" }
}
