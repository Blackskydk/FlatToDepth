# FlatToDepth installer. Sets FlatToDepth up for the supported games (games.catalog.ini, plus any in your games.user.ini) found in
# your Steam library:
#   - the Geo-11 stereo fix for the game (downloaded from its author, after you accept its license), when its catalog
#     entry says where to get one, and
#   - FlatToDepth's small controller shim, so the Steam Frame controllers play the game.
# Nothing outside the game folders and this folder is changed, and every file added is recorded for uninstalling.
param(
    [string[]]$Games,                                       # game ids from the catalog to set up; default: ask for each one found
    [switch]$Yes,                                           # set up every game found without asking
    [switch]$AcceptFixLicense,                              # accept the stereo fix author's license for all games
    [switch]$DryRun                                         # show what would happen, change nothing
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\games.ps1"
if ($Games) { foreach ($id in $Games) { [void](Get-FtdGame $id) } }      # a typo is reported with the games that exist
function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Ask([string]$question) {
    if ($Yes) { return $true }
    if ([Console]::IsInputRedirected) { return $false }
    $answer = Read-Host "$question [Y/n]"
    return ($answer -eq '' -or $answer -match '^(y|yes)$')
}

Say ''
Say 'FlatToDepth installer' 'Cyan'
Say '---------------' 'Cyan'
if ($DryRun) { Say 'Dry run: nothing will be changed.' 'Yellow' }
foreach ($problem in $FtdCatalogProblems) { Say "Games list: $problem" 'Yellow' }

# 1. The program itself.
$exe = Join-Path $FtdBin 'FlatToDepth.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "FlatToDepth.exe was not found in $FtdBin. Download the release zip from GitHub, or build it first (docs\BUILDING.md)." }
if (-not [Environment]::Is64BitOperatingSystem) { throw 'FlatToDepth needs 64-bit Windows 10 or 11.' }
Say "FlatToDepth found: $exe" 'Green'

# 2. SteamVR and the OpenXR runtime (checked, never changed: that is a system setting).
$libraries = @(Get-FtdSteamLibraries)
if (-not $libraries) { throw 'Steam does not seem to be installed. Install Steam and the games first.' }
$steamvr = $libraries | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'steamapps\common\SteamVR\bin\win64\vrserver.exe') } | Select-Object -First 1
if ($steamvr) { Say 'SteamVR found.' 'Green' }
else { Say 'SteamVR was not found. Install it from Steam (Library > Tools > SteamVR) before using FlatToDepth.' 'Yellow' }
$runtime = (Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -ErrorAction SilentlyContinue).ActiveRuntime
if ($runtime -and $runtime -match 'steam') { Say 'SteamVR is the active OpenXR runtime.' 'Green' }
elseif ($runtime) { Say "The active OpenXR runtime is $runtime, not SteamVR." 'Yellow'; Say '  In SteamVR open Settings > OpenXR and choose "Set SteamVR as OpenXR Runtime".' 'Yellow' }
else { Say 'No OpenXR runtime is set. In SteamVR open Settings > OpenXR and choose "Set SteamVR as OpenXR Runtime".' 'Yellow' }

# 3. Which games are installed.
$found = foreach ($game in $FtdGames.Values) {
    try { [PSCustomObject]@{ Game = $game; Directory = (Resolve-FtdGameDirectory $game $null) } } catch { }
}
$found = @($found)
foreach ($game in $FtdGames.Values) { if (-not ($found | Where-Object { $_.Game.Id -eq $game.Id })) { Say "Not installed: $($game.Title)" 'DarkGray' } }
if (-not $found) { throw 'None of the supported games was found in your Steam libraries. Install one from Steam first, or add yours to games.user.ini (see docs\GAMES.md).' }

$chosen = foreach ($entry in $found) {
    if ($Games) { if ($Games -contains $entry.Game.Id) { $entry } }
    elseif (Ask "Set up $($entry.Game.Title)?") { $entry }
}
$chosen = @($chosen)
if (-not $chosen) { Say 'Nothing selected. Run this again to set a game up.' 'Yellow'; return }

# 4. Set each game up. One game failing never stops the next.
$results = @()
foreach ($entry in $chosen) {
    $game = $entry.Game; $dir = $entry.Directory
    Say ''
    Say $game.Title 'Cyan'
    Say "  folder: $dir"
    try {
        if (Get-Process -Name $game.Process -ErrorAction SilentlyContinue) { throw "$($game.Title) is running. Close it and run the installer again." }
        $fixRecord = Join-Path $FtdState $game.FixManifest
        if (-not $DryRun) {
            # A game moved to another Steam library keeps our files; just point the records at its new folder.
            foreach ($record in $fixRecord, (Join-Path $FtdState $game.ShimManifest)) {
                if (Sync-FtdRecordDirectory $record $dir) { Say '  the game was moved to another Steam library; install record updated.' 'Yellow' }
            }
        }
        $foreignFix = (Test-Path -LiteralPath (Join-Path $dir 'd3d11.dll')) -and -not (Test-Path -LiteralPath $fixRecord)
        if (Test-Path -LiteralPath $fixRecord) { Say '  stereo fix: already installed by FlatToDepth.' 'Green' }
        elseif ($foreignFix) {
            Say '  stereo fix: a d3d11.dll is already in the game folder (not installed by FlatToDepth), so it was left alone.' 'Yellow'
            Say '  Make sure its d3dxdm.ini has  direct_mode = katanga_vr  and force_stereo=2 in d3dx.ini.' 'Yellow'
        }
        elseif (-not $game.HasFix) {
            Say '  stereo fix: this game''s catalog entry has no download, so none was installed. Install a Geo-11 fix for it yourself' 'Yellow'
            Say '  (docs\GAMES.md says how); its d3dxdm.ini needs  direct_mode = katanga_vr  and force_stereo=2 in d3dx.ini.' 'Yellow'
        }
        elseif ($DryRun) { Say "  stereo fix: would download $($game.FixArchive) from its author and install it." }
        else {
            $licenseSwitch = @{}; if ($AcceptFixLicense) { $licenseSwitch.AcceptLicense = $true }
            & "$PSScriptRoot\install-geo11.ps1" -Game $game.Id -GameDirectory $dir @licenseSwitch | ForEach-Object { Say "  $_" 'Green' }
        }
        if (-not $game.Machine -or -not $game.ShimFiles.Count) { throw 'its catalog entry needs machine=x86 or x64 and shim_files before the controller shim can be installed' }
        if ($DryRun) { Say "  controller shim: would install $($game.ShimFiles -join ', ')." }
        else { & "$PSScriptRoot\install-gamepad.ps1" -Game $game.Id -GameDirectory $dir | ForEach-Object { Say "  $_" 'Green' } }
        $results += "OK    $($game.Title)"
    } catch {
        Say "  FAILED: $($_.Exception.Message)" 'Red'
        $results += "FAIL  $($game.Title): $($_.Exception.Message)"
    }
}

# 5. What next.
Say ''
Say 'Summary' 'Cyan'
$results | ForEach-Object { Say "  $_" $(if ($_ -like 'FAIL*') { 'Red' } else { 'Green' }) }
if (-not $DryRun) {
    Say ''
    Say 'Next:' 'Cyan'
    Say '  1. Start SteamVR and put the headset on.'
    Say '  2. Double-click Start-FlatToDepth.cmd. A game menu appears in the headset; point at a game and pull the trigger.'
    Say '  3. To start it from the headset, add scripts\start-flattodepth.cmd to Steam (see docs\INSTALL.md).'
    Say '  Restart a game if it was open: it loads the controller shim when it starts.'
}
if ($results | Where-Object { $_ -like 'FAIL*' }) { exit 1 }
