<#
.SYNOPSIS
    Cook a source model (glTF/FBX/OBJ) into the engine's portable ".pemesh" format and assemble a
    self-contained cooked-model folder under Assets/Models/<name>/.

.DESCRIPTION
    The player (desktop and Android) loads only ".pemesh" — Assimp is editor-only and is used here, on
    the desktop, purely to import the source model and cook it. This mirrors tools/bake_android_shaders.ps1:
    a desktop host produces an asset the Assimp-less, runtime-compiler-less device consumes.

    Steps:
      1. Build the desktop PhasmaPlayer (the cook host) if requested.
      2. Run PhasmaPlayer in cook mode (PHASMA_COOK_MODEL / PHASMA_COOK_OUT) to import <source> via
         Assimp and write <name>.pemesh — vertex/index/aabb streams in GPU-upload-ready layout.
      3. Copy the source model's sibling textures (png/jpg/jpeg) next to the .pemesh so the cooked
         folder is self-contained.

    Output (default): prebaked/Models/<name>/<name>.pemesh + textures (gitignored APK staging).
    Cooked assets are build artifacts and never live under PhasmaEditor/Assets (the source tree).

.PARAMETER Source
    Path to the source model file (e.g. PhasmaEditor/Assets/Objects/.../Sponza.gltf).

.PARAMETER Name
    Cooked model name (folder + .pemesh stem). Defaults to the source file stem.

.PARAMETER Build
    Rebuild the desktop PhasmaPlayer cook host first.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$Name = "",
    [string]$BuildDir = "build-ninja-full",
    [string]$Config = "Release",
    # Where cooked models land. Cooked assets are BUILD ARTIFACTS, not source, so they live in a
    # gitignored staging/config folder — never under PhasmaEditor/Assets. Default is the Android APK
    # staging (gradle bundles prebaked/Models/<name> into Assets/Models/<name>); pass e.g.
    # build-ninja-full/Release/Assets/Models to cook for the desktop player's config folder.
    [string]$OutModelsDir = "PhasmaPlayer/android/prebaked/Models",
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (-not (Test-Path $Source)) { Write-Host "[cook] ERROR: source not found: $Source" -ForegroundColor Red; exit 1 }
$srcItem = Get-Item $Source
if ([string]::IsNullOrWhiteSpace($Name)) { $Name = $srcItem.BaseName }

$binDir = Join-Path $repoRoot "$BuildDir/$Config"
$player = Join-Path $binDir "PhasmaPlayer.exe"
$modelsDir = Join-Path $repoRoot (Join-Path $OutModelsDir $Name)
$outPemesh = Join-Path $modelsDir "$Name.pemesh"

Write-Host "[cook] source : $($srcItem.FullName)"
Write-Host "[cook] name   : $Name"
Write-Host "[cook] output : $outPemesh"

if ($Build) {
    Write-Host "[cook] building cook host (PhasmaPlayer)..."
    $vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    cmd /c "`"$vcvars`" >nul 2>&1 && cmake --build $BuildDir --config $Config --target PhasmaPlayer"
    if ($LASTEXITCODE -ne 0) { Write-Host "[cook] ERROR: build failed" -ForegroundColor Red; exit 1 }
}
if (-not (Test-Path $player)) { Write-Host "[cook] ERROR: PhasmaPlayer.exe not found (use -Build): $player" -ForegroundColor Red; exit 1 }

New-Item -ItemType Directory -Force -Path $modelsDir | Out-Null

# 1. Cook geometry via the desktop player's cook mode.
$env:PHASMA_COOK_MODEL = $srcItem.FullName
$env:PHASMA_COOK_OUT = $outPemesh
Write-Host "[cook] running cook host..."
$p = Start-Process -FilePath $player -ArgumentList '--api', 'vulkan' -WorkingDirectory $binDir -PassThru -NoNewWindow
$p.WaitForExit(180000) | Out-Null
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Host "[cook] ERROR: cook host timed out" -ForegroundColor Red; exit 1 }
Remove-Item Env:\PHASMA_COOK_MODEL, Env:\PHASMA_COOK_OUT -ErrorAction SilentlyContinue
if ($p.ExitCode -ne 0 -or -not (Test-Path $outPemesh)) {
    Write-Host "[cook] ERROR: cook failed (exit $($p.ExitCode)). See $binDir/PhasmaEngine.log" -ForegroundColor Red
    exit 1
}
$pemeshSize = [math]::Round((Get-Item $outPemesh).Length / 1MB, 2)
Write-Host "[cook] cooked $outPemesh ($pemeshSize MB)"

# 2. Copy the source model's sibling textures so the cooked folder is self-contained.
$srcDir = $srcItem.DirectoryName
$textures = Get-ChildItem -Path $srcDir -File | Where-Object { $_.Extension -match '^\.(png|jpg|jpeg)$' }
$copied = 0
foreach ($t in $textures) {
    Copy-Item $t.FullName (Join-Path $modelsDir $t.Name) -Force
    $copied++
}
$texMb = [math]::Round((($textures | Measure-Object Length -Sum).Sum) / 1MB, 2)
Write-Host "[cook] copied $copied texture(s) ($texMb MB) -> $modelsDir"
Write-Host "[cook] done: Assets/Models/$Name/ is self-contained ($Name.pemesh + textures)." -ForegroundColor Green
