# Tests for the install scripts' logic, with fake Steam libraries and fake game folders (nothing real is touched).
# Run with Windows PowerShell 5.1 or PowerShell 7:  powershell -NoProfile -File tests\scripts_test.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $root 'scripts\games.ps1')
$script:failures = 0
function Check([bool]$condition, [string]$message) {
    if ($condition) { Write-Host "ok    $message" } else { Write-Host "FAIL  $message" -ForegroundColor Red; $script:failures++ }
}
$temp = Join-Path ([IO.Path]::GetTempPath()) ('flattodepth-scripts-test-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $temp | Out-Null
$env:FLATTODEPTH_STATE_DIR = Join-Path $temp 'state'     # install records live here, not in the real build folder
try {
    $game = $FtdGames['wotw']
    # A fake game folder whose exe is a real 64-bit executable, as the installer checks.
    function New-FakeGame([string]$library, [string]$folder) {
        $dir = Join-Path $library "steamapps\common\$folder"
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\cmd.exe') -Destination (Join-Path $dir $game.Exe)
        $dir
    }
    function New-Library([string]$name) { $p = Join-Path $temp $name; New-Item -ItemType Directory -Force -Path (Join-Path $p 'steamapps') | Out-Null; $p }
    function Write-AppManifest([string]$library, [string]$installDir) {
        Set-Content -LiteralPath (Join-Path $library "steamapps\appmanifest_$($game.AppId).acf") -Value ("`"AppState`"`n{`n`t`"installdir`"`t`t`"" + $installDir + "`"`n}")
    }

    # --- Steam libraries come from the override when one is set -------------------------------------------------------
    $libA = New-Library 'LibA'; $libB = New-Library 'LibB'
    $env:FLATTODEPTH_STEAM_LIBRARIES = "$libA;$libB"
    Check ((@(Get-FtdSteamLibraries)).Count -eq 2) 'fake libraries are used'

    # --- The launchers ------------------------------------------------------------------------------------------------
    $mainStart = [IO.File]::ReadAllText((Join-Path $root 'scripts\start-flattodepth.cmd')); $launcher = [IO.File]::ReadAllText((Join-Path $root 'Start-FlatToDepth.cmd'))
    Check (($mainStart -match 'FlatToDepth\.exe') -and ($mainStart -match 'flattodepth\.ini') -and ($launcher -match 'start-flattodepth\.cmd')) 'the launcher runs FlatToDepth.exe with flattodepth.ini'

    # --- The release package: top-level folder, what is in it and what is not -----------------------------------------
    $fakeBin = Join-Path $temp 'pkgbin'
    foreach ($f in 'FlatToDepth.exe', 'openxr_loader.dll', 'FlatToDepthCaptureProbe.exe', 'gamepad\xinput9_1_0.dll', 'gamepad64\xinput1_4.dll', 'gamepad64\xinput1_3.dll') {
        $path = Join-Path $fakeBin $f; New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null; Set-Content -LiteralPath $path 'x'
    }
    $pkgOut = Join-Path $temp 'dist'
    $pkgText = & (Join-Path $root 'scripts\package.ps1') -Version 'vtest' -BinDir $fakeBin -OutDir $pkgOut 2>&1 | Out-String
    $zipFile = Join-Path $pkgOut 'FlatToDepth-vtest-win64.zip'
    Check (Test-Path -LiteralPath $zipFile) "package.ps1 makes FlatToDepth-<version>-win64.zip ($($pkgText.Trim()))"
    if (Test-Path -LiteralPath $zipFile) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::OpenRead($zipFile)
        try {
            $entries = @($zip.Entries | ForEach-Object { $_.FullName -replace '\\', '/' })
            Check (@($entries | Where-Object { $_ -notlike 'FlatToDepth/*' }).Count -eq 0) 'everything in the zip sits in one top-level folder, FlatToDepth'
            foreach ($f in 'bin/FlatToDepth.exe', 'bin/gamepad/xinput9_1_0.dll', 'bin/gamepad64/xinput1_4.dll', 'Install.cmd', 'Uninstall.cmd', 'Start-FlatToDepth.cmd', 'flattodepth.default.ini', 'games.catalog.ini',
                'scripts/start-flattodepth.cmd', 'scripts/launch-game.cmd', 'scripts/games.ps1', 'docs/GAMES.md', 'docs/INSTALL.md', 'VERSION.txt') {
                Check ($entries -contains "FlatToDepth/$f") "the zip has $f"
            }
        } finally { $zip.Dispose() }
    }

    # --- A stale leftover folder must not beat the library Steam says has the game ------------------------------------
    $stale = New-FakeGame $libA $game.Folder                      # leftover folder, no Steam record
    $real = New-FakeGame $libB $game.Folder
    Write-AppManifest $libB $game.Folder
    Check ((Resolve-FtdGameDirectory $game $null) -eq $real) "the library with Steam's appmanifest wins over a stale folder"
    # installdir in the manifest is honoured, not the folder name we guess
    Remove-Item -LiteralPath $real -Recurse -Force
    $renamed = New-FakeGame $libB 'Some Other Folder Name'
    Write-AppManifest $libB 'Some Other Folder Name'
    Check ((Resolve-FtdGameDirectory $game $null) -eq $renamed) "the manifest's installdir is used"
    # no Steam record at all: fall back to the usual folder name
    Remove-Item -LiteralPath (Join-Path $libB "steamapps\appmanifest_$($game.AppId).acf")
    Check ((Resolve-FtdGameDirectory $game $null) -eq $stale) 'without a Steam record the usual folder name is used'
    # nothing anywhere
    $env:FLATTODEPTH_STEAM_LIBRARIES = (New-Library 'Empty')
    $threw = $false; try { [void](Resolve-FtdGameDirectory $game $null) } catch { $threw = $true }
    Check $threw 'a game that is not installed is reported, not guessed'
    Check ((Resolve-FtdGameDirectory $game $stale) -eq $stale) 'an explicit folder is honoured'
    $env:FLATTODEPTH_STEAM_LIBRARIES = "$libA;$libB"

    # --- A game moved between libraries: the install record follows -------------------------------------------------
    $from = Join-Path $temp 'from'; New-Item -ItemType Directory -Path $from | Out-Null
    Set-Content -LiteralPath (Join-Path $from 'a.dll') 'x'; Set-Content -LiteralPath (Join-Path $from 'b.dll') 'y'
    $record = Join-Path $temp 'multi.json'
    [PSCustomObject]@{ GameDirectory = $from; Files = @([PSCustomObject]@{ Relative = 'a.dll'; SHA256 = '1' }, [PSCustomObject]@{ Relative = 'b.dll'; SHA256 = '2' }) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $record
    $to = Join-Path $temp 'to'; Move-Item -LiteralPath $from -Destination $to
    Check (Sync-FtdRecordDirectory $record $to) 'a moved game updates its record'
    $updated = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    Check (($updated.GameDirectory -eq $to) -and (@($updated.Files).Count -eq 2)) 'the record points at the new folder and keeps its files'
    Check (-not (Sync-FtdRecordDirectory $record $to)) 'syncing again changes nothing'
    $one = Join-Path $temp 'one.json'
    [PSCustomObject]@{ GameDirectory = $to; Files = @([PSCustomObject]@{ Relative = 'a.dll'; SHA256 = '1' }) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $one
    $to2 = Join-Path $temp 'to2'; Move-Item -LiteralPath $to -Destination $to2
    Check ((Sync-FtdRecordDirectory $one $to2) -and ((Get-Content -LiteralPath $one -Raw) -match '"Files":\s*\[')) 'a one-file record stays a list'
    $legacy = Join-Path $temp 'legacy.json'
    [PSCustomObject]@{ Target = (Join-Path $to 'a.dll'); SHA256 = '1' } | ConvertTo-Json | Set-Content -LiteralPath $legacy
    Check ((Sync-FtdRecordDirectory $legacy $to2) -and ((Get-Content -LiteralPath $legacy -Raw | ConvertFrom-Json).Target -eq (Join-Path $to2 'a.dll'))) 'a first-version record is followed too'
    $unrelated = Join-Path $temp 'unrelated'; New-Item -ItemType Directory -Path $unrelated | Out-Null
    Check (-not (Sync-FtdRecordDirectory $record $unrelated)) 'a folder that does not hold our files is never adopted'

    # --- The installer, end to end in dry-run mode, with fake libraries --------------------------------------------------
    if (Test-Path -LiteralPath (Join-Path $FtdBin 'FlatToDepth.exe')) {
        $out = & (Join-Path $root 'scripts\install.ps1') -DryRun -Yes -Games wotw 6>&1 | Out-String
        Check (($out -match 'Ori and the Will of the Wisps') -and ($out -match 'would download') -and ($out -match 'virtual Xbox controller') -and ($out -match 'OK ')) 'install.ps1 -DryRun plans the fix and the virtual controller for a fake game'
        Check ($out -notmatch 'FAIL') 'install.ps1 -DryRun reports no failure'
        $out2 = & (Join-Path $root 'scripts\uninstall.ps1') -DryRun 6>&1 | Out-String
        Check (($out2 -match 'nothing installed') -or ($out2 -match 'would remove')) 'uninstall.ps1 -DryRun runs'
    } else { Write-Host 'skip  installer dry run (FlatToDepth.exe is not built here)' }

    # --- A game the user added themselves (games.user.ini) goes through the same installers -----------------------------
    $catalog = Join-Path $temp 'catalog'; New-Item -ItemType Directory -Path $catalog | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'games.catalog.ini') -Destination $catalog
    Set-Content -LiteralPath (Join-Path $catalog 'games.user.ini') -Value ("[zed]`ntitle=Zed Quest`nsteam_app_id=1001`nexe=zed.exe`nmachine=x64`nfolder=Zed`nshim_files=xinput1_4.dll`n" +
        "[nomachine]`ntitle=No Machine`nsteam_app_id=1002`nexe=nm.exe`nfolder=NoMachine`nshim_files=xinput1_4.dll`n[bad one]`ntitle=Broken`n")
    $userLib = New-Library 'UserLib'
    foreach ($entry in @(@('Zed', 'zed.exe', 1001), @('NoMachine', 'nm.exe', 1002))) {
        $dir = Join-Path $userLib "steamapps\common\$($entry[0])"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\cmd.exe') -Destination (Join-Path $dir $entry[1])
        Set-Content -LiteralPath (Join-Path $userLib "steamapps\appmanifest_$($entry[2]).acf") -Value ("`"AppState`"`n{`n`t`"appid`"`t`t`"$($entry[2])`"`n`t`"installdir`"`t`t`"$($entry[0])`"`n}")
    }
    $env:FLATTODEPTH_STEAM_LIBRARIES = $userLib; $env:FLATTODEPTH_CATALOG_DIR = $catalog
    $zedDir = Join-Path $userLib 'steamapps\common\Zed'; $noMachineDir = Join-Path $userLib 'steamapps\common\NoMachine'
    if (Test-Path -LiteralPath (Join-Path $FtdBin 'FlatToDepth.exe')) {
        $out = & (Join-Path $root 'scripts\install.ps1') -DryRun -Yes -Games zed 6>&1 | Out-String
        Check (($out -match 'Zed Quest') -and ($out -match 'catalog entry has no download') -and ($out -match 'would install xinput1_4.dll') -and ($out -match 'OK ')) 'a user-defined game is planned: no fix to download, its own shim'
        Check (($out -notmatch 'FAIL') -and ($out -match "skipped") -and ($out -match 'bad one')) 'a broken entry in games.user.ini is reported without stopping anything'
        $out = & (Join-Path $root 'scripts\install.ps1') -DryRun -Yes -Games nomachine 6>&1 | Out-String
        Check (($out -match 'FAIL') -and ($out -match 'machine=x86 or x64')) 'an entry without machine= is told what to add, and fails alone'
        $unknown = $false; try { & (Join-Path $root 'scripts\install.ps1') -DryRun -Games notagame 6>&1 | Out-Null } catch { $unknown = $_.Exception.Message -match "Unknown game 'notagame'.*zed" }
        Check $unknown 'a mistyped game id is reported with the games that exist, user games included'
    } else { Write-Host 'skip  user-defined game dry run (FlatToDepth.exe is not built here)' }
    $noFix = $false; try { & (Join-Path $root 'scripts\install-geo11.ps1') -Game zed -GameDirectory $zedDir -AcceptLicense | Out-Null } catch { $noFix = $_.Exception.Message -match 'no stereo fix to download' }
    Check $noFix 'the fix installer refuses a game whose entry has no download'
    $noShim = $false; try { & (Join-Path $root 'scripts\install-gamepad.ps1') -Game nomachine -GameDirectory $noMachineDir | Out-Null } catch { $noShim = $_.Exception.Message -match 'x86 or x64' }
    Check $noShim 'the shim installer refuses a game that does not say its bitness'
    $env:FLATTODEPTH_CATALOG_DIR = $null; Remove-Item Env:FLATTODEPTH_CATALOG_DIR -ErrorAction SilentlyContinue
    $env:FLATTODEPTH_STEAM_LIBRARIES = "$libA;$libB"

    # --- The fix must not install without the license being accepted ---------------------------------------------------
    $refused = $false
    try { & (Join-Path $root 'scripts\install-geo11.ps1') -Game wotw -GameDirectory $stale } catch { $refused = $true }
    Check $refused 'the stereo fix refuses to install without accepting its license (non-interactive)'
} finally {
    Remove-Item Env:FLATTODEPTH_STEAM_LIBRARIES -ErrorAction SilentlyContinue
    Remove-Item Env:FLATTODEPTH_CATALOG_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:FLATTODEPTH_STATE_DIR -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:failures) { Write-Host "$script:failures script check(s) failed" -ForegroundColor Red; exit 1 }
Write-Host 'PASS install scripts: Steam library resolution, moved games, installer dry run, user-defined games, license gate'
# The checks above run install.ps1 on entries that are meant to fail, and its exit 1 stays in $LASTEXITCODE, which the CI
# shell would report as this script's own result.
exit 0
