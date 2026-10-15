$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$driver = Join-Path $repo 'build-game-driver/Release/PhasmaGameDriver.exe'

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Observation([int]$StateId) {
    @{
        protocol_version = 1
        game_id = 'fixture'
        session_id = 'fixture:1'
        state_id = $StateId
        mode = 'playing'
        terminal = $false
        busy = $false
        score = 0
        observation = @{ board = @() }
        actions = @(@{ id = 'drop'; description = 'Drop the piece'; score = 1 })
    }
}

function Read-Request([Net.Sockets.NetworkStream]$Stream) {
    $buffer = [byte[]]::new(65536)
    $data = [IO.MemoryStream]::new()
    $headerEnd = -1
    while ($headerEnd -lt 0) {
        $count = $Stream.Read($buffer, 0, $buffer.Length)
        if ($count -le 0) {
            return $null
        }

        $data.Write($buffer, 0, $count)
        $text = [Text.Encoding]::ASCII.GetString($data.ToArray())
        $headerEnd = $text.IndexOf("`r`n`r`n")
    }

    $length = [regex]::Match($text.Substring(0, $headerEnd), 'Content-Length:\s*(\d+)', 'IgnoreCase').Groups[1].Value
    $length = if ($length) { [int]$length } else { 0 }
    while ($data.Length -lt $headerEnd + 4 + $length) {
        $count = $Stream.Read($buffer, 0, $buffer.Length)
        if ($count -le 0) {
            break
        }

        $data.Write($buffer, 0, $count)
    }

    [Text.Encoding]::UTF8.GetString($data.ToArray(), $headerEnd + 4, $length)
}

function Write-Response([Net.Sockets.NetworkStream]$Stream, [string]$Body) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Body)
    $head = "HTTP/1.1 200 OK`r`nContent-Type: application/json`r`n" +
        "Content-Length: $($bytes.Length)`r`nConnection: close`r`n`r`n"
    $headBytes = [Text.Encoding]::ASCII.GetBytes($head)
    $Stream.Write($headBytes, 0, $headBytes.Length)
    $Stream.Write($bytes, 0, $bytes.Length)
    $Stream.Flush()
}

function Serve-Request([Net.Sockets.TcpClient]$Client, [string]$Tracking, [scriptblock]$Act, [hashtable]$Game) {
    $stream = $Client.GetStream()
    $body = Read-Request $stream
    if (-not $body) {
        return
    }

    # A timed game moves on by itself (the next piece falls) once its scheduled change comes due.
    if ($null -ne $Game.changeAt -and $Game.clock.ElapsedMilliseconds -ge $Game.changeAt) {
        $Game.id++
        $Game.changeAt = $null
    }

    $request = $body | ConvertFrom-Json
    if ($request.method -eq 'notifications/initialized') {
        Write-Response $stream ''
        return
    }

    if ($request.method -eq 'initialize') {
        $result = @{
            protocolVersion = '2025-06-18'
            capabilities = @{}
            serverInfo = @{ name = 'fixture'; version = '1' }
        }
    } else {
        $payload = switch ($request.params.name) {
            'game_describe' {
                @{
                    protocol_version = 1
                    game_id = 'fixture'
                    description = 'Fixture game'
                    reset_supported = $true
                    state_tracking = $Tracking
                }
            }
            'game_reset' {
                $Game.id = 1
                Observation 1
            }
            'game_observe' {
                Observation $Game.id
            }
            'game_act' {
                $Game.acts++
                & $Act $Game $request.params.arguments
            }
        }
        $result = @{ content = @(@{ type = 'text'; text = ($payload | ConvertTo-Json -Depth 10 -Compress) }) }
    }

    $response = @{ jsonrpc = '2.0'; id = $request.id; result = $result }
    Write-Response $stream ($response | ConvertTo-Json -Depth 10 -Compress)
}

# One scripted game: every MCP request gets its own connection, and $Act answers each game_act.
function Invoke-Scenario([string]$Tracking, [scriptblock]$Act, [string]$Arguments, [int]$TimeoutMs = 8000) {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $game = @{ id = 1; acts = 0; changeAt = $null; clock = [Diagnostics.Stopwatch]::StartNew() }
    $launch = [Diagnostics.ProcessStartInfo]::new()
    $launch.FileName = $driver
    $launch.Arguments = "--url http://127.0.0.1:$($listener.LocalEndpoint.Port)/mcp $Arguments"
    $launch.UseShellExecute = $false
    $launch.CreateNoWindow = $true
    $launch.RedirectStandardOutput = $true
    $launch.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($launch)
    $stderr = $process.StandardError.ReadToEndAsync()
    $watch = [Diagnostics.Stopwatch]::StartNew()

    # Stdout is read line by line as it arrives, so tests can see when a parent game would get each line.
    $lines = [Collections.Generic.List[object]]::new()
    $pending = $process.StandardOutput.ReadLineAsync()
    $collect = {
        while ($pending -and $pending.IsCompleted) {
            if ($null -eq $pending.Result) {
                $pending = $null
                break
            }

            $lines.Add([pscustomobject]@{ Text = $pending.Result; Ms = $watch.ElapsedMilliseconds })
            $pending = $process.StandardOutput.ReadLineAsync()
        }
    }
    try {
        while (-not $process.HasExited -and $watch.ElapsedMilliseconds -lt $TimeoutMs) {
            . $collect
            if (-not $listener.Pending()) {
                Start-Sleep -Milliseconds 2
                continue
            }

            $client = $listener.AcceptTcpClient()
            try {
                Serve-Request $client $Tracking $Act $game
            } finally {
                $client.Dispose()
            }
        }

        $timedOut = -not $process.HasExited
        if ($timedOut) {
            $process.Kill()
        }
        $process.WaitForExit()
        while ($pending) {
            [void]$pending.Wait(2000)
            . $collect
        }
        [pscustomobject]@{
            TimedOut = $timedOut
            ExitCode = $process.ExitCode
            Output = (($lines | ForEach-Object Text) -join "`n") + "`n" + $stderr.Result
            Lines = $lines
            ElapsedMs = $watch.ElapsedMilliseconds
            Acts = $game.acts
        }
    } finally {
        $listener.Stop()
        $process.Dispose()
    }
}

$accept = {
    param($game)
    $game.id++
    Observation $game.id
}
$stale = {
    param($game)
    $game.id++
    @{ ok = $false; error = @{ code = 'stale_state'; message = 'the piece locked' } }
}
$illegal = {
    param($game)
    @{ ok = $false; error = @{ code = 'illegal_action'; message = 'not offered' } }
}
$timed = {
    param($game, $arguments)
    if ($arguments.state_id -ne $game.id) {
        return @{ ok = $false; error = @{ code = 'stale_state'; message = 'the next piece moved on' } }
    }

    $game.id++
    $game.changeAt = $game.clock.ElapsedMilliseconds + 300
    Observation $game.id
}

# --steps caps paid decisions, re-decides included.
$result = Invoke-Scenario 'game_epoch' $stale '--policy local --steps 3 --seed 42'
Assert (-not $result.TimedOut -and $result.ExitCode -eq 0) "Re-decides ran past --steps: $($result.Acts) acts"
$counted = $result.Output -match 'Finished: decisions=0 redecides=3'
Assert ($result.Acts -eq 3 -and $counted) 'Re-decides were not counted.'

# Every paid call reports its tokens, so a game can show the running cost.
function Lines([string]$Output, [string]$Pattern) {
    @($Output -split "`n" | Where-Object { $_ -match $Pattern })
}
$redecides = Lines $result.Output ' re-decide \d+ '
$untagged = @($redecides | Where-Object { $_ -notmatch 'input_tokens=0\s*$' })
Assert ($redecides.Count -eq 3 -and $untagged.Count -eq 0) 'Re-decide lines carry no token count.'

# A paced real-time run waits, then decides on what the game shows now: the next piece moved on meanwhile.
$result = Invoke-Scenario 'game_epoch' $timed '--policy local --steps 3 --seed 42 --pace-ms 400'
$fresh = $result.Output -match 'Finished: decisions=3 redecides=0'
Assert ($result.ExitCode -eq 0 -and $fresh) "A paced real-time run acted on an aged observation: $($result.Output)"
Assert ($result.ElapsedMs -ge 1200) "A real-time run ignored --pace-ms ($($result.ElapsedMs) ms)."
$steps = Lines $result.Output ' step \d+ '
$untagged = @($steps | Where-Object { $_ -notmatch 'input_tokens=0\s*$' })
Assert ($steps.Count -eq 3 -and $untagged.Count -eq 0) 'Step lines carry no token count.'

# A game that starts the driver reads these lines live: each must arrive when it is printed, not at exit.
$first = @($result.Lines | Where-Object { $_.Text -match ' step 1 ' }) | Select-Object -First 1
$total = $result.ElapsedMs
Assert ($first -and $first.Ms -lt 800) "Step 1 reached the parent after $($first.Ms) ms of a $total ms run."

# Turn-based games still honour the pace and still end on a stale act.
$result = Invoke-Scenario 'public_state' $accept '--policy local --steps 3 --seed 42 --pace-ms 400'
Assert ($result.ExitCode -eq 0 -and $result.ElapsedMs -ge 1200) `
    "A turn-based run ignored --pace-ms ($($result.ElapsedMs) ms)."
$result = Invoke-Scenario 'public_state' $stale '--policy local --steps 3 --seed 42'
$ended = $result.ExitCode -ne 0 -and $result.Acts -eq 1
Assert ($ended -and $result.Output -match 'stale_state') 'Turn-based stale act did not end the run.'

# Real rejections still end a real-time run, with the game's error.
$result = Invoke-Scenario 'game_epoch' $illegal '--policy local --steps 3 --seed 42'
$rejected = $result.Output -match 'Game rejected the action: .*illegal_action'
Assert ($result.ExitCode -ne 0 -and $rejected) 'Illegal act did not end the run.'

Write-Host 'PASS: real-time step cap, pacing, stale and rejected acts'
