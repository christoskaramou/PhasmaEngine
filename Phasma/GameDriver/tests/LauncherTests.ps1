$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$launcher = Join-Path $repo 'Phasma/GameDriver/run-tetris.ps1'

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($launcher, [ref]$tokens, [ref]$parseErrors)
Assert (-not $parseErrors) 'Launcher syntax failed.'

$measure = $ast.Find({
    param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Measure-GameTrace'
}, $true)
Assert ($null -ne $measure) 'Missing production trace summary function.'

$launcherParameters = $ast.ParamBlock.Parameters.Name.VariablePath.UserPath
Assert ($launcherParameters -contains 'UseVault') 'Launcher lost its -UseVault switch.'
Assert ($ast.Extent.Text.Contains("'--key-source', 'vault'")) 'Launcher does not pass the vault key source.'
Assert ($launcherParameters -contains 'PaceMs') 'Launcher lost its -PaceMs parameter.'
Assert ($ast.Extent.Text.Contains("'--pace-ms', `$PaceMs")) 'Launcher does not pass the pace to the driver.'

. ([scriptblock]::Create($measure.Extent.Text))

$scratch = Join-Path $repo 'build-game-driver/launcher-tests'
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$trace = Join-Path $scratch 'fixture.jsonl'

@(
    @{ event = 'start'; time_ms = 1000; policy = 'JEV' },
    @{ event = 'decision'; duration_ms = 100 },
    @{ event = 'decision'; duration_ms = 200 },
    @{ event = 'decision'; duration_ms = 300 },
    @{ event = 'finish'; time_ms = 3500; decisions = 3; score = 321; terminal = $false;
       api_calls = 3; input_tokens = 900; output_tokens = 3 }
) | ForEach-Object { $_ | ConvertTo-Json -Compress } | Set-Content -LiteralPath $trace

$summary = Measure-GameTrace $trace
Assert ($summary.policy -eq 'JEV' -and $summary.placements -eq 3) 'Summary lost policy/decision identity.'
Assert ($summary.elapsed_seconds -eq 2.5 -and $summary.placements_per_second -eq 1.2) 'Incorrect run speed.'
Assert ($summary.mean_decision_ms -eq 200 -and $summary.p95_decision_ms -eq 300) 'Incorrect request latency.'
Assert ($summary.first_decision_ms -eq 100 -and $summary.input_tokens -eq 900) 'Incorrect cold latency/token count.'
Assert ($summary.score -eq 321 -and -not $summary.terminal) 'Incorrect final outcome.'

'{"event":"start","time_ms":1,"policy":"LOCAL"}' | Set-Content -LiteralPath $trace
$rejected = $false
try {
    Measure-GameTrace $trace | Out-Null
} catch {
    $rejected = $_.Exception.Message -like 'Run did not finish*'
}
Assert $rejected 'Incomplete run was reported as a speed result.'

$original = @{}
foreach ($name in @('JEV_API_KEY', 'TYPESAFE_API_KEY')) {
    $original[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    foreach ($name in $original.Keys) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    foreach ($badKey in @('', 'private-fixture key', ('x' * 4097))) {
        [Environment]::SetEnvironmentVariable('JEV_API_KEY', $badKey, 'Process')
        $failure = $null
        try {
            & $launcher -Policy jev | Out-Null
        } catch {
            $failure = $_.Exception.Message
        }

        Assert ($failure -match '^Set JEV_API_KEY|^Jev API key must contain') `
            'Missing/invalid key did not fail before launch.'
        if ($badKey) {
            Assert (-not $failure.Contains($badKey)) 'Key appeared in an error message.'
        }
    }

    function Invoke-Driver([string]$Arguments) {
        $launch = [Diagnostics.ProcessStartInfo]::new()
        $launch.FileName = Join-Path $repo 'build-game-driver/Release/PhasmaGameDriver.exe'
        $launch.Arguments = $Arguments
        $launch.UseShellExecute = $false
        $launch.CreateNoWindow = $true
        $launch.RedirectStandardOutput = $true
        $launch.RedirectStandardError = $true
        $process = [Diagnostics.Process]::Start($launch)
        try {
            if (-not $process.WaitForExit(5000)) {
                $process.Kill()
                throw 'Native credential preflight did not finish promptly.'
            }
            [pscustomobject]@{
                ExitCode = $process.ExitCode
                Output = $process.StandardOutput.ReadToEnd() + $process.StandardError.ReadToEnd()
            }
        } finally {
            $process.Dispose()
        }
    }

    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try {
        $port = $listener.LocalEndpoint.Port
        foreach ($badKey in @('', 'private-fixture key', ('x' * 4097))) {
            [Environment]::SetEnvironmentVariable('JEV_API_KEY', $badKey, 'Process')
            $result = Invoke-Driver "--policy jev --steps 1 --seed 42 --url http://127.0.0.1:$port/mcp"
            Assert ($result.ExitCode -ne 0 -and $result.Output -match 'key') 'Native missing/invalid key was accepted.'
            Assert (-not $listener.Pending()) 'Driver connected to the game before validating credentials.'
            if ($badKey) {
                Assert (-not $result.Output.Contains($badKey)) 'Native error revealed a key.'
            }
        }

        $result = Invoke-Driver "--policy jev --steps 1 --seed 42 --key-source bogus --url http://127.0.0.1:$port/mcp"
        Assert ($result.ExitCode -ne 0 -and $result.Output -match 'key-source') 'Unknown key source was accepted.'
        Assert (-not $listener.Pending()) 'Driver connected to the game before rejecting the key source.'

        $result = Invoke-Driver "--policy jev --steps 1 --seed 42 --pace-ms 99999 --url http://127.0.0.1:$port/mcp"
        Assert ($result.ExitCode -ne 0 -and $result.Output -match 'numeric') 'Out-of-range pace was accepted.'
        Assert (-not $listener.Pending()) 'Driver connected to the game before rejecting the pace.'

        # Key commands answer with one fixed status line and never print key material. Only the read-only
        # status runs here: saving and removing would touch the real saved key.
        $result = Invoke-Driver '--key-status'
        $status = $result.Output.Trim()
        Assert ($result.ExitCode -eq 0 -and $status -match '^KEY (saved|missing)$') "Key status answered: $status"
        $mixedCommands = @('--key-status --policy local', '--remove-key now', '--save-key-from-clipboard --steps 1')
        foreach ($mixed in $mixedCommands) {
            $result = Invoke-Driver $mixed
            $refused = $result.ExitCode -ne 0 -and $result.Output -notmatch '^KEY'
            Assert $refused "Key command with extra arguments ran: $mixed"
        }
        Assert (-not $listener.Pending()) 'A key command connected to the game.'
    } finally {
        $listener.Stop()
    }

    $launchBlock = $ast.Find({
        param($node)
        $node -is [Management.Automation.Language.TryStatementAst] -and
            $node.Body.Extent.Text.Contains('$running = Start-Process -FilePath $player')
    }, $true)
    Assert ($null -ne $launchBlock) 'Missing isolated Player launch block.'
    function Start-Process {
        Assert (-not $env:JEV_API_KEY -and -not $env:TYPESAFE_API_KEY) 'Player inherited a model credential.'
        if ($script:failLaunch) {
            throw 'fixture launch failure'
        }

        [pscustomobject]@{ Id = 1234 }
    }
    foreach ($fail in @($false, $true)) {
        $script:failLaunch = $fail
        $driverKeys = @{ JEV_API_KEY = 'fixture-jev'; TYPESAFE_API_KEY = 'fixture-typesafe' }
        foreach ($name in $driverKeys.Keys) {
            [Environment]::SetEnvironmentVariable($name, $driverKeys[$name], 'Process')
        }
        $player = 'fixture-player.exe'
        $runtime = $scratch
        try {
            . ([scriptblock]::Create($launchBlock.Extent.Text))
        } catch {
            Assert ($fail -and $_.Exception.Message -eq 'fixture launch failure') 'Unexpected Player launch failure.'
        }
        Assert ($env:JEV_API_KEY -eq 'fixture-jev') 'Jev key was not restored to the driver after launch.'
        Assert ($env:TYPESAFE_API_KEY -eq 'fixture-typesafe') 'Alternate key was not restored after launch.'
        Assert ($driverKeys.Count -eq 0) 'Temporary key dictionary was retained.'
    }
} finally {
    foreach ($name in $original.Keys) {
        [Environment]::SetEnvironmentVariable($name, $original[$name], 'Process')
    }
    $original.Clear()
}

Write-Output 'PASS: explicit key preflight before MCP, isolated Player credentials and completed-run speed summary.'
