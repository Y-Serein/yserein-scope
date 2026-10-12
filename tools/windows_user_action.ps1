param(
    [switch]$Generic,
    [switch]$SkipInstaller,
    [switch]$SdkOnly,
    [string]$BuildOnly = '',
    [string[]]$Components = @()
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo 'build\windows-release'
$candidateName = if ($Generic) { 'yserein-scope-generic' } else { 'yserein-scope' }
$candidateDir = Join-Path $repo "out\$candidateName"
$candidateExe = Join-Path $candidateDir 'SereinScope.exe'
$stagingDir = Join-Path $repo "out\.$candidateName-staging"
$stagingExe = Join-Path $stagingDir 'SereinScope.exe'
$previousCandidateDir = Join-Path $repo "out\.$candidateName-previous"
$candidateAppDir = $stagingDir
$candidateComponentsDir = Join-Path $stagingDir 'Components'
$stableComponent = Join-Path $repo 'out\run-win\Components\tn160-thermal-ncm.sscomponent'

# Selectable component registry: each entry maps a CMake build option to the
# build source and the candidate copy name. Components with an Extra DLL list
# (e.g. the simulator) ship those DLLs next to their .sscomponent; every
# selected component also gets a separate zip pack.
$componentRegistry = [ordered]@{
    'serial-scope'      = @{ Option = 'SEREIN_BUILD_SERIAL_SCOPE'; Source = 'component_packages\SereinScope.SerialScope.sscomponent'; Name = 'serial-scope.sscomponent' }
    'tn160-thermal'     = @{ Option = 'SEREIN_BUILD_TN160';        Source = 'component_packages\SereinScope.TN160.sscomponent';        Name = 'tn160-thermal.sscomponent' }
    'tn160-thermal-ncm' = @{ Option = 'SEREIN_BUILD_TN160_NCM';    Source = 'component_packages\SereinScope.TN160Ncm.sscomponent';    Name = 'tn160-thermal-ncm.sscomponent' }
    't384'              = @{ Option = 'SEREIN_BUILD_T384';          Source = 'component_packages\SereinScope.T384.sscomponent';        Name = 'SereinScope.T384.sscomponent' }
    'yserein-hmi'       = @{ Option = 'SEREIN_BUILD_YSEREIN_HMI';  Source = 'component_packages\SereinScope.YsereinHmi.sscomponent';  Name = 'yserein-hmi.sscomponent' }
    'yserein-universe'  = @{ Option = 'SEREIN_BUILD_YSEREIN_UNIVERSE'; Source = 'component_packages\SereinScope.YsereinUniverse.sscomponent'; Name = 'yserein-universe.sscomponent' }
    'yserein-swarm-sim' = @{ Option = 'SEREIN_BUILD_YSEREIN_SWARM_SIM'; Source = 'component_packages\SereinScope.YSereinSwarmSimulator.sscomponent'; Name = 'yserein-swarm-sim.sscomponent'; Extra = @('Qt6Quick3D.dll','Qt6Quick3DRuntimeRender.dll','Qt6Quick3DUtils.dll','Qt6Quick3DHelpers.dll','Qt6Quick3DHelpersImpl.dll','Qt6QuickWidgets.dll','Qt6Positioning.dll','Qt6Core5Compat.dll','Qt6Concurrent.dll','Qt6ShaderTools.dll','Qt6Core.dll','Qt6Gui.dll','Qt6Qml.dll','Qt6QmlModels.dll','Qt6Quick.dll','Qt6Widgets.dll','Qt6OpenGL.dll','Qt6Network.dll','Qt6Quick3DAssetUtils.dll','Qt6Quick3DEffects.dll','Qt6Quick3DParticleEffects.dll','Qt6Quick3DParticles.dll','Qt6Quick3DSpatialAudio.dll','Qt6Quick3DXr.dll','libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll'); Qml = @('QtQuick3D','Qt5Compat') }
}
$componentBuildTargets = @{
    'serial-scope'      = 'serein_scope_serial_scope'
    'tn160-thermal'     = 'serein_scope_tn160'
    'tn160-thermal-ncm' = 'serein_scope_tn160_ncm'
    't384'              = 'serein_scope_t384'
    'yserein-hmi'       = 'serein_scope_yserein_hmi'
    'yserein-universe'  = 'serein_scope_yserein_universe'
    'yserein-swarm-sim' = 'serein_scope_swarm_sim'
}
if ($BuildOnly -and -not $componentBuildTargets.ContainsKey($BuildOnly)) {
    throw "Unknown build target: $BuildOnly. Valid components: $($componentRegistry.Keys -join ', ')"
}
if ($BuildOnly -and $Components.Count -gt 0) {
    throw '-BuildOnly and -Components have different meanings and cannot be combined.'
}
if ($SdkOnly -and ($BuildOnly -or $Components.Count -gt 0)) {
    throw '-SdkOnly cannot be combined with -BuildOnly or -Components.'
}

# Components that ship by default in the base installer. The default is now all
# developed components, not just the serial scope: building the candidate must
# exercise and ship every registered component unless the caller narrows it via
# -Components.
$baseComponents = @($componentRegistry.Keys)
$selectedComponents = if ($Components.Count -eq 0) { $baseComponents } else { @($Components) }
$unknownComponents = @($selectedComponents | Where-Object { -not ($componentRegistry.Keys -contains $_) })
if ($unknownComponents.Count -gt 0) {
    throw "Unknown component(s): $($unknownComponents -join ', '). Valid choices: $($componentRegistry.Keys -join ', ')"
}
$componentOptions = foreach ($key in $componentRegistry.Keys) {
    $enabled = if ($SdkOnly) { 'OFF' } elseif (-not $BuildOnly -or $key -eq $BuildOnly) { 'ON' } else { 'OFF' }
    "-D$($componentRegistry[$key].Option)=$enabled"
}
if ($SdkOnly) {
    Write-Host '[preflight] Building and packaging Core SDK only.'
} elseif ($BuildOnly) {
    Write-Host "[preflight] Build-only component: $BuildOnly"
} else {
    Write-Host "[preflight] Building components: $($componentRegistry.Keys -join ', ')"
    Write-Host "[preflight] Installing into base delivery: $($selectedComponents -join ', ')"
}

$cmake = 'C:\DOWNLOAD\Qt\Tools\CMake_64\bin\cmake.exe'
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$ninja = 'C:\DOWNLOAD\Qt\Tools\Ninja\ninja.exe'
$mingw = 'C:\DOWNLOAD\Qt\Tools\mingw1310_64\bin'
$qtRoot = 'C:\DOWNLOAD\Qt\6.11.1\mingw_64'
$qtBin = Join-Path $qtRoot 'bin'
$windeployqt = Join-Path $qtBin 'windeployqt.exe'
$mingwRuntimeFiles = @(
    (Join-Path $mingw 'c++.exe'),
    (Join-Path $mingw 'gcc.exe'),
    (Join-Path $mingw 'libwinpthread-1.dll'),
    (Join-Path $mingw 'libgcc_s_seh-1.dll'),
    (Join-Path $mingw 'libstdc++-6.dll')
)

function Get-CandidateProcesses {
    param([Parameter(Mandatory = $true)][string]$ExecutablePath)
    @(Get-Process -Name SereinScope -ErrorAction SilentlyContinue |
        Where-Object {
            try {
                $_.Path -and $_.Path.Equals(
                    $ExecutablePath,
                    [System.StringComparison]::OrdinalIgnoreCase)
            } catch {
                $false
            }
        })
}

function Close-CandidateProcesses {
    param([Parameter(Mandatory = $true)][string]$ExecutablePath)
    $processes = Get-CandidateProcesses -ExecutablePath $ExecutablePath
    if (-not $processes) { return }

    foreach ($process in $processes) {
        Write-Host "[preflight] Requesting graceful close of candidate PID $($process.Id)..."
        if ($process.MainWindowHandle -ne 0) {
            [void]$process.CloseMainWindow()
        }
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 250
        $processes = Get-CandidateProcesses -ExecutablePath $ExecutablePath
    } while ($processes.Count -gt 0 -and [DateTime]::UtcNow -lt $deadline)

    if ($processes.Count -gt 0) {
        $pids = ($processes | ForEach-Object Id) -join ', '
        throw "The candidate SereinScope did not exit after a graceful close request (PID: $pids). Close it manually before retrying: $ExecutablePath"
    }
}

function Remove-DirectoryWithRetry {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $lastError = $null
    for ($attempt = 1; $attempt -le 8; $attempt++) {
        try {
            Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
            if (-not (Test-Path -LiteralPath $Path)) { return }
        } catch {
            $lastError = $_
        }
        Start-Sleep -Milliseconds (150 * $attempt)
    }
    throw "Unable to remove temporary directory after retries: $Path`n$lastError"
}

function Move-DirectoryWithRetry {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    $lastError = $null
    for ($attempt = 1; $attempt -le 8; $attempt++) {
        try {
            $sourceExists = Test-Path -LiteralPath $Source
            $destinationExists = Test-Path -LiteralPath $Destination
            if (-not $sourceExists -and $destinationExists) {
                return
            }
            if (-not $sourceExists) {
                throw "Source directory does not exist: $Source"
            }
            if ($destinationExists) {
                throw "Destination directory still exists: $Destination"
            }
            Move-Item -LiteralPath $Source -Destination $Destination -ErrorAction Stop
            if (-not (Test-Path -LiteralPath $Destination)) {
                throw "Directory move did not complete: $Source -> $Destination"
            }
            return
        } catch {
            $lastError = $_
        }
        Start-Sleep -Milliseconds (200 * $attempt)
    }
    throw "Unable to move directory after retries: $Source -> $Destination`n$lastError"
}

function Write-ForwardSlashZip {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDir,
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$BaseName
    )
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }
    $root = (Resolve-Path -LiteralPath $SourceDir).Path.TrimEnd('\')
    $archive = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $files = Get-ChildItem -LiteralPath $SourceDir -Recurse -File
        foreach ($f in $files) {
            $rel = $f.FullName.Substring($root.Length).TrimStart('\').Replace('\', '/')
            $entryName = "$BaseName/$rel"
            $entry = $archive.CreateEntry($entryName, [System.IO.Compression.CompressionLevel]::Optimal)
            $es = $entry.Open()
            try {
                $fs = [System.IO.File]::OpenRead($f.FullName)
                try { $fs.CopyTo($es) } finally { $fs.Close() }
            } finally {
                $es.Close()
            }
        }
    } finally {
        $archive.Dispose()
    }
}

# 把组件用到的第三方 Qt QML 模块目录复制到组件文件夹里,保证组件自包含。
# Scope 宿主自身 QML 不 import QtQuick3D / Qt5Compat,所以 windeployqt 不会
# 部署这些模块;模拟器组件的 QQuickView 运行时需要它们。
function Copy-ComponentQml {
    param(
        [Parameter(Mandatory = $true)][string]$ComponentDir,
        [Parameter(Mandatory = $true)][string[]]$Modules
    )
    foreach ($mod in $Modules) {
        $src = Join-Path $qtRoot "qml\$mod"
        if (-not (Test-Path -LiteralPath $src)) {
            throw "Component QML module not found in Qt installation: $src"
        }
        $dst = Join-Path $ComponentDir $mod
        Copy-Item -LiteralPath $src -Destination $dst -Recurse -Force
    }
}
# 把组件用到的运行库拷贝进组件文件夹。优先从 Qt bin 目录找，找不到再从
# MinGW 运行时目录找（兼容 Qt libstdc++/libgcc/libwinpthread 等运行库放在
# MinGW 目录、而不在 Qt bin 的情况）。这样组件包才能做到自包含、任意位置
# 部署，不依赖宿主程序目录里恰好摆着同类 DLL。
function Copy-RuntimeDll {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$DestinationDir
    )
    foreach ($candidateRoot in @($qtBin, $mingw)) {
        $src = Join-Path $candidateRoot $Name
        if (Test-Path -LiteralPath $src) {
            Copy-Item -LiteralPath $src -Destination $DestinationDir -Force
            return
        }
    }
    throw "Component runtime DLL not found in Qt bin or MinGW runtime: $Name"
}

function Stage-CoreSdk {
    if (Test-Path -LiteralPath $sdkStagingDir) {
        Remove-DirectoryWithRetry -Path $sdkStagingDir
    }
    & $cmake --install $buildDir --prefix $sdkStagingDir --component SDK
    if ($LASTEXITCODE -ne 0) { throw 'Core SDK installation failed.' }

    foreach ($sdkFile in @(
        (Join-Path $sdkStagingDir 'bin\libserein_scope_core.dll'),
        (Join-Path $sdkStagingDir 'lib\libserein_scope_core.dll.a'),
        (Join-Path $sdkStagingDir 'include\serein\scope\PluginInterface.h'),
        (Join-Path $sdkStagingDir 'include\serein\scope\WorkspaceCanvas.h'),
        (Join-Path $sdkStagingDir 'lib\cmake\SereinScopeCore\SereinScopeCoreConfig.cmake'),
        (Join-Path $sdkStagingDir 'lib\cmake\SereinScopeCore\SereinScopeCoreTargets.cmake'),
        (Join-Path $sdkStagingDir 'examples\component_sdk\CMakeLists.txt'),
        (Join-Path $sdkStagingDir 'SDK_INFO.json'),
        (Join-Path $sdkStagingDir 'README.md'),
        (Join-Path $sdkStagingDir 'LICENSE')
    )) {
        if (-not (Test-Path -LiteralPath $sdkFile)) {
            throw "Core SDK export is incomplete: $sdkFile"
        }
    }

    $sdkInfo = Get-Content -LiteralPath (Join-Path $sdkStagingDir 'SDK_INFO.json') -Raw | ConvertFrom-Json
    if ($sdkInfo.pluginAbi -ne 2 -or -not $sdkInfo.coreVersion) {
        throw 'Core SDK metadata is incomplete or has the wrong plugin ABI.'
    }
    $sdkManifest = foreach ($file in (Get-ChildItem -LiteralPath $sdkStagingDir -Recurse -File | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($sdkStagingDir.Length).TrimStart('\').Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        "$hash  $relative"
    }
    Set-Content -LiteralPath (Join-Path $sdkStagingDir 'SHA256SUMS.txt') `
        -Value $sdkManifest -Encoding Ascii
    Write-ForwardSlashZip -SourceDir $sdkStagingDir -ZipPath $sdkZipStaging -BaseName 'component-sdk'

    # Verify the archive after relocation; the example must compile without
    # reading headers or CMake targets from the repository source tree.
    if (Test-Path -LiteralPath $sdkVerifyDir) {
        Remove-DirectoryWithRetry -Path $sdkVerifyDir
    }
    New-Item -ItemType Directory -Path $sdkVerifyDir | Out-Null
    [System.IO.Compression.ZipFile]::ExtractToDirectory($sdkZipStaging, $sdkVerifyDir)
    $extractedSdk = Join-Path $sdkVerifyDir 'component-sdk'
    foreach ($line in (Get-Content -LiteralPath (Join-Path $extractedSdk 'SHA256SUMS.txt'))) {
        if ($line -notmatch '^([A-Fa-f0-9]{64})  (.+)$') {
            throw "Malformed Core SDK checksum line: $line"
        }
        $file = Join-Path $extractedSdk $Matches[2]
        if (-not (Test-Path -LiteralPath $file) -or
            (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $Matches[1]) {
            throw "Core SDK archive checksum mismatch: $file"
        }
    }
    if (Test-Path -LiteralPath $sdkSmokeDir) {
        Remove-DirectoryWithRetry -Path $sdkSmokeDir
    }
    & $cmake -S (Join-Path $extractedSdk 'examples\component_sdk') -B $sdkSmokeDir -G Ninja `
        '-DCMAKE_BUILD_TYPE=Release' `
        "-DCMAKE_PREFIX_PATH=$qtRoot" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCMAKE_CXX_COMPILER=$(Join-Path $mingw 'c++.exe')" `
        "-DSEREIN_SCOPE_SDK_PREFIX=$extractedSdk"
    if ($LASTEXITCODE -ne 0) { throw 'Core SDK archive example configuration failed.' }
    & $cmake --build $sdkSmokeDir --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Core SDK archive example build failed.' }
    if (-not (Test-Path -LiteralPath (Join-Path $sdkSmokeDir 'component_packages\SereinScope.Template.sscomponent'))) {
        throw 'Core SDK archive example produced no .sscomponent file.'
    }
    Remove-DirectoryWithRetry -Path $sdkVerifyDir
}

function Publish-CoreSdk {
    if (Test-Path -LiteralPath $sdkPreviousDir) {
        Remove-DirectoryWithRetry -Path $sdkPreviousDir
    }
    if (Test-Path -LiteralPath $sdkDir) {
        Move-DirectoryWithRetry -Source $sdkDir -Destination $sdkPreviousDir
    }
    try {
        Move-DirectoryWithRetry -Source $sdkStagingDir -Destination $sdkDir
    } catch {
        if (Test-Path -LiteralPath $sdkPreviousDir) {
            Move-DirectoryWithRetry -Source $sdkPreviousDir -Destination $sdkDir
        }
        throw
    }
    if (Test-Path -LiteralPath $sdkPreviousDir) {
        Remove-DirectoryWithRetry -Path $sdkPreviousDir
    }
    Move-Item -LiteralPath $sdkZipStaging -Destination $sdkZip -Force
    $zipHash = (Get-FileHash -LiteralPath $sdkZip -Algorithm SHA256).Hash
    Set-Content -LiteralPath "$sdkZip.sha256.txt" -Value "$zipHash  component-sdk.zip" -Encoding Ascii
    Write-Host "[OK] Core SDK: $sdkDir"
    Write-Host "[OK] Core SDK archive: $sdkZip"
}

$sdkDir = Join-Path $repo 'out\component-sdk'
$sdkStagingDir = Join-Path $repo 'out\.component-sdk-staging'
$sdkPreviousDir = Join-Path $repo 'out\.component-sdk-previous'
$sdkZip = Join-Path $repo 'out\component-sdk.zip'
$sdkZipStaging = Join-Path $repo 'out\.component-sdk-staging.zip'
$sdkVerifyDir = Join-Path $repo 'out\.component-sdk-verify'
$sdkSmokeDir = Join-Path $buildDir 'component-sdk-package-smoke'
$sdkTemplateStore = Join-Path $repo 'out\.sdk-template-cycle'


foreach ($required in @($cmake, $ctest, $ninja, $windeployqt)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required build tool not found: $required"
    }
}
foreach ($required in $mingwRuntimeFiles) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "MinGW compiler/runtime file not found: $required. The build was not started."
    }
}

$stableHashBefore = if (Test-Path -LiteralPath $stableComponent) {
    (Get-FileHash -LiteralPath $stableComponent -Algorithm SHA256).Hash
} else {
    ''
}

$env:Path = "$mingw;$qtBin;$env:Path"
Push-Location $repo
try {
    if ($SdkOnly) {
        Write-Host '[sdk-only] Refreshing the Release build configuration...'
    } else {
        Write-Host '[1/8] Refreshing the existing Release build configuration...'
    }
    & $cmake -S $repo -B $buildDir -G Ninja `
        '-DCMAKE_BUILD_TYPE=Release' `
        "-DCMAKE_PREFIX_PATH=$qtRoot" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCMAKE_C_COMPILER=$(Join-Path $mingw 'gcc.exe')" `
        "-DCMAKE_CXX_COMPILER=$(Join-Path $mingw 'c++.exe')" `
        @componentOptions
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if ($SdkOnly) {
        Write-Host '[sdk-only] Building Core and verifying the standalone SDK archive...'
        & $cmake --build $buildDir --target serein_scope_core --parallel
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Stage-CoreSdk
        Publish-CoreSdk
        return
    }

    if ($BuildOnly) {
        Write-Host "[build-only] Building $BuildOnly and its CMake dependencies..."
        & $cmake --build $buildDir --target $componentBuildTargets[$BuildOnly] --parallel
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Write-Host "[OK] Built only $BuildOnly. Candidate and component packs were not changed."
        return
    }

    Close-CandidateProcesses -ExecutablePath $candidateExe
    Close-CandidateProcesses -ExecutablePath $stagingExe

    Write-Host '[2/8] Building the complete host, core, and components...'
    & $cmake --build $buildDir --parallel
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    # The same one-line user action must verify the plugin update path before
    # assembling or replacing a candidate installer.
    $pluginTestExe = Join-Path $buildDir 'bin\plugin_loading_tests.exe'
    if (-not (Test-Path -LiteralPath $pluginTestExe)) {
        throw "Plugin lifecycle test binary missing: $pluginTestExe"
    }
    Write-Host '[2/8] Running plugin, simulator protocol, and box dance tests...'
    & $ctest --test-dir $buildDir -R '^(plugin_loading_tests|swarm_sim_mavlink_protocol_tests|swarm_sim_box_dance_tests)$' --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Stage-CoreSdk

    $hostExe = Join-Path $buildDir 'bin\SereinScope.exe'
    $coreDll = Join-Path $buildDir 'bin\libserein_scope_core.dll'
    $componentSources = [ordered]@{}
    foreach ($key in $selectedComponents) {
        $entry = $componentRegistry[$key]
        $componentSources[$entry.Name] = (Join-Path $buildDir $entry.Source)
    }
    $requiredArtifacts = @($hostExe, $coreDll)
    $requiredArtifacts += @($componentSources.Values)
    foreach ($required in $requiredArtifacts) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "Required build artifact not found: $required"
        }
    }

    Write-Host '[3/8] Assembling the candidate: host + self-contained component dirs...'
    if (Test-Path -LiteralPath $stagingDir) {
        Remove-DirectoryWithRetry -Path $stagingDir
    }
    New-Item -ItemType Directory -Path $candidateAppDir -Force | Out-Null
    New-Item -ItemType Directory -Path $candidateComponentsDir -Force | Out-Null
    Copy-Item -LiteralPath $hostExe -Destination $candidateAppDir
    Copy-Item -LiteralPath $coreDll -Destination $candidateAppDir
    Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination $candidateAppDir
    foreach ($key in $selectedComponents) {
        $entry = $componentRegistry[$key]
        $componentDir = Join-Path $candidateComponentsDir $key
        $componentSource = Join-Path $buildDir $entry.Source
        $componentDest = Join-Path $componentDir $entry.Name
        if (-not (Test-Path -LiteralPath $componentSource)) {
            throw "Component source not found: $componentSource"
        }
        New-Item -ItemType Directory -Path $componentDir -Force | Out-Null
        Copy-Item -LiteralPath $componentSource -Destination $componentDest
        if (-not (Test-Path -LiteralPath $componentDest)) {
            throw "Component copy produced no file. Source: $componentSource Dest: $componentDest"
        }
        if ($entry.Extra) {
            foreach ($dll in $entry.Extra) {
                Copy-RuntimeDll -Name $dll -DestinationDir $componentDir
            }
        }
        if ($entry.Qml) {
            Copy-ComponentQml -ComponentDir $componentDir -Modules $entry.Qml
        }
    }

    Write-Host '[4/8] Deploying Qt runtime dependencies into the candidate...'
    $candidateExe = Join-Path $candidateAppDir 'SereinScope.exe'
    $qmlDir = Join-Path $repo 'src\app\qml'
    & $windeployqt --release --compiler-runtime --no-translations `
        --qmldir $qmlDir $candidateExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Set-Content -LiteralPath (Join-Path $candidateAppDir 'qt.conf') `
        -Value @('[Paths]', 'Plugins=.', 'Qml2Imports=qml') -Encoding Ascii

    # Extra Qt modules needed only by heavy components (e.g. Quick3D for the
    # simulator) are copied into each component's own folder above, so the
    # host directory stays clean and every component remains self-contained.

    foreach ($runtimeFile in @(
        (Join-Path $candidateAppDir 'Qt6Core.dll'),
        (Join-Path $candidateAppDir 'Qt6Gui.dll'),
        (Join-Path $candidateAppDir 'Qt6Widgets.dll'),
        (Join-Path $candidateAppDir 'platforms\qwindows.dll'),
        (Join-Path $candidateAppDir 'qt.conf')
    )) {
        if (-not (Test-Path -LiteralPath $runtimeFile)) {
            throw "Required deployed Qt runtime file not found: $runtimeFile"
        }
    }

    Write-Host '[5/8] Running native lifecycle smoke checks from the candidate...'
    & $candidateExe --smoke-test "--component-dir=$candidateComponentsDir"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    foreach ($componentId in $selectedComponents) {
        & $candidateExe "--component-cycle-test=$componentId" `
            "--component-dir=$candidateComponentsDir"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    if (Test-Path -LiteralPath $sdkTemplateStore) {
        Remove-DirectoryWithRetry -Path $sdkTemplateStore
    }
    $templatePackageDir = Join-Path $sdkTemplateStore 'template-component'
    New-Item -ItemType Directory -Path $templatePackageDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $sdkSmokeDir 'component_packages\SereinScope.Template.sscomponent') `
        -Destination (Join-Path $templatePackageDir 'template-component.sscomponent')
    & $candidateExe '--component-cycle-test=template-component' `
        "--component-dir=$sdkTemplateStore"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Remove-DirectoryWithRetry -Path $sdkTemplateStore

    Write-Host '[6/8] Writing the candidate SHA256 manifest...'
    $manifestTargets = @(
        $candidateExe,
        (Join-Path $candidateAppDir 'libserein_scope_core.dll')
    )
    foreach ($key in $selectedComponents) {
        $entry = $componentRegistry[$key]
        $componentDir = Join-Path $candidateComponentsDir $key
        $manifestTargets += (Join-Path $componentDir $entry.Name)
        if ($entry.Extra) {
            foreach ($dll in $entry.Extra) {
                $manifestTargets += (Join-Path $componentDir $dll)
            }
        }
    }
    $manifest = foreach ($target in $manifestTargets) {
        if (-not (Test-Path -LiteralPath $target)) {
            throw "Manifest target missing before hashing: $target`nThis file was present after [3/8] but disappeared before [6/8]. Check that smoke tests did not consume the candidate Components directory."
        }
        $relative = $target.Substring($stagingDir.Length).TrimStart('\')
        $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
        "$hash  $relative"
    }
    Set-Content -LiteralPath (Join-Path $stagingDir 'SHA256SUMS.txt') `
        -Value $manifest -Encoding Ascii

    Write-Host '[7/8] Publishing the complete candidate without touching run-win...'
    if (Test-Path -LiteralPath $previousCandidateDir) {
        Remove-DirectoryWithRetry -Path $previousCandidateDir
    }
    if (Test-Path -LiteralPath $candidateDir) {
        Move-DirectoryWithRetry -Source $candidateDir -Destination $previousCandidateDir
    }
    try {
        Move-DirectoryWithRetry -Source $stagingDir -Destination $candidateDir
    } catch {
        if (Test-Path -LiteralPath $previousCandidateDir) {
            Move-DirectoryWithRetry -Source $previousCandidateDir -Destination $candidateDir
        }
        throw
    }
    if (Test-Path -LiteralPath $previousCandidateDir) {
        Remove-DirectoryWithRetry -Path $previousCandidateDir
    }

    $stableHashAfter = if (Test-Path -LiteralPath $stableComponent) {
        (Get-FileHash -LiteralPath $stableComponent -Algorithm SHA256).Hash
    } else {
        ''
    }
    if ($stableHashAfter -ne $stableHashBefore) {
        throw 'Frozen out\run-win component changed unexpectedly.'
    }

    Publish-CoreSdk

    # Every developed component gets a self-contained zip pack for independent
    # distribution, whether or not it ships in the base installer. The user
    # imports the zip in-app ("导入插件"), or drops the extracted folder into
    # the components store: %LOCALAPPDATA%\Serein\Serein Scope\components\<id>.
    $packRoot = Join-Path $repo 'out\component-packs'
    New-Item -ItemType Directory -Path $packRoot -Force | Out-Null
    foreach ($key in $componentRegistry.Keys) {
        $entry = $componentRegistry[$key]
        $componentSource = Join-Path $buildDir $entry.Source
        if (-not (Test-Path -LiteralPath $componentSource)) {
            throw "Component pack source not found: $componentSource"
        }
        $packDir = Join-Path $packRoot $key
        if (Test-Path -LiteralPath $packDir) {
            Remove-DirectoryWithRetry -Path $packDir
        }
        New-Item -ItemType Directory -Path $packDir -Force | Out-Null
        Copy-Item -LiteralPath $componentSource -Destination (Join-Path $packDir $entry.Name)
        if ($entry.Extra) {
            foreach ($dll in $entry.Extra) {
                Copy-RuntimeDll -Name $dll -DestinationDir $packDir
            }
        }
        if ($entry.Qml) {
            Copy-ComponentQml -ComponentDir $packDir -Modules $entry.Qml
        }
        $zipPath = Join-Path $packRoot "$key.zip"
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        # 手动创建 zip 条目,强制用正斜杠。.NET ZipFile 和 Compress-Archive 在
        # Windows 上生成的 zip 内部都用反斜杠分隔路径,Qt 的 QZipReader 读不了。
        Write-ForwardSlashZip -SourceDir $packDir -ZipPath $zipPath -BaseName $key
        Write-Host "[OK] Component pack zip: $zipPath"
    }
    Write-Host "[OK] Component packs for all developed components: $packRoot"

    if ($SkipInstaller) {
        Write-Host '[OK] Installer skipped (-SkipInstaller).'
    } else {
        Write-Host '[8/8] Building the Windows installer...'
        & (Join-Path $PSScriptRoot 'package_windows_installer.ps1') `
            -CandidateDir $candidateDir `
            -OutputDir (Join-Path $repo 'out')
    }

    Write-Host '[OK] Complete candidate ready.'
    Write-Host "Candidate: $candidateDir"
    Write-Host "Executable: $(Join-Path $candidateDir 'SereinScope.exe')"
    Write-Host "Manifest: $(Join-Path $candidateDir 'SHA256SUMS.txt')"
    if ($stableHashAfter) {
        Write-Host "Frozen run-win NCM SHA256: $stableHashAfter"
    }
} finally {
    if (-not $SdkOnly -and (Test-Path -LiteralPath $stagingDir)) {
        Remove-DirectoryWithRetry -Path $stagingDir
    }
    if ($sdkStagingDir -and (Test-Path -LiteralPath $sdkStagingDir)) {
        Remove-DirectoryWithRetry -Path $sdkStagingDir
    }
    if ($sdkZipStaging -and (Test-Path -LiteralPath $sdkZipStaging)) {
        Remove-Item -LiteralPath $sdkZipStaging -Force
    }
    if ($sdkVerifyDir -and (Test-Path -LiteralPath $sdkVerifyDir)) {
        Remove-DirectoryWithRetry -Path $sdkVerifyDir
    }
    if ($sdkTemplateStore -and (Test-Path -LiteralPath $sdkTemplateStore)) {
        Remove-DirectoryWithRetry -Path $sdkTemplateStore
    }
    Pop-Location
}
