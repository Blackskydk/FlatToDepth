param(
    [switch]$Test, [switch]$Probe,
    [string]$Game = ''   # a game id from the catalog: skip the game menu and bridge this game's settings
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\games.ps1"
if ($Game) { [void](Get-FtdGame $Game) }      # a typo is reported here, with the games that exist
$exe = Join-Path $FtdBin 'FlatToDepth.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'FlatToDepth.exe not found. From a source checkout, build it first with scripts/build.cmd.' }
Push-Location $FtdRoot
try {
    $argsForFlatToDepth = @('--config', (Join-Path $FtdRoot 'flattodepth.ini'))
    if ($Test) { $argsForFlatToDepth += '--test' }
    if ($Probe) { $argsForFlatToDepth += '--probe' }
    if ($Game) { $argsForFlatToDepth += @('--game', $Game) }
    & $exe @argsForFlatToDepth
    $code = $LASTEXITCODE
} finally { Pop-Location }
exit $code
