# Assembles the release zip from a finished build: the program, both controller shims, the games catalog, the install
# scripts and the guides. The Geo-11 stereo fixes are deliberately NOT included: their license forbids redistribution, so the
# installer downloads them from their author.
#   ./scripts/package.ps1 -Version v0.1.0 [-BinDir build] [-OutDir dist]      (folders are relative to the project, or absolute)
param(
    [string]$Version = 'dev',
    [string]$BinDir = 'build',
    [string]$OutDir = 'dist'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bin = if ([IO.Path]::IsPathRooted($BinDir)) { $BinDir } else { Join-Path $root $BinDir }
$out = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
$required = 'FlatToDepth.exe', 'openxr_loader.dll', 'FlatToDepthCaptureProbe.exe', 'gamepad\xinput9_1_0.dll', 'gamepad64\xinput1_4.dll', 'gamepad64\xinput1_3.dll'
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $bin $file))) { throw "Missing $file in $bin. Run scripts\build.cmd, build-gamepad.cmd and build-gamepad64.cmd first." }
}
$name = "FlatToDepth-$Version-win64"
$work = Join-Path $out $name
$stage = Join-Path $work 'FlatToDepth'          # the zip's single top-level folder; the same every release, so a new one can be extracted over the old
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
foreach ($dir in 'bin\gamepad', 'bin\gamepad64', 'scripts', 'docs') { New-Item -ItemType Directory -Force -Path (Join-Path $stage $dir) | Out-Null }
function Put([string]$from, [string]$to) { Copy-Item -LiteralPath (Join-Path $root $from) -Destination (Join-Path $stage $to) }
foreach ($file in 'FlatToDepth.exe', 'openxr_loader.dll', 'FlatToDepthCaptureProbe.exe') { Copy-Item -LiteralPath (Join-Path $bin $file) -Destination (Join-Path $stage "bin\$file") }
Copy-Item -LiteralPath (Join-Path $bin 'gamepad\xinput9_1_0.dll') -Destination (Join-Path $stage 'bin\gamepad')
foreach ($file in 'xinput1_4.dll', 'xinput1_3.dll') { Copy-Item -LiteralPath (Join-Path $bin "gamepad64\$file") -Destination (Join-Path $stage 'bin\gamepad64') }
foreach ($file in 'install.ps1', 'uninstall.ps1', 'games.ps1', 'install-geo11.ps1', 'uninstall-geo11.ps1', 'install-gamepad.ps1', 'uninstall-gamepad.ps1',
    'run.ps1', 'stop.ps1', 'start-bridge.ps1', 'launch-game.cmd', 'start-flattodepth.cmd') { Put "scripts\$file" "scripts\$file" }
foreach ($file in 'Install.cmd', 'Uninstall.cmd', 'Start-FlatToDepth.cmd', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'flattodepth.default.ini', 'games.catalog.ini') { Put $file $file }
foreach ($file in 'INSTALL.md', 'USAGE.md', 'GAMES.md') { Put "docs\$file" "docs\$file" }
$commit = 'unknown'    # git may be missing, or the folder may not be a repository
try { $head = & git -C $root rev-parse --short HEAD 2>$null; if ($LASTEXITCODE -eq 0 -and $head) { $commit = $head } } catch { }
$global:LASTEXITCODE = 0                # a failed git call must not leak out as this script's exit code
"FlatToDepth $Version`r`ncommit: $commit`r`nbuilt: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm')) UTC" | Set-Content -LiteralPath (Join-Path $stage 'VERSION.txt') -Encoding ascii
$zip = Join-Path $out "$name.zip"
Remove-Item -LiteralPath $zip -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name.zip" | Set-Content -LiteralPath "$zip.sha256" -Encoding ascii
Write-Output ("{0}  {1:N1} MB  sha256 {2}" -f $zip, ((Get-Item $zip).Length / 1MB), $hash)
