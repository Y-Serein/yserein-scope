qt_add_library(sim_ui_resources STATIC)
qt_add_qml_module(sim_ui_resources
    URI YSerein.SwarmSimulator.Resources
    VERSION 1.0
    RESOURCES
        materials/lightMaterial.frag
        materials/lightMaterial.vert
        materials/customshadertiles.frag
        images/sphere.png
        images/play_16.svg
        images/gear_16.svg
        images/pause_16.svg
)
