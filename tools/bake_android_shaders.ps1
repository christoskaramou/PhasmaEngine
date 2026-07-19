<#
.SYNOPSIS
    Bake the Android (Vulkan 1.2) SPIR-V shader cache on the desktop and stage it into the APK.

.DESCRIPTION
    Android ships PhasmaPlayer with no runtime shader compiler (PE_ENABLE_RUNTIME_SHADER_COMPILER=OFF),
    so the engine can only ReadSpvFile from a pre-populated cache. This script:

      1. Runs a desktop Vulkan PhasmaPlayer with PHASMA_SPIRV_TARGET=1.2 so it compiles shaders
         with the *same* SPIR-V target (and therefore the same portable cache key) the device uses.
         By default this includes temporary scene variants that cover the Android HUD's runtime
         pass toggles, plus the TAA-off/Upsample path.
      2. Harvests the resulting ShaderCache/_spv/* blobs into the APK staging dir
         (Phasma/Player/android/prebaked/ShaderCache/_spv), which gradle bundles and the Activity
         extracts to <internalStorage>/ShaderCache/ on device.
      3. Disassembles every baked blob and FAILS if any declares a capability whose Vulkan feature
         is softened to warn-on-Android in RHI.cpp.

    PREREQUISITE: rebuild the desktop bake host AFTER any engine change so its portable cache hash
    matches the Android build:
        cmake --build build-ninja-full --config Release --target PhasmaPlayer

.NOTES
    The cache key folds in the SPIR-V target version and is computed with a portable FNV-1a hash
    (Phasma/Core/Code/Base/Hash.h). Both the desktop bake host and the Android build must be built
    from the same engine revision for the keys to match.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "build-ninja-full",
    [string]$Config = "Release",
    [string]$Scene = "Assets/Scenes/new.pescene",
    [int]$WaitSeconds = 25,
    [string]$VulkanSdkBin = $(if ($env:VULKAN_SDK) { Join-Path $env:VULKAN_SDK "Bin" } else { "" }),
    [switch]$SkipRuntimeToggleVariants
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$binDir = Join-Path $repoRoot "$BuildDir/$Config"
$player = Join-Path $binDir "PhasmaPlayer.exe"
$assetsDir = Join-Path $binDir "Assets"
$cacheSpv = Join-Path $binDir "ShaderCache/_spv"
$logFile = Join-Path $binDir "PhasmaEngine.log"
$prebaked = Join-Path $repoRoot "Phasma/Player/android/prebaked/ShaderCache/_spv"
$spirvDis = if ($VulkanSdkBin) { Join-Path $VulkanSdkBin "spirv-dis.exe" } else { "spirv-dis" }

function Fail([string]$msg) { Write-Host "[bake] ERROR: $msg" -ForegroundColor Red; exit 1 }

function Write-TextUtf8NoBom([string]$path, [string]$content) {
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($path, $content, $encoding)
}

function Set-JsonProperty($obj, [string]$name, $value) {
    if ($obj.PSObject.Properties.Name -contains $name) {
        $obj.$name = $value
    }
    else {
        $obj | Add-Member -NotePropertyName $name -NotePropertyValue $value
    }
}

function New-BakeSceneVariant([string]$sourceScene, [string]$suffix, [hashtable]$settings) {
    $sourceScenePath = Join-Path $binDir $sourceScene
    if (-not (Test-Path $sourceScenePath)) { Fail "Scene not found at $sourceScenePath" }

    $sceneJson = Get-Content $sourceScenePath -Raw | ConvertFrom-Json
    if (-not ($sceneJson.PSObject.Properties.Name -contains "settings") -or -not $sceneJson.settings) {
        $sceneJson | Add-Member -NotePropertyName "settings" -NotePropertyValue ([pscustomobject]@{})
    }

    foreach ($key in $settings.Keys) {
        Set-JsonProperty $sceneJson.settings $key $settings[$key]
    }

    $sceneDir = Split-Path $sourceScene -Parent
    $variantFileName = "__android_bake_$suffix.pescene"
    $variantScene = if ($sceneDir) { (Join-Path $sceneDir $variantFileName) } else { $variantFileName }
    $variantScene = $variantScene -replace "\\", "/"
    $variantPath = Join-Path $binDir $variantScene

    Write-TextUtf8NoBom $variantPath ($sceneJson | ConvertTo-Json -Depth 100)
    return $variantScene
}

function Read-SpirvDisassembly([string]$toolPath, [string]$blobPath) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $toolPath
    $psi.Arguments = '"' + $blobPath + '"'
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false

    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()

    if ($proc.ExitCode -ne 0) {
        if (-not [string]::IsNullOrWhiteSpace($stderr)) { Write-Host $stderr.Trim() }
        Fail "spirv-dis failed on $(Split-Path $blobPath -Leaf) (not valid SPIR-V?)."
    }

    return $stdout
}

Write-Host "[bake] repo            : $repoRoot"
Write-Host "[bake] bake host       : $player"
Write-Host "[bake] scene           : $Scene"
Write-Host "[bake] spirv-dis       : $spirvDis"
Write-Host "[bake] staging target  : $prebaked"

if (-not (Test-Path $player)) {
    Fail "PhasmaPlayer.exe not found at $player. Build it first: cmake --build $BuildDir --config $Config --target PhasmaPlayer"
}
if (-not (Test-Path $assetsDir)) { Fail "Assets dir not found at $assetsDir (the bake host needs the shader sources + .pass assets)." }
if (-not (Get-Command $spirvDis -ErrorAction SilentlyContinue) -and -not (Test-Path $spirvDis)) {
    Fail "spirv-dis not found. Set `$env:VULKAN_SDK or pass -VulkanSdkBin."
}

# Android ships tracked local scene files under Phasma/Player/android/app/src/main/assets/Assets.
# If the requested scene exists there, copy it into the desktop bake host so global render settings
# (cascade count, shadow-map size, TAA/CAS defaults, etc.) match the APK instead of a local editor scene.
$androidSceneSource = Join-Path $repoRoot ("Phasma/Player/android/app/src/main/assets/" + $Scene)
$bakeHostScene = Join-Path $binDir $Scene
if (Test-Path $androidSceneSource) {
    Copy-Item -Path $androidSceneSource -Destination $bakeHostScene -Force
    Write-Host "[bake] copied Android-local scene into bake host: $Scene"
}

# 1. Build the scene list. Runtime HUD toggles can enable passes after startup; with the Android
#    runtime compiler off, those lazy paths need to be pre-baked too. The second variant bakes the
#    non-TAA path so toggling TAA off has an Upsample shader in the cache.
$editorConfig = Join-Path $assetsDir "editor_config.json"
Set-Content -Path (Join-Path $binDir "phasma_settings.json") -Value (@{ graphics_api = "vulkan" } | ConvertTo-Json -Compress) -Encoding utf8

$bakeScenes = @($Scene)
$temporaryScenes = @()
if (-not $SkipRuntimeToggleVariants) {
    $runtimeToggleSettings = @{
        shadows        = $true
        ssr            = $true
        fxaa           = $true
        taa            = $true
        cas_sharpening = $true
        tonemapping    = $true
        bloom          = $true
        dof            = $true
        motion_blur    = $true
        ssao           = $false
    }
    $runtimeToggleNoTaaSettings = $runtimeToggleSettings.Clone()
    $runtimeToggleNoTaaSettings["taa"] = $false
    $runtimeToggleNoTaaSettings["cas_sharpening"] = $false

    $allTogglesScene = New-BakeSceneVariant $Scene "runtime_toggles_on" $runtimeToggleSettings
    $noTaaScene = New-BakeSceneVariant $Scene "runtime_toggles_no_taa" $runtimeToggleNoTaaSettings
    $temporaryScenes += @($allTogglesScene, $noTaaScene)
    $bakeScenes = @($allTogglesScene, $noTaaScene)
}

# 2. Clear the desktop cache so we harvest exactly these runs.
if (Test-Path $cacheSpv) { Remove-Item -Recurse -Force $cacheSpv }

# 3. Run the bake host with the SPIR-V target pinned to the Android value.
$env:PHASMA_SPIRV_TARGET = "1.2"
$env:PHASMA_API = "vulkan"
foreach ($bakeScene in $bakeScenes) {
    Set-Content -Path $editorConfig -Value (@{ last_scene = $bakeScene } | ConvertTo-Json -Compress) -Encoding utf8
    Write-Host "[bake] launching PhasmaPlayer scene=$bakeScene (PHASMA_SPIRV_TARGET=1.2, --api vulkan); waiting ${WaitSeconds}s for shader compile..."
    $proc = Start-Process $player -ArgumentList '--api', 'vulkan' -WorkingDirectory $binDir -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds $WaitSeconds
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "[bake] bake host still running after ${WaitSeconds}s (expected) -> stopped."
    }
    else {
        Write-Host "[bake] WARNING: bake host exited early (code=0x$('{0:X8}' -f $proc.ExitCode)). Check $logFile." -ForegroundColor Yellow
    }
}

foreach ($temporaryScene in $temporaryScenes) {
    $temporaryPath = Join-Path $binDir $temporaryScene
    if (Test-Path $temporaryPath) { Remove-Item -Force $temporaryPath }
}

# 4. Harvest.
if (-not (Test-Path $cacheSpv)) {
    if (Test-Path $logFile) { Write-Host "---- PhasmaEngine.log (tail) ----"; Get-Content $logFile -Tail 20 }
    Fail "No ShaderCache/_spv produced at $cacheSpv. Did the player run Vulkan and reach renderer.Init()?"
}
$blobs = @(Get-ChildItem -File $cacheSpv)
if ($blobs.Count -eq 0) {
    if (Test-Path $logFile) { Write-Host "---- PhasmaEngine.log (tail) ----"; Get-Content $logFile -Tail 20 }
    Fail "ShaderCache/_spv is empty."
}
if (Test-Path $prebaked) { Remove-Item -Recurse -Force $prebaked }
New-Item -ItemType Directory -Force -Path $prebaked | Out-Null
Copy-Item -Path (Join-Path $cacheSpv '*') -Destination $prebaked -Force
Write-Host "[bake] harvested $($blobs.Count) SPIR-V blob(s) -> $prebaked"

# 5. Evidence gate: the RHI device-feature contract (RHI.cpp) softens a set of Vulkan features to
#    warn-on-Android on the premise that the shipped shaders never use them. Enforce that premise:
#    if any baked blob declares the matching SPIR-V capability, the corresponding feature must be
#    promoted back to RequireVulkanFeature, so fail the bake and name it. Keeps the "works on any
#    Android GPU" contract honest as shaders evolve.
$softenedCapToFeature = [ordered]@{
    'Int64'                                = 'shaderInt64'
    'Int16'                                = 'shaderInt16'
    'Float16'                              = 'shaderFloat16'
    'StorageBufferArrayNonUniformIndexing' = 'shaderStorageBufferArrayNonUniformIndexing'
}
$allCaps = @{}
$violations = @()
foreach ($b in (Get-ChildItem -File $prebaked)) {
    $dis = Read-SpirvDisassembly $spirvDis $b.FullName
    foreach ($m in ([regex]::Matches($dis, 'OpCapability (\w+)'))) {
        $cap = $m.Groups[1].Value
        if (-not $allCaps.ContainsKey($cap)) { $allCaps[$cap] = 0 }
        $allCaps[$cap]++
        if ($softenedCapToFeature.Contains($cap)) { $violations += "$($b.Name): $cap (needs $($softenedCapToFeature[$cap]))" }
    }
}

Write-Host ""
Write-Host "[bake] === summary ===" -ForegroundColor Cyan
Write-Host "[bake] baked blobs : $($blobs.Count)  (SPIR-V target vulkan1.2)"
Write-Host "[bake] SPIR-V capabilities across blobs (count = #blobs):"
$allCaps.GetEnumerator() | Sort-Object Name | ForEach-Object { Write-Host ("[bake]   {0,-40} {1}" -f $_.Name, $_.Value) }
if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Host "[bake]   VIOLATION $_" -ForegroundColor Red }
    Fail "Baked SPIR-V uses a capability whose Vulkan feature is softened to warn-on-Android in RHI.cpp. Promote that feature back to RequireVulkanFeature (or drop the shader usage) - otherwise Android GPUs that lack it will warn then crash."
}
Write-Host "[bake] softened-feature capabilities (Int64/Int16/Float16/StorageBufferNonUniform): NONE -> warn-on-Android gates are SAFE." -ForegroundColor Green
Write-Host "[bake] Next: cd Phasma/Player/android ; .\gradlew.bat :app:assembleDebug"
