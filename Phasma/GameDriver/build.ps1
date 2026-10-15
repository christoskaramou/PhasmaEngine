$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$build = Join-Path $repoRoot 'build-game-driver'

& cmake -S $PSScriptRoot -B $build -A x64
if ($LASTEXITCODE -ne 0) {
    throw 'Game driver configuration failed.'
}

& cmake --build $build --config Release --parallel 1
if ($LASTEXITCODE -ne 0) {
    throw 'Game driver build failed.'
}

& ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw 'Game driver tests failed.'
}
