# Removes what the installer added to the games: the stereo fix files and the controller shim. Only files that
# are still exactly as FlatToDepth installed them are removed. To remove FlatToDepth itself, delete this folder afterwards.
param([switch]$DryRun)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\games.ps1"
function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }
Say ''
Say 'FlatToDepth uninstaller' 'Cyan'
Say '-----------------' 'Cyan'
$failed = $false
foreach ($game in $FtdGames.Values) {
    $hasFix = Test-Path -LiteralPath (Join-Path $FtdState $game.FixManifest)
    $hasShim = Test-Path -LiteralPath (Join-Path $FtdState $game.ShimManifest)
    if (-not ($hasFix -or $hasShim)) { Say "$($game.Title): nothing installed by FlatToDepth." 'DarkGray'; continue }
    Say $game.Title 'Cyan'
    try {
        if ($DryRun) { Say "  would remove:$(if ($hasFix) { ' stereo fix' })$(if ($hasShim) { ' controller shim' })"; continue }
        if ($hasShim) { & "$PSScriptRoot\uninstall-gamepad.ps1" -Game $game.Id | ForEach-Object { Say "  $_" 'Green' } }
        if ($hasFix) { & "$PSScriptRoot\uninstall-geo11.ps1" -Game $game.Id | ForEach-Object { Say "  $_" 'Green' } }
    } catch { Say "  FAILED: $($_.Exception.Message)" 'Red'; $failed = $true }
}
Say ''
Say 'Your settings (flattodepth*.ini, games.user.ini) and logs were kept. Delete this folder to remove FlatToDepth completely.'
if ($failed) { exit 1 }
