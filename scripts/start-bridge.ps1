# Starts the FlatToDepth bridge for a game that is launching (or already running), then exits.
# launch-game.cmd runs this in the background; it is also safe to run by hand.
# It never blocks or breaks the game: every problem is written to logs\launch.log and ignored.
param(
    [string]$Game = '',                                           # a catalog game id to wait for; empty means whichever shows a window first
    [string]$Bridge,                                              # defaults to build\FlatToDepth.exe
    [int]$WaitSeconds = 180,                                      # how long to wait for the game window
    [switch]$DryRun                                               # report what would happen, start nothing
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot\games.ps1"
if (-not $Bridge) { $Bridge = Join-Path $FtdBin 'FlatToDepth.exe' }
$logFile = Join-Path $root 'logs\launch.log'
New-Item -ItemType Directory -Force -Path (Split-Path $logFile) | Out-Null
function Note([string]$text) {
    $line = '[{0}] {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $text
    Add-Content -LiteralPath $logFile -Value $line
    Write-Output $line
}
try {
    if (-not (Get-Process -Name vrserver -ErrorAction SilentlyContinue)) {
        Note 'SteamVR is not running: game starts flat, no VR bridge.'; return
    }
    if (-not (Test-Path -LiteralPath $Bridge)) { Note "Bridge is not built: $Bridge (run scripts\build.cmd)."; return }
    $Bridge = (Resolve-Path -LiteralPath $Bridge).Path
    $running = Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($Bridge)) -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and [string]::Equals($_.Path, $Bridge, [StringComparison]::OrdinalIgnoreCase) }
    if ($running) { Note 'Bridge is already running (for example the game menu): leaving it alone.'; return }

    # The process that launched us (launch-game.cmd) lives exactly as long as the game; stop waiting if it is gone.
    $parent = $null
    try { $parent = (Get-CimInstance Win32_Process -Filter "ProcessId=$PID").ParentProcessId } catch { }

    # Start the bridge only once the game has a window, so the headset keeps showing the SteamVR
    # dashboard while the game loads instead of an empty void.
    $candidates = if ($Game) { @(Get-FtdGame $Game) } else { @($FtdGames.Values) }
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    $found = $null
    while ((Get-Date) -lt $deadline -and -not $found) {
        foreach ($candidate in $candidates) {
            $window = Get-Process -Name $candidate.Process -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
            if ($window) { $found = $candidate; break }
        }
        if ($found) { break }
        if ($parent -and -not (Get-Process -Id $parent -ErrorAction SilentlyContinue)) { Note 'Launcher ended before the game showed a window.'; return }
        Start-Sleep -Milliseconds 500
    }
    if (-not $found) { Note "No game window within $WaitSeconds s; no VR bridge."; return }

    # --game selects that game's own settings; --follow makes the bridge end when the game does.
    # Every value is quoted, because Start-Process joins them with spaces as they are (a folder or a game name may have spaces).
    $arguments = @('--config', "`"$(Join-Path $root 'flattodepth.ini')`"", '--game', $found.Id, '--follow', "`"$($found.Exe)`"")
    if ($DryRun) { Note "DRY RUN: would start $Bridge $($arguments -join ' ')"; return }
    $process = Start-Process -FilePath $Bridge -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
    Note "Started bridge PID $($process.Id) for $($found.Title) (it exits when the game does)."
} catch {
    Note "Bridge start failed: $($_.Exception.Message)"
}
