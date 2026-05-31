<#
.SYNOPSIS
    Bake the Android (Vulkan 1.2) SPIR-V shader cache on the desktop and stage it into the APK.

.DESCRIPTION
    Android ships PhasmaPlayer with no runtime shader compiler (PE_ENABLE_RUNTIME_SHADER_COMPILER=OFF),
    so the engine can only ReadSpvFile from a pre-populated cache. This script:

      1. Runs a desktop Vulkan PhasmaPlayer with PHASMA_SPIRV_TARGET=1.2 so it compiles every shader
         with the *same* SPIR-V target (and therefore the same portable cache key) the device uses.
      2. Harvests the resulting ShaderCache/_spv/* blobs into the APK staging dir
         (PhasmaPlayer/android/prebaked/ShaderCache/_spv), which gradle bundles and the Activity
         extracts to <internalStorage>/ShaderCache/ on device.
      3. Disassembles every baked blob and FAILS if any declares "OpCapability Int64" - the
         evidence gate that keeps the shaderInt64 warn-on-Android downgrade (RHI.cpp) provably safe.

    PREREQUISITE: rebuild the desktop bake host AFTER any engine change so its portable cache hash
    matches the Android build:
        cmake --build build-ninja-full --config Release --target PhasmaPlayer

.NOTES
    The cache key folds in the SPIR-V target version and is computed with a portable FNV-1a hash
    (PhasmaCore/Code/Base/Hash.h). Both the desktop bake host and the Android build must be built
    from the same engine revision for the keys to match.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "build-ninja-full",
    [string]$Config = "Release",
    [string]$Scene = "Assets/Scenes/new.pescene",
    [int]$WaitSeconds = 25,
    [string]$VulkanSdkBin = $(if ($env:VULKAN_SDK) { Join-Path $env:VULKAN_SDK "Bin" } else { "" })
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$binDir = Join-Path $repoRoot "$BuildDir/$Config"
$player = Join-Path $binDir "PhasmaPlayer.exe"
$assetsDir = Join-Path $binDir "Assets"
$cacheSpv = Join-Path $binDir "ShaderCache/_spv"
$logFile = Join-Path $binDir "PhasmaEngine.log"
$prebaked = Join-Path $repoRoot "PhasmaPlayer/android/prebaked/ShaderCache/_spv"
$spirvDis = if ($VulkanSdkBin) { Join-Path $VulkanSdkBin "spirv-dis.exe" } else { "spirv-dis" }

function Fail([string]$msg) { Write-Host "[bake] ERROR: $msg" -ForegroundColor Red; exit 1 }

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

# 1. Point the player at the target scene (so its pass/define set matches the device). Even an
#    empty scene compiles the full non-RT pass set at renderer.Init(), but matching the real scene
#    keeps the bake faithful.
$editorConfig = Join-Path $assetsDir "editor_config.json"
Set-Content -Path $editorConfig -Value (@{ last_scene = $Scene } | ConvertTo-Json -Compress) -Encoding utf8
Set-Content -Path (Join-Path $binDir "phasma_settings.json") -Value (@{ graphics_api = "vulkan" } | ConvertTo-Json -Compress) -Encoding utf8

# 2. Clear the desktop cache so we harvest exactly this run.
if (Test-Path $cacheSpv) { Remove-Item -Recurse -Force $cacheSpv }

# 3. Run the bake host with the SPIR-V target pinned to the Android value.
$env:PHASMA_SPIRV_TARGET = "1.2"
$env:PHASMA_API = "vulkan"
Write-Host "[bake] launching PhasmaPlayer (PHASMA_SPIRV_TARGET=1.2, --api vulkan); waiting ${WaitSeconds}s for shader compile..."
$proc = Start-Process $player -ArgumentList '--api', 'vulkan' -WorkingDirectory $binDir -PassThru
Start-Sleep -Seconds $WaitSeconds
if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
    Write-Host "[bake] bake host still running after ${WaitSeconds}s (expected) -> stopped."
}
else {
    Write-Host "[bake] WARNING: bake host exited early (code=0x$('{0:X8}' -f $proc.ExitCode)). Check $logFile." -ForegroundColor Yellow
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
    $dis = & $spirvDis $b.FullName 2>$null
    if ($LASTEXITCODE -ne 0) { Fail "spirv-dis failed on $($b.Name) (not valid SPIR-V?)." }
    foreach ($m in ([regex]::Matches(($dis -join "`n"), 'OpCapability (\w+)'))) {
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
Write-Host "[bake] Next: cd PhasmaPlayer/android ; .\gradlew.bat :app:assembleDebug"
