import QtQuick
import QtQuick.Controls

import QtQuick3D
import QtQuick3D.Helpers
// import QtGraphs // 打包会出问题

import YSerein.SwarmSimulator.Config

import Frontend
import Frontend.Show3D

// qt 是左手系左边
// 右 -> z+
// 北 -> x+ 
// 上 -> y+
// 所以 (x,y,z) 的坐标要映射为 (z,x,y)

View3D 
{
    id              : root
    camera          : camera
    // anchors.fill    : parent
    // width           : Styles.middle_3DView_width
    // height          : Styles.middle_3DView_height
    z               : Styles.middle_3DView_z

    property var g_factor : 1.3
    
    property var controller : Frontend.droneShowPlayerController
    readonly property var mockVehicles: Frontend.mockVehicleController
    property bool showImported: controller.canPlay && !mockVehicles.danceViewActive
    property var drones: showImported ? controller.drones : undefined
    readonly property real mockCameraDistance: Math.max(1200, mockVehicles.previewRadius * 320)
    readonly property real mockMarkerScale: mockVehicles.dancePlaybackState === 1
                                             || mockVehicles.dancePosition > 0
                                           ? Math.min(1.4, Math.max(0.75, mockVehicles.sceneRadius / 100))
                                           : Math.min(4, Math.max(0.75, mockVehicles.sceneRadius / 100))
    property bool cameraMoved: false
    property bool fittingCamera: false

    function resetCamera() {
        fittingCamera = true
        navigationTarget.position = showImported ? Qt.vector3d(-300, 300, 300)
                                                : Qt.vector3d(-mockCameraDistance, mockCameraDistance * 0.49, 0)
        navigationTarget.eulerRotation = Qt.vector3d(-26, -90, 0)
        cameraMoved = false
        fittingCamera = false
    }

    function zoomCamera(steps) {
        if (!steps) return
        const p = navigationTarget.position
        const forward = navigationTarget.forward
        const distance = Math.max(300, Math.sqrt(p.x * p.x + p.y * p.y + p.z * p.z))
        const clampedSteps = Math.max(-4, Math.min(4, steps))
        const amount = Math.min(distance - 150,
                                distance * (1 - Math.pow(0.82, clampedSteps)))
        navigationTarget.position = Qt.vector3d(p.x + forward.x * amount,
                                                p.y + forward.y * amount,
                                                p.z + forward.z * amount)
    }

    onMockCameraDistanceChanged: {
        if (!cameraMoved && !showImported) resetCamera()
    }
    onShowImportedChanged: resetCamera()
    Connections {
        target: root.mockVehicles
        function onDancePreparedChanged() {
            if (root.mockVehicles.dancePrepared && !root.showImported) root.resetCamera()
        }
    }

    onDronesChanged: {
        if( showImported ){
            instanceTable.drones = controller.drones
        }
        
    }

    Component.onCompleted: 
    {
        mainWindow.view3DRoot = root
        resetCamera()
    }

    // environment: SceneEnvironment {
    //     clearColor: Theme.colors.lightFill
    //     // clearColor: "#8fafff"
    //     backgroundMode: SceneEnvironment.Color

    //     InfiniteGrid {
    //        gridInterval    : 300
    //     }
    // }

    environment: ExtendedSceneEnvironment {
        backgroundMode  : SceneEnvironment.Color
        clearColor      : "#0B1020"

        glowEnabled     : true
        glowStrength    : 1.0
        glowIntensity   : 0.3
        glowBloom       : 0.5
        glowQualityHigh : true
        glowBlendMode   : ExtendedSceneEnvironment.GlowBlendMode.Additive
        glowLevel       : ExtendedSceneEnvironment.GlowLevel.One |
                            ExtendedSceneEnvironment.GlowLevel.Two | 
                            ExtendedSceneEnvironment.GlowLevel.Three |
                            ExtendedSceneEnvironment.GlowLevel.Four 
                            // ExtendedSceneEnvironment.GlowLevel.Five |
                            // ExtendedSceneEnvironment.GlowLevel.Six |
                            // ExtendedSceneEnvironment.GlowLevel.Seven
                        
        InfiniteGrid {
            gridInterval    : 1000
        }
    }

    DirectionalLight {
        eulerRotation.x: 250
        eulerRotation.y: -30
        brightness: 1.0
        ambientColor: "#7f7f7f"
    }


    // The controller moves the target at the original Qt speed. The camera
    // follows its position with a short ease-out after key or wheel input.
    Node {
        id: navigationTarget
        position: Qt.vector3d(-1200, 588, 0)
        eulerRotation: Qt.vector3d(-26, -90, 0)
        onPositionChanged: {
            if (!root.fittingCamera) root.cameraMoved = true
        }
        onEulerRotationChanged: {
            if (!root.fittingCamera) root.cameraMoved = true
        }
    }

    PerspectiveCamera {
        id  : camera
        position        : navigationTarget.position
        eulerRotation   : navigationTarget.eulerRotation
        clipFar         : Math.max(100000, root.mockCameraDistance * 8)
        Behavior on position {
            enabled: !root.fittingCamera
            Vector3dAnimation { duration: 180; easing.type: Easing.OutCubic }
        }
    }
    
    // point light
    Model {
        id          : instanceSphere
        visible     : root.showImported
        // source      : "#Sphere"
        scale       : Qt.vector3d(0.2,0.2,0.2)
        instancing  : DroneShowInstanceTable {
            id          : instanceTable
            position    : showImported ? controller.position : 0
        }

        geometry : SphereGeometry {
            id  : sphereCustom
            rings       : 16
            segments    : 16
            radius      : 50
        }
        
        // materials: DefaultMaterial { diffuseColor: "blue" }
        materials: [ lightMaterial ]
    }
    
    Model {
        visible    : !root.showImported && root.mockVehicles.count > 0
        scale      : Qt.vector3d(root.mockMarkerScale,
                                 root.mockMarkerScale,
                                 root.mockMarkerScale)
        instancing : MockVehicleInstanceTable {}
        geometry   : SphereGeometry {
            rings    : 8
            segments : 8
            radius   : 50
        }
        materials: [ lightMaterial ]
    }

    CustomMaterial {
        id              : lightMaterial
        vertexShader    : "/qt/qml/YSerein/SwarmSimulator/Resources/materials/lightMaterial.vert"
        fragmentShader  : "/qt/qml/YSerein/SwarmSimulator/Resources/materials/lightMaterial.frag"
    }


    /// 显示 X,Y,Z 轴（上是 Y 轴）
    // AxisHelper {
    //     enableXYGrid    : false
    //     enableXZGrid    : false
    //     enableYZGrid    : false
    // }

    /// 可以通过 WASD 移动，以及鼠标按下拖动镜头
    WasdController {
        id: navigationController
        controlledObject : navigationTarget
        focus: true
        speed: 1
    }

    WheelHandler {
        target: null
        onWheel: function(wheel) {
            const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y / 120
                                                  : wheel.pixelDelta.y / 100
            root.zoomCamera(wheel.inverted ? -delta : delta)
            navigationController.forceActiveFocus()
            wheel.accepted = true
        }
    }

    DebugView {
        id: dbg
        y           : Styles.head_height
        anchors.left: parent.left
        source: root
        visible: false
    }


}
