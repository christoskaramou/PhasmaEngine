[CmdletBinding()]
param(
    [string]$Exe = "",
    [string[]]$PlayerArgs = @(),
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot

if (-not $Exe) {
    $Exe = Get-ChildItem "$repoRoot\build*\Release\PhasmaPlayer.exe" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Exe -or -not (Test-Path -LiteralPath $Exe)) {
    throw "PhasmaPlayer.exe not found; pass -Exe with its full path."
}

$exePath = (Resolve-Path -LiteralPath $Exe).Path
$exeDir = Split-Path $exePath
if (-not $Output) {
    $Output = Join-Path $exeDir "PhasmaPlayer.exit.jsonl"
}
$Output = [System.IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force (Split-Path $Output) | Out-Null

$dumpDir = Join-Path $env:LOCALAPPDATA "CrashDumps"
$dumpKey = "HKCU:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\PhasmaPlayer.exe"
New-Item -ItemType Directory -Force $dumpDir | Out-Null
New-Item -Path $dumpKey -Force | Out-Null
New-ItemProperty -Path $dumpKey -Name DumpFolder -Value $dumpDir -PropertyType ExpandString -Force | Out-Null
New-ItemProperty -Path $dumpKey -Name DumpCount -Value 10 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $dumpKey -Name DumpType -Value 2 -PropertyType DWord -Force | Out-Null

$startArgs = @{
    FilePath = $exePath
    WorkingDirectory = $exeDir
    PassThru = $true
}
if ($PlayerArgs.Count) {
    $startArgs.ArgumentList = $PlayerArgs
}

$started = Get-Date
$proc = Start-Process @startArgs
Write-Host "Monitoring $exePath (pid=$($proc.Id)); exit record: $Output"
$proc.WaitForExit()
$ended = Get-Date
$exitCode = [int]$proc.ExitCode
$exitHex = "{0:X8}" -f [BitConverter]::ToUInt32([BitConverter]::GetBytes($exitCode), 0)
$dumps = @(Get-ChildItem -LiteralPath $dumpDir -Filter "PhasmaPlayer*.dmp" -ErrorAction SilentlyContinue |
    Where-Object LastWriteTime -ge $started.AddSeconds(-1) |
    Select-Object -ExpandProperty FullName)

$record = [ordered]@{
    started_at = $started.ToString("o")
    exited_at = $ended.ToString("o")
    duration_seconds = [Math]::Round(($ended - $started).TotalSeconds, 3)
    executable = $exePath
    arguments = $PlayerArgs
    pid = $proc.Id
    exit_code = $exitCode
    exit_hex = "0x$exitHex"
    dumps = $dumps
}
$json = $record | ConvertTo-Json -Compress
Add-Content -LiteralPath $Output -Value $json -Encoding utf8
Write-Host "Player exited: code=$exitCode (0x$exitHex), duration=$($record.duration_seconds)s"
if ($dumps.Count) {
    Write-Host "Crash dump: $($dumps -join ', ')"
}
