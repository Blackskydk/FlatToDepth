param(
    [Parameter(Mandatory)][string]$Game,                 # a game id from games.catalog.ini or games.user.ini
    [string]$GameDirectory,
    [switch]$AcceptLicense      # confirm you accept the stereo fix author's personal-use license (otherwise you are asked)
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot\games.ps1"
$info = Get-FtdGame $Game
if (-not $info.HasFix) { throw "$($info.Title) has no stereo fix to download in its catalog entry (fix_url). Install a Geo-11 fix for it yourself; see docs\GAMES.md." }
$gameDir = Resolve-FtdGameDirectory $info $GameDirectory
$exe = Join-Path $gameDir $info.Exe
if (-not (Test-Path -LiteralPath $exe)) { throw "Expected $($info.Exe) in the named game directory." }
if ($info.Machine -and (Get-PeMachine $exe) -ne $info.Machine) { throw "This fix expects a $(if ($info.Machine -eq 0x14c) { 'x86' } else { 'x64' }) $($info.Exe)." }
if (Get-Process -Name $info.Process -ErrorAction SilentlyContinue) { throw "Close $($info.Title) before installing." }

# The fix is somebody else's work, licensed for personal, non-commercial use and not for redistribution. FlatToDepth
# never ships it: it is downloaded from the author's own page, onto this PC, with the author's license kept beside it.
if (-not $AcceptLicense) {
    if (-not [Environment]::UserInteractive -or [Console]::IsInputRedirected) { throw 'The stereo fix is licensed for personal, non-commercial use. Re-run with -AcceptLicense to confirm you accept its license.' }
    Write-Host ''
    Write-Host "The Geo-11 stereo fix for $($info.Title) is made by $($info.FixAuthor)."
    Write-Host 'It is licensed for personal, non-commercial use only: you may not redistribute it or use it commercially.'
    Write-Host "FlatToDepth does not include it. It will be downloaded from the author's page and installed into the game folder,"
    Write-Host 'with the author license saved there as LICENSE.txt.'
    if ((Read-Host 'Type YES to accept the license and continue') -ne 'YES') { throw 'License not accepted. Nothing was installed.' }
}
$deps = Join-Path $root '.deps'
$source = Join-Path $deps $info.FixDir
if (-not (Test-Path -LiteralPath (Join-Path $source $info.FixMarker))) {
    New-Item -ItemType Directory -Force -Path $source | Out-Null
    $archive = Join-Path $deps $info.FixArchive
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $info.FixUrl -OutFile $archive }
    # The pinned hash is of the package the maintainers of this project inspected; anything else is refused.
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $info.FixSha256) { throw "Downloaded fix does not match the pinned SHA256 for $($info.FixArchive). Refusing to install it." }
    Expand-FtdArchive $archive $source
    # Some packages wrap the real files in a second archive.
    if ($info.FixInner) { Expand-FtdArchive (Join-Path $source $info.FixInner) $source }
}
$packaging = @('uninstall.bat'); if ($info.FixInner) { $packaging += $info.FixInner }
$items = Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object { $_.Name -notin $packaging }
$planned = foreach ($item in $items) {
    $relative = $item.FullName.Substring($source.Length + 1)
    $target = [IO.Path]::GetFullPath((Join-Path $gameDir $relative))
    if (-not $target.StartsWith($gameDir + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Target escapes game directory.' }
    if (Test-Path -LiteralPath $target) { throw "Existing file would be replaced: $target. Refusing overwrite." }
    [PSCustomObject]@{ Source = $item.FullName; Target = $target; Relative = $relative }
}
$manifestPath = Join-Path $FtdState $info.FixManifest
if (Test-Path -LiteralPath $manifestPath) { throw 'An install manifest already exists; inspect/uninstall that deployment before reinstalling.' }
New-Item -ItemType Directory -Force -Path (Split-Path $manifestPath -Parent) | Out-Null
$installed = [Collections.Generic.List[object]]::new()
function Save-Manifest { [PSCustomObject]@{ GameDirectory = $gameDir; Files = @($installed.ToArray()) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath }
try {
    foreach ($item in $planned) {
        New-Item -ItemType Directory -Force -Path (Split-Path $item.Target -Parent) | Out-Null
        Copy-Item -LiteralPath $item.Source -Destination $item.Target
        if ($item.Relative -eq 'd3dxdm.ini') {
            $text = [IO.File]::ReadAllText($item.Target)
            $text = [regex]::Replace($text, '(?m)^direct_mode\s*=.*$', 'direct_mode = katanga_vr')
            $text = [regex]::Replace($text, '(?m)^dm_separation\s*=.*$', 'dm_separation = 25')
            [IO.File]::WriteAllText($item.Target, $text)
        }
        $installed.Add([PSCustomObject]@{ Relative = $item.Relative; SHA256 = (Get-FileHash -LiteralPath $item.Target).Hash })
        if ($installed.Count % 512 -eq 0) { Save-Manifest }
    }
} catch {
    Save-Manifest
    Write-Warning 'Installation did not finish. The manifest records completed files for selective removal.'
    throw
}
Save-Manifest
Write-Output "Installed the Geo-11 fix for $($info.Title): katanga_vr, separation 25, original convergence 12. Manifest: $manifestPath"
# A release install has no use for the 100+ MB unpacked copy once the files are in the game folder (the small
# download stays, so reinstalling needs no new download).
if ($FtdRelease) { Remove-Item -LiteralPath $source -Recurse -Force -ErrorAction SilentlyContinue }
