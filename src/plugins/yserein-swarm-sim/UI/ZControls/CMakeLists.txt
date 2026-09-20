qt_add_library(sim_ui_zcontrols STATIC)
qt_add_qml_module(sim_ui_zcontrols
    URI YSerein.SwarmSimulator.ZControls
    VERSION 1.0
    QML_FILES
        ZButton.qml
        ZSlider.qml
        ZText.qml
        ZIconButton.qml
        ZSplitViewHHandler.qml
        ZSplitViewVHandler.qml

        # spacer
        Spacer/ZLayoutSpacer.qml
        Spacer/ZLayoutWSpacer.qml
        Spacer/ZLayoutHSpacer.qml
        Spacer/ZLayoutColorSpacer.qml
        Spacer/ZLayoutWLine.qml
        Spacer/ZLayoutHLine.qml
        ZIcon.qml
)
