param(
    [Parameter(Mandatory)][string]$Game,                 # a game id from games.catalog.ini or games.user.ini
    [string]$GameDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot\games.ps1"
$info = Get-FtdGame $Game
if ($info.VirtualPad) {
    # This game gets the VR controllers as a virtual Xbox controller in Windows (ViGEmBus), so no shim DLL may stand in its folder:
    # a shim answers "no controller" for every pad and would hide the virtual one. Remove one that an earlier version installed.
    if (Test-Path -LiteralPath (Join-Path $FtdState $info.ShimManifest)) { & "$PSScriptRoot\uninstall-gamepad.ps1" -Game $Game }
    if (Test-FtdVirtualGamepadDriver) { Write-Output "$($info.Title) gets the VR controllers as a virtual Xbox controller (ViGEmBus is installed); nothing is added to the game folder." }
    else { Write-Output "$($info.Title) needs the ViGEmBus driver for the VR controllers (a virtual Xbox controller), and it is not installed. FlatToDepth does not install drivers; see docs\GAMES.md. Until then the game can be played with a gamepad or keyboard on the PC." }
    return
}
$gameDir = Resolve-FtdGameDirectory $info $GameDirectory
$gameDir = Get-FtdInstallDirectory $info $gameDir      # from here on: the folder that holds the program, where the shim goes
$exe = Join-Path $gameDir $info.Exe
if (-not (Test-Path -LiteralPath $exe)) { throw "Expected $($info.Exe) in the game directory." }
if (-not $info.Machine) { throw "$($info.Title) does not say whether it is x86 or x64: add machine=x86 or machine=x64 to its catalog entry, so the right controller shim is used." }
if (-not $info.ShimFiles.Count) { throw "$($info.Title) names no controller shim (shim_files) in its catalog entry." }
if ((Get-PeMachine $exe) -ne $info.Machine) { throw "$($info.Exe) is not the expected $(if ($info.Machine -eq 0x14c) { 'x86' } else { 'x64' }) build." }
$buildScript = if ($info.Machine -eq 0x14c) { 'scripts\build-gamepad.cmd' } else { 'scripts\build-gamepad64.cmd' }
$manifest = Join-Path $FtdState $info.ShimManifest

# What an earlier install recorded, so an FlatToDepth shim can be updated in place but nothing else is overwritten.
$known = @{}
if (Test-Path -LiteralPath $manifest) {
    $old = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if ($old.PSObject.Properties.Name -contains 'Files') { foreach ($f in $old.Files) { $known[$f.Relative.ToLowerInvariant()] = $f.SHA256 } }
    else { $known[[IO.Path]::GetFileName($old.Target).ToLowerInvariant()] = $old.SHA256 }   # first-version manifest
}
$plan = foreach ($name in $info.ShimFiles) {
    $source = Join-Path $info.ShimBuild $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing $source. Build it first with $buildScript." }
    $target = Join-Path $gameDir $name
    if (Test-Path -LiteralPath $target) {
        if (-not $known.ContainsKey($name.ToLowerInvariant())) { throw "An existing $name has no FlatToDepth record; refusing overwrite." }
        if ((Get-FileHash -LiteralPath $target).Hash -ne $known[$name.ToLowerInvariant()]) { throw "Installed $name changed since FlatToDepth installed it; refusing overwrite." }
        if (Get-Process -Name $info.Process -ErrorAction SilentlyContinue) { throw "Close $($info.Title) before updating its loaded input DLL." }
    }
    [PSCustomObject]@{ Name = $name; Source = $source; Target = $target }
}
New-Item -ItemType Directory -Force -Path (Split-Path $manifest -Parent) | Out-Null
$installed = foreach ($item in $plan) {
    Copy-Item -LiteralPath $item.Source -Destination $item.Target -Force
    [PSCustomObject]@{ Relative = $item.Name; SHA256 = (Get-FileHash -LiteralPath $item.Target).Hash }
}
[PSCustomObject]@{ GameDirectory = $gameDir; Files = @($installed) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifest
Write-Output "Installed FlatToDepth input shim for $($info.Title): $(($info.ShimFiles | ForEach-Object { Join-Path $gameDir $_ }) -join ', '). Restart the game to load it."
