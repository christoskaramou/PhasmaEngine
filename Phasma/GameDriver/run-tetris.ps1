param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('local', 'jev')]
    [string]$Policy,
    [ValidateRange(1, 10000)]
    [int]$Steps = 20,
    [ValidateRange(0, 2147483647)]
    [int]$Seed = 42,
    [ValidateRange(1024, 65535)]
    [int]$Port = 8876,
    [ValidateRange(0, 10000)]
    [int]$PaceMs = 0,
    [switch]$ShowWindow,
    [switch]$UseVault
)

$ErrorActionPreference = 'Stop'

function Measure-GameTrace([string]$Path) {
    $latencies = [Collections.Generic.List[double]]::new()
    $start = $null
    $finish = $null
    Get-Content -LiteralPath $Path | ForEach-Object {
        $row = $_ | ConvertFrom-Json
        switch ($row.event) {
            'start' { $start = $row }
            'decision' { $latencies.Add([double]$row.duration_ms) }
            'finish' { $finish = $row }
        }
    }
    if (-not $start -or -not $finish) {
        throw 'Run did not finish; its partial trace is available, but no speed result will be reported.'
    }

    $sorted = @($latencies | Sort-Object)
    $seconds = [Math]::Max(0.0, ($finish.time_ms - $start.time_ms) / 1000.0)
    $mean = if ($sorted.Count) { ($latencies | Measure-Object -Average).Average } else { 0 }
    $p95 = if ($sorted.Count) { $sorted[[Math]::Ceiling($sorted.Count * 0.95) - 1] } else { 0 }
    $rate = if ($seconds -gt 0) { $finish.decisions / $seconds } else { 0 }

    [pscustomobject][ordered]@{
        policy = $start.policy
        placements = $finish.decisions
        elapsed_seconds = [Math]::Round($seconds, 3)
        placements_per_second = [Math]::Round($rate, 3)
        mean_decision_ms = [Math]::Round($mean, 1)
        p95_decision_ms = $p95
        first_decision_ms = if ($latencies.Count) { $latencies[0] } else { 0 }
        score = $finish.score
        terminal = $finish.terminal
        api_calls = $finish.api_calls
        input_tokens = $finish.input_tokens
        output_tokens = $finish.output_tokens
        trace = $Path
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$build = Join-Path $repoRoot 'build-game-driver'
$source = Join-Path $repoRoot 'build-jev-player/Release'
$runtime = Join-Path $build 'tetris'
$assets = Join-Path $runtime 'Assets'
$player = Join-Path $runtime 'PhasmaPlayer.exe'
$driver = Join-Path $build 'Release/PhasmaGameDriver.exe'
$instancePath = Join-Path $runtime 'instance.json'

if ($Policy -eq 'jev' -and -not $UseVault) {
    $configuredKey = if ($env:JEV_API_KEY) { $env:JEV_API_KEY } else { $env:TYPESAFE_API_KEY }
    if (-not $configuredKey) {
        throw 'Set JEV_API_KEY or TYPESAFE_API_KEY in this terminal before selecting Jev. Credential stores are not read.'
    }
    if ($configuredKey.Length -gt 4096 -or $configuredKey -match '[^\x21-\x7e]') {
        throw 'Jev API key must contain 1..4096 printable ASCII characters without spaces.'
    }
    $configuredKey = $null
}

foreach ($required in @($driver, (Join-Path $source 'PhasmaPlayer.exe'), (Join-Path $source 'PhasmaCore.dll'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing $required. Run build.ps1 and build-player.ps1 first."
    }
}

$running = $null
if (Test-Path -LiteralPath $instancePath) {
    $previous = Get-Content -LiteralPath $instancePath -Raw | ConvertFrom-Json
    $candidate = Get-Process -Id $previous.pid -ErrorAction SilentlyContinue
    if ($candidate -and $candidate.Path -eq $player) {
        if ($previous.port -ne $Port) {
            throw "This launcher already has a Player on port $($previous.port). Close that Player before changing ports."
        }
        $running = $candidate
    }
}

if (-not $running) {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $Port)
    try {
        $listener.Start()
    } catch {
        throw "Port $Port is occupied. Choose a different -Port."
    } finally {
        $listener.Stop()
    }

    New-Item -ItemType Directory -Path $assets -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'PhasmaPlayer.exe') -Destination $runtime -Force
    Get-ChildItem -LiteralPath $source -Filter '*.dll' -File |
        Copy-Item -Destination $runtime -Force

    foreach ($folder in @('RuntimeAssets', 'MaterialX')) {
        $sourceFolder = Join-Path $source $folder
        if (Test-Path -LiteralPath $sourceFolder) {
            Copy-Item -LiteralPath $sourceFolder -Destination $runtime -Recurse -Force
        }
    }

    foreach ($folder in @('Audio', 'Scenes', 'Scripts', 'Textures')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot "Games/PrismTetris/Assets/$folder") `
            -Destination $assets -Recurse -Force
    }

    $agent = Join-Path $assets 'Agent'
    New-Item -ItemType Directory -Path $agent -Force | Out-Null
    @{ mcp = $true; mcp_port = $Port } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $agent 'agent_config.json') -Encoding utf8
    @{
        project_path = $assets
        project_manifest = ''
        startup_scene = 'Scenes/prism_tetris.pescene'
        graphics_api = 'vulkan'
        live_profiler = $false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runtime 'phasma_settings.json') -Encoding utf8

    $windowStyle = if ($ShowWindow) { 'Normal' } else { 'Hidden' }
    $driverKeys = @{}
    foreach ($name in @('JEV_API_KEY', 'TYPESAFE_API_KEY')) {
        $driverKeys[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    try {
        foreach ($name in $driverKeys.Keys) {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
        $running = Start-Process -FilePath $player -ArgumentList '--api', 'vulkan', '--display', '1' `
            -WorkingDirectory $runtime -WindowStyle $windowStyle -PassThru
    } finally {
        foreach ($name in $driverKeys.Keys) {
            [Environment]::SetEnvironmentVariable($name, $driverKeys[$name], 'Process')
        }
        $driverKeys.Clear()
    }
    @{ pid = $running.Id; executable = $player; assets = $assets; port = $Port } | ConvertTo-Json |
        Set-Content -LiteralPath $instancePath -Encoding utf8
}

$ready = $false
$deadline = [DateTime]::UtcNow.AddSeconds(90)
while ([DateTime]::UtcNow -lt $deadline) {
    $running.Refresh()
    if ($running.HasExited) {
        throw "Player exited ($($running.ExitCode)). See $runtime/PhasmaEngine.log"
    }

    $client = [Net.Sockets.TcpClient]::new()
    try {
        $client.Connect([Net.IPAddress]::Loopback, $Port)
        $ready = $true
        break
    } catch {
        Start-Sleep -Milliseconds 250
    } finally {
        $client.Dispose()
    }
}
if (-not $ready) {
    throw "Player did not open port $Port. See $runtime/PhasmaEngine.log"
}

$trace = Join-Path $runtime ('play-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '.jsonl')
Write-Host "Player PID $($running.Id), policy $Policy, trace $trace"
$keyArguments = if ($UseVault) { @('--key-source', 'vault') } else { @() }
$paceArguments = @('--pace-ms', $PaceMs)
& $driver --url "http://127.0.0.1:$Port/mcp" --policy $Policy --steps $Steps --seed $Seed --trace $trace `
    @keyArguments @paceArguments
if ($LASTEXITCODE -ne 0) {
    throw "Game driver exited ($LASTEXITCODE)."
}

$summary = Measure-GameTrace $trace
$summaryPath = [IO.Path]::ChangeExtension($trace, '.summary.json')
$summary | ConvertTo-Json | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Host ("{0}: {1} placements in {2}s ({3}/s), decision mean {4}ms, p95 {5}ms." -f `
    $summary.policy, $summary.placements, $summary.elapsed_seconds, $summary.placements_per_second, `
    $summary.mean_decision_ms, $summary.p95_decision_ms)
Write-Host "Summary: $summaryPath"
