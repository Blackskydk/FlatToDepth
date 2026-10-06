param([Parameter(Mandatory)][string]$Game)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot\games.ps1"
$info = Get-FtdGame $Game
$manifestPath = Join-Path $FtdState $info.FixManifest
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
# If Steam moved the game to another library since, the files moved with it: follow it before removing anything.
try {
    if (Sync-FtdRecordDirectory $manifestPath (Resolve-FtdGameDirectory $info $null)) {
        Write-Output 'The game was moved to another Steam library; the install record was updated.'
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    }
} catch { }
$gameDir = [IO.Path]::GetFullPath($manifest.GameDirectory).TrimEnd('\')
if (Get-Process -Name $info.Process -ErrorAction SilentlyContinue) { throw "Close $($info.Title) before uninstalling." }
$remaining = [Collections.Generic.List[object]]::new()
foreach ($item in $manifest.Files) {
    $target = [IO.Path]::GetFullPath((Join-Path $gameDir $item.Relative))
    if (-not $target.StartsWith($gameDir + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest target escapes game directory.' }
    if (-not (Test-Path -LiteralPath $target)) { continue }
    if ((Get-FileHash -LiteralPath $target).Hash -ne $item.SHA256) {
        Write-Warning "Keeping file changed since installation: $target"
        $remaining.Add($item)
        continue
    }
    Remove-Item -LiteralPath $target
}
if ($remaining.Count) {
    [PSCustomObject]@{ GameDirectory = $gameDir; Files = @($remaining.ToArray()) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath
} else { Remove-Item -LiteralPath $manifestPath }
Write-Output "Removed only unchanged files recorded by the FlatToDepth installer for $($info.Title). Empty directories may remain."
