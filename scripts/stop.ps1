$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\games.ps1"
$expected = [IO.Path]::GetFullPath((Join-Path $FtdBin 'FlatToDepth.exe'))
Get-Process -Name FlatToDepth -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and [string]::Equals($_.Path,$expected,[StringComparison]::OrdinalIgnoreCase)
} | Stop-Process
Write-Output 'Stopped FlatToDepth instances from this folder. The game remains running.'
