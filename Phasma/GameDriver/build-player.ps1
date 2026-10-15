$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$build = Join-Path $repoRoot 'build-jev-player'
$log = Join-Path $build 'build.log'
$dependencies = Join-Path $repoRoot '.fetchcontent-ninja'

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Install the Visual Studio C++ x64 tools or run this script from their developer shell.'
    }

    $visualStudio = & $vswhere -latest -prerelease -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) {
        throw 'Visual Studio C++ x64 tools were not found.'
    }

    $vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
    $command = 'call "{0}" >nul && powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{1}"' -f `
        $vcvars, $PSCommandPath
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Player build failed ($LASTEXITCODE). See $log"
    }
    return
}

$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ninja = (Get-Command ninja.exe -ErrorAction Stop).Source
$configure = @(
    '-S', $repoRoot, '-B', $build, '-G', 'Ninja Multi-Config',
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    '-DCMAKE_CONFIGURATION_TYPES=Release',
    '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',
    "-DFETCHCONTENT_BASE_DIR=$build/_deps",
    '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
    '-DFETCHCONTENT_UPDATES_DISCONNECTED=ON',
    '-DPE_BUILD_PLAYER=ON',
    '-DPE_BUILD_MCP=ON',
    '-DPE_BUILD_EDITOR=OFF',
    '-DPE_BUILD_LAUNCHER=OFF',
    '-DPE_BUILD_ANIMATOR=OFF',
    '-DPE_BUILD_EXPORTER=OFF',
    '-DPE_BUILD_PROFILER=OFF',
    '-DPE_WEBGPU=OFF',
    '-DPE_PHYSICS=ON',
    '-DPE_PHYSICS2D=ON',
    '-DPE_NAV=ON',
    '-DPE_AUDIO=ON',
    '-DPE_TLS=ON',
    '-DPE_ENABLE_ASSIMP=ON',
    '-DPE_MATERIALX=ON',
    '-DPE_PMCP_CODEBASE=OFF',
    '-DPE_PROJECT_NATIVE=OFF'
)

# FetchContent's source overrides reuse cached checkouts without sharing their build directories.
foreach ($name in @('JoltPhysics', 'Box2D', 'MaterialX', 'RecastNavigation', 'MbedTLS', 'meshoptimizer')) {
    $source = Join-Path $dependencies ($name.ToLowerInvariant() + '-src')
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Cached dependency missing: $source. Populate the engine dependencies before building offline."
    }
    $configure += "-DFETCHCONTENT_SOURCE_DIR_$($name.ToUpperInvariant())=$source"
}

$slang = Get-Command slangc.exe -ErrorAction SilentlyContinue
$slangPath = if ($slang) { $slang.Source } else { Join-Path $repoRoot '../slang-bin/bin/slangc.exe' }
if (Test-Path -LiteralPath $slangPath) {
    $configure += "-DPE_SLANGC_PATH=$([IO.Path]::GetFullPath($slangPath))"
}

New-Item -ItemType Directory -Path $build -Force | Out-Null
Write-Host "Configuring private PhasmaPlayer build. Log: $log"
$ErrorActionPreference = 'Continue'
& $cmake @configure *> $log
$configureExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($configureExit -ne 0) {
    Get-Content -LiteralPath $log -Tail 25 | Write-Host
    throw "Player configure failed ($configureExit). See $log"
}

Write-Host 'Building Release player with physics, navigation, audio and MCP.'
$ErrorActionPreference = 'Continue'
& $cmake --build $build --config Release --target PhasmaPlayer --parallel 4 *>> $log
$buildExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($buildExit -ne 0) {
    Get-Content -LiteralPath $log -Tail 25 | Write-Host
    throw "Player build failed ($buildExit). See $log"
}

Write-Host "Player ready: $build/Release/PhasmaPlayer.exe"
