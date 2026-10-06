param([Parameter(Mandatory)][string]$Game)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot\games.ps1"
$info = Get-FtdGame $Game
$manifest = Join-Path $FtdState $info.ShimManifest
$install = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
# If Steam moved the game to another library since, the files moved with it: follow it before removing anything.
try {
    if (Sync-FtdRecordDirectory $manifest (Resolve-FtdGameDirectory $info $null)) {
        Write-Output 'The game was moved to another Steam library; the install record was updated.'
        $install = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    }
} catch { }
# First-version manifests recorded a single Target; newer ones list Files relative to GameDirectory.
if ($install.PSObject.Properties.Name -contains 'Files') {
    $gameDir = [IO.Path]::GetFullPath($install.GameDirectory).TrimEnd('\')
    $entries = foreach ($f in $install.Files) { [PSCustomObject]@{ Path = [IO.Path]::GetFullPath((Join-Path $gameDir $f.Relative)); SHA256 = $f.SHA256 } }
} else {
    $entries = @([PSCustomObject]@{ Path = [IO.Path]::GetFullPath($install.Target); SHA256 = $install.SHA256 })
    $gameDir = Split-Path $entries[0].Path -Parent
}
if (-not (Test-Path -LiteralPath (Join-Path $gameDir $info.Exe))) { throw 'Unexpected input shim location.' }
if (Get-Process -Name $info.Process -ErrorAction SilentlyContinue) { throw "Close $($info.Title) first: it has the input DLL loaded." }
$kept = $false
foreach ($entry in $entries) {
    if (-not $entry.Path.StartsWith($gameDir + '\', [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($entry.Path) -notmatch '^xinput[\w.]*\.dll$') { throw 'Unexpected input shim target.' }
    if (-not (Test-Path -LiteralPath $entry.Path)) { continue }
    if ((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.SHA256) { Write-Warning "Keeping changed file: $($entry.Path)"; $kept = $true; continue }
    Remove-Item -LiteralPath $entry.Path
}
if (-not $kept) { Remove-Item -LiteralPath $manifest }
Write-Output "Removed FlatToDepth input compatibility DLL(s) for $($info.Title). Restart the game to use system XInput again."
