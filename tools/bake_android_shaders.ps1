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

    The script rebuilds the desktop bake host before launching it, then writes the content manifest
    Gradle uses to reject a missing or stale cache before APK asset staging.

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
    [switch]$SkipRuntimeToggleVariants,
    # A game project to bake as well. Its scenes run in a temporary copy of the project laid out the way the APK stages
    # it (the engine's RuntimeAssets Shaders and PassInfo win, the game's own files fill the gaps), so every cache key is
    # the one the device computes; the game's own copies of engine shaders never reach the device. -ProjectWaitSeconds
    # gives each -ProjectScene its run time, and -ProjectEnv (NAME=value) sets launch options for those runs, such as a
    # hands-off match that meets every enemy.
    [string]$ProjectPath = "",
    [string[]]$ProjectScene = @(),
    [int[]]$ProjectWaitSeconds = @(),
    [string[]]$ProjectEnv = @()
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$binDir = Join-Path $repoRoot "$BuildDir/$Config"
$player = Join-Path $binDir "PhasmaPlayer.exe"
$assetsDir = Join-Path $binDir "Assets"
$cacheSpv = Join-Path $binDir "ShaderCache/_spv"
$logFile = Join-Path $binDir "PhasmaEngine.log"
$prebaked = Join-Path $repoRoot "Phasma/Player/android/prebaked/ShaderCache/_spv"
$bakeManifest = Join-Path $repoRoot "Phasma/Player/android/prebaked/ShaderCache/bake-manifest.json"
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

function Get-BakeInputHashes {
    $inputs = @(
        Get-Item (Join-Path $repoRoot "tools/bake_android_shaders.ps1")
        Get-Item (Join-Path $repoRoot "Phasma/Core/Code/Base/Hash.h")
        Get-ChildItem -File -Recurse (Join-Path $repoRoot "Phasma/Core/Code/API") | Where-Object { $_.Extension -in ".cpp", ".h" }
        Get-ChildItem -File -Recurse (Join-Path $repoRoot "Phasma/Runtime/Code") | Where-Object { $_.Extension -in ".cpp", ".h" }
        Get-ChildItem -File -Recurse (Join-Path $repoRoot "Phasma/Runtime/RuntimeAssets/Shaders")
        Get-ChildItem -File -Recurse (Join-Path $repoRoot "Phasma/Runtime/RuntimeAssets/PassInfo")
        Get-ChildItem -File -Recurse (Join-Path $repoRoot "Phasma/Player/android/app/src/main/assets/Assets")
    )
    if ($ProjectPath) {
        # The game's own shaders and passes are compiled into the cache too (app/build.gradle.kts lists the same).
        $gameAssets = Join-Path (Resolve-Path $ProjectPath).Path "Assets"
        foreach ($dir in "Shaders", "PassInfo") {
            $gameDir = Join-Path $gameAssets $dir
            if (Test-Path $gameDir) { $inputs += Get-ChildItem -File -Recurse $gameDir }
        }
    }
    $files = $inputs | Sort-Object FullName -Unique

    $hashes = [ordered]@{}
    foreach ($file in $files) {
        $relativePath = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName).Replace("\", "/")
        $hashes[$relativePath] = (Get-FileHash -Algorithm SHA256 $file.FullName).Hash.ToLowerInvariant()
    }
    return $hashes
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
if ($ProjectPath) { Write-Host "[bake] project scenes  : $($ProjectScene -join ', ') in $ProjectPath" }
Write-Host "[bake] spirv-dis       : $spirvDis"
Write-Host "[bake] staging target  : $prebaked"

Write-Host "[bake] building desktop PhasmaPlayer bake host..."
& cmake --build (Join-Path $repoRoot $BuildDir) --config $Config --target PhasmaPlayer
if ($LASTEXITCODE -ne 0) {
    Fail "Desktop PhasmaPlayer build failed."
}
if (-not (Test-Path $player)) { Fail "PhasmaPlayer.exe not found at $player after the build." }
if (-not (Test-Path $assetsDir)) { Fail "Assets dir not found at $assetsDir (the bake host needs the shader sources + .pass assets)." }
if (-not (Get-Command $spirvDis -ErrorAction SilentlyContinue) -and -not (Test-Path $spirvDis)) {
    Fail "spirv-dis not found. Set `$env:VULKAN_SDK or pass -VulkanSdkBin."
}
$bakeInputHashes = Get-BakeInputHashes

# Android ships tracked local scene files under Phasma/Player/android/app/src/main/assets/Assets.
# If the requested scene exists there, copy it into the desktop bake host so global render settings
# (cascade count, shadow-map size, TAA/CAS defaults, etc.) match the APK instead of a local editor scene.
$androidSceneSource = Join-Path $repoRoot ("Phasma/Player/android/app/src/main/assets/" + $Scene)
$bakeHostScene = Join-Path $binDir $Scene
if (Test-Path $androidSceneSource) {
    New-Item -ItemType Directory -Force -Path (Split-Path $bakeHostScene -Parent) | Out-Null
    Copy-Item -Path $androidSceneSource -Destination $bakeHostScene -Force
    Write-Host "[bake] copied Android-local scene into bake host: $Scene"
}

# 1. Build the scene list. Runtime HUD toggles can enable passes after startup; with the Android
#    runtime compiler off, those lazy paths need to be pre-baked too. The second variant bakes the
#    non-TAA path so toggling TAA off has an Upsample shader in the cache.
$editorConfig = Join-Path $assetsDir "editor_config.json"
# The bake host runs on Vulkan and silent; the build folder's own settings come back after the runs.
$settingsPath = Join-Path $binDir "phasma_settings.json"
$settingsBackup = if (Test-Path $settingsPath) { Get-Content $settingsPath -Raw } else { $null }
function Write-BakeSettings([hashtable]$extra) {
    Set-Content -Path $settingsPath -Value ((@{ graphics_api = "vulkan"; audio_muted = $true } + $extra) | ConvertTo-Json -Compress) -Encoding utf8
}
function Invoke-BakeHost([string]$label, [int]$seconds) {
    Write-Host "[bake] launching PhasmaPlayer $label (PHASMA_SPIRV_TARGET=1.2, --api vulkan); waiting ${seconds}s for shader compile..."
    $proc = Start-Process $player -ArgumentList '--api', 'vulkan' -WorkingDirectory $binDir -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds $seconds
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "[bake] bake host still running after ${seconds}s (expected) -> stopped."
        return $true
    }
    Write-Host "[bake] WARNING: bake host exited early (code=0x$('{0:X8}' -f $proc.ExitCode)). Check $logFile." -ForegroundColor Yellow
    return $false
}
Write-BakeSettings @{}

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
$overlayRoot = $null
$overlayLinks = @()
$savedEnv = @{}
try {
    foreach ($bakeScene in $bakeScenes) {
        Set-Content -Path $editorConfig -Value (@{ last_scene = $bakeScene } | ConvertTo-Json -Compress) -Encoding utf8
        $null = Invoke-BakeHost "scene=$bakeScene" $WaitSeconds
    }

    # 3b. The game project's scenes, in a copy of the project laid out as the APK stages it.
    if ($ProjectPath) {
        # The host's C++ scripts must be the project's, or its scenes run without them and bake too little.
        $projectRoot = (Resolve-Path $ProjectPath).Path
        $nativeManifest = Join-Path $binDir "NativeScripts.json"
        if (-not (Test-Path $nativeManifest)) { Fail "No ${nativeManifest}: build the bake host with PE_PROJECT_NATIVE_DIR set to the project's C++ scripts." }
        $nativeSources = (Get-Content $nativeManifest -Raw | ConvertFrom-Json).sources
        if (-not $nativeSources -or -not [System.IO.Path]::GetFullPath($nativeSources).StartsWith($projectRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            Fail "The bake host's C++ scripts come from '$nativeSources', not from $projectRoot. Rebuild it with PE_PROJECT_NATIVE_DIR set to the project's scripts."
        }
        $projectAssets = Join-Path $projectRoot "Assets"
        $engineAssets = Join-Path $repoRoot "Phasma/Runtime/RuntimeAssets"
        $overlayRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("phasma-android-bake-" + [guid]::NewGuid().ToString("N"))
        $overlayAssets = Join-Path $overlayRoot "Assets"
        New-Item -ItemType Directory -Force -Path (Join-Path $overlayAssets "Save") | Out-Null
        foreach ($entry in Get-ChildItem $projectAssets) {
            if ($entry.Name -in "Shaders", "PassInfo", "Save") { continue }
            $target = Join-Path $overlayAssets $entry.Name
            if ($entry.PSIsContainer) {
                New-Item -ItemType Junction -Path $target -Target $entry.FullName | Out-Null
                $overlayLinks += $target
            }
            else { Copy-Item $entry.FullName $target }
        }
        foreach ($dir in "Shaders", "PassInfo") {
            Copy-Item -Recurse (Join-Path $engineAssets $dir) (Join-Path $overlayAssets $dir)
            $gameDir = Join-Path $projectAssets $dir
            if (Test-Path $gameDir) {
                foreach ($file in Get-ChildItem -Recurse -File $gameDir) {
                    $target = Join-Path (Join-Path $overlayAssets $dir) ([System.IO.Path]::GetRelativePath($gameDir, $file.FullName))
                    if (-not (Test-Path $target)) {
                        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
                        Copy-Item $file.FullName $target
                    }
                }
            }
        }
        $manifest = (Join-Path $overlayRoot "phasma_project.json").Replace("\", "/")
        Write-TextUtf8NoBom $manifest (@{ version = 1; name = "Android shader bake"; assets = $overlayAssets.Replace("\", "/") } | ConvertTo-Json -Compress)
        foreach ($pair in $ProjectEnv) {
            $name, $value = $pair.Split("=", 2)
            $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name)
            [Environment]::SetEnvironmentVariable($name, $value)
        }
        for ($i = 0; $i -lt $ProjectScene.Count; $i++) {
            $scenePath = Join-Path $overlayRoot $ProjectScene[$i]
            if (-not (Test-Path $scenePath)) { Fail "Project scene not found at $scenePath" }
            $scenePath = (Resolve-Path $scenePath).Path.Replace("\", "/")
            Write-BakeSettings @{ project_path = $overlayRoot.Replace("\", "/"); project_manifest = $manifest; startup_scene = $scenePath }
            if (Test-Path $logFile) { Clear-Content $logFile }
            $sceneName = Split-Path $scenePath -Leaf
            $ranFull = Invoke-BakeHost "project scene=$($ProjectScene[$i])" $(if ($i -lt $ProjectWaitSeconds.Count) { $ProjectWaitSeconds[$i] } else { $WaitSeconds })
            if (-not $ranFull) { Fail "The bake host exited early on $sceneName; its later shaders are not baked. Check $logFile." }
            $log = if (Test-Path $logFile) { @(Get-Content $logFile) } else { @() }
            if (-not ($log | Where-Object { $_.Contains("Scene loaded from:") -and $_.Contains($sceneName) })) {
                Fail "The bake host never loaded $sceneName; its shaders are not baked. Check $logFile."
            }
            if (-not ($log | Where-Object { $_ -match '\[CppScript\] module (re)?loaded' })) {
                Fail "The C++ scripts never loaded for $sceneName; its scripted content is not baked. Check $logFile."
            }
            $scriptError = $log | Where-Object { $_ -match '\[ERROR\] \[CppScript\]' } | Select-Object -First 1
            if ($scriptError) { Fail "A C++ script failed in $sceneName ($scriptError); its later shaders are not baked." }
        }
    }
}
finally {
    foreach ($name in $savedEnv.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnv[$name]) }
    $unlinked = $true
    foreach ($link in $overlayLinks) {
        try { [System.IO.Directory]::Delete($link) }  # the junction only, never the game folder it points at
        catch { $unlinked = $false; Write-Host "[bake] WARNING: could not remove junction $link; leaving $overlayRoot in place." -ForegroundColor Yellow }
    }
    if ($overlayRoot -and $unlinked -and (Test-Path $overlayRoot)) { Remove-Item -Recurse -Force $overlayRoot }
    if ($null -ne $settingsBackup) { Set-Content -Path $settingsPath -Value $settingsBackup -NoNewline -Encoding utf8 }
    else { Remove-Item $settingsPath -ErrorAction SilentlyContinue }
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
$currentBakeInputHashes = Get-BakeInputHashes
if (($bakeInputHashes | ConvertTo-Json -Compress) -cne ($currentBakeInputHashes | ConvertTo-Json -Compress)) {
    Fail "Bake inputs changed while the cache was being generated. Run the bake again."
}
Write-TextUtf8NoBom $bakeManifest ([ordered]@{ version = 1; files = $bakeInputHashes } | ConvertTo-Json -Depth 4)
Write-Host "[bake] manifest    : $bakeManifest"
Write-Host "[bake] softened-feature capabilities (Int64/Int16/Float16/StorageBufferNonUniform): NONE -> warn-on-Android gates are SAFE." -ForegroundColor Green
Write-Host "[bake] Next: cd Phasma/Player/android ; .\gradlew.bat :app:assembleDebug"
