# The games FlatToDepth can show in 3D, for the install scripts: read from the games catalog (see below).
# Dot-source this file: . "$PSScriptRoot\games.ps1"
# Where things are. A release package keeps binaries in bin\ and install records in state\; a source checkout
# builds into build\ and keeps the records next to the binaries.
$FtdRoot = Split-Path $PSScriptRoot -Parent
$FtdRelease = Test-Path -LiteralPath (Join-Path $FtdRoot 'bin\FlatToDepth.exe')
$FtdBin = Join-Path $FtdRoot $(if ($FtdRelease) { 'bin' } else { 'build' })
$FtdState = Join-Path $FtdRoot $(if ($FtdRelease) { 'state' } else { 'build' })
if ($env:FLATTODEPTH_STATE_DIR) { $FtdState = $env:FLATTODEPTH_STATE_DIR }     # tests use a throwaway folder

# --- The catalog -----------------------------------------------------------------------------------------------------
# The games come from games.catalog.ini (shipped; updates replace it) and games.user.ini (yours; never overwritten, and a
# section there with the same name as a catalog one replaces it). FlatToDepth.exe reads the same two files with the same rules,
# so the menu and the installers cannot disagree about a game. See docs\GAMES.md for the format.
$FtdCatalogDir = if ($env:FLATTODEPTH_CATALOG_DIR) { $env:FLATTODEPTH_CATALOG_DIR } else { $FtdRoot }
$FtdCatalogProblems = [System.Collections.Generic.List[string]]::new()

# Plain INI: [section], key=value; a line starting with ; or # is a comment; UTF-8. Keys are lower-cased, the last of a
# repeated key wins. Returns the sections in file order, each with a Name and a Values table.
function Read-FtdIni([string]$Path) {
    $sections = [System.Collections.Generic.List[object]]::new()
    if (-not (Test-Path -LiteralPath $Path)) { return $sections.ToArray() }
    $text = [IO.File]::ReadAllText($Path, [Text.UTF8Encoding]::new($false))
    $current = $null
    foreach ($raw in ($text -split "`n")) {
        $line = $raw.Trim()
        if (-not $line -or $line.StartsWith(';') -or $line.StartsWith('#')) { continue }
        if ($line.StartsWith('[')) {
            $end = $line.IndexOf(']')
            if ($end -gt 0) { $current = [PSCustomObject]@{ Name = $line.Substring(1, $end - 1).Trim(); Values = @{} }; $sections.Add($current) }
            continue
        }
        $eq = $line.IndexOf('=')
        if ($eq -lt 1 -or -not $current) { continue }
        $current.Values[$line.Substring(0, $eq).Trim().ToLowerInvariant()] = $line.Substring($eq + 1).Trim()
    }
    return $sections.ToArray()
}
# A name that ends up in a file path or a command line: letters, digits and a few separators, never a folder.
function Test-FtdPlainName([string]$Name, [string]$Allowed = '_.-') {
    if (-not $Name -or $Name.Length -gt 96 -or $Name.Contains('..')) { return $false }
    foreach ($c in $Name.ToCharArray()) { if (-not (([char]::IsLetterOrDigit($c) -and [int]$c -lt 128) -or $Allowed.Contains([string]$c))) { return $false } }
    return $true
}
# The file name a download address ends in, %-escapes decoded and any ?query or #fragment dropped: what the archive is
# called on disk when the entry does not say. Same algorithm as urlFileName() in src\catalog.hpp.
function Get-FtdUrlFileName([string]$Url) {
    $rest = $Url
    $scheme = $rest.IndexOf('://'); if ($scheme -ge 0) { $rest = $rest.Substring($scheme + 3) }
    $slash = $rest.IndexOf('/'); $rest = if ($slash -lt 0) { '' } else { $rest.Substring($slash) }
    $cut = $rest.IndexOfAny([char[]]'?#'); if ($cut -ge 0) { $rest = $rest.Substring(0, $cut) }
    $name = $rest.Substring($rest.LastIndexOf('/') + 1)
    [regex]::Replace($name, '%([0-9A-Fa-f]{2})', { param($m) [string][char][Convert]::ToInt32($m.Groups[1].Value, 16) })
}
# Turns one catalog section into the table the install scripts use, or throws what is wrong with it. These are the same
# rules as src\catalog.hpp: a game with a problem is skipped, never half-loaded.
function ConvertTo-FtdGame($Section, [string]$Source) {
    $v = $Section.Values
    function Field([string]$Name, [string]$Default = '') { if ($v.ContainsKey($Name)) { $v[$Name] } else { $Default } }
    $id = $Section.Name
    if ($id -cnotmatch '^[a-z0-9][a-z0-9_-]{0,31}$') { throw 'the section name must be 1 to 32 lower-case letters, digits, - or _' }
    $title = Field 'title'
    if (-not $title) { throw 'title is missing' }
    $appText = Field 'steam_app_id'
    $app = [uint64]0
    if ($appText -notmatch '^[0-9]{1,10}$' -or -not [uint64]::TryParse($appText, [ref]$app) -or $app -eq 0 -or $app -gt 4294967295) { throw "steam_app_id must be the game's Steam app number" }
    $exe = Field 'exe'
    if (-not (Test-FtdPlainName $exe ' _.-+') -or $exe.Length -lt 5 -or -not $exe.EndsWith('.exe', [StringComparison]::OrdinalIgnoreCase)) { throw 'exe must be the game''s executable name, like game.exe, with no folder' }
    $exeDir = Field 'exe_dir'
    if ($exeDir -and -not (Test-FtdPlainName $exeDir)) { throw 'exe_dir must be the name of one folder inside the game folder, with no slashes' }
    $fixRoot = Field 'fix_root'
    if ($fixRoot -and -not (Test-FtdPlainName $fixRoot)) { throw 'fix_root must be the name of one folder inside the fix download, with no slashes' }
    $machineText = Field 'machine'
    if ($machineText -notin @('', 'x86', 'x64')) { throw 'machine must be x86 or x64' }
    $profile = Field 'profile' "flattodepth-$id.ini"
    if (-not (Test-FtdPlainName $profile) -or $profile.Length -lt 5 -or -not $profile.EndsWith('.ini', [StringComparison]::OrdinalIgnoreCase)) { throw 'profile must be a file name ending in .ini, with no folder' }
    $keys = @(); $used = @{}
    foreach ($k in 1..6) {
        if (-not $v.ContainsKey("key$k")) { continue }
        $spec = $v["key$k"]; $bar = $spec.LastIndexOf('|')
        $label = if ($bar -ge 0) { $spec.Substring(0, $bar).Trim() } else { '' }
        $key = if ($bar -ge 0) { $spec.Substring($bar + 1).Trim() } else { '' }
        if (-not $label -or $label.Length -gt 24 -or $key -notmatch '^[Ff]([1-9]|1[0-2])$') { throw "key$k must look like  Label|F1  with a key from F1 to F12" }
        if ($used.ContainsKey($key.ToUpperInvariant())) { throw "key$k repeats a key that is already used" }
        $used[$key.ToUpperInvariant()] = $true
        $keys += [PSCustomObject]@{ Label = $label; Key = $key.ToUpperInvariant() }
    }
    $shim = @((Field 'shim_files') -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    foreach ($dll in $shim) { if ($dll -inotmatch '^xinput[A-Za-z0-9_.]*\.dll$') { throw 'shim_files may only name xinput*.dll files' } }
    # How the VR controllers reach the game: through a shim in its folder (the default), or as a virtual Xbox controller that
    # Windows itself shows to every game. Same rules as src\catalog.hpp.
    $gamepad = Field 'gamepad' $(if ($shim.Count) { 'shim' } else { 'virtual' })
    if ($gamepad -cnotin @('shim', 'virtual')) { throw 'gamepad must be shim or virtual' }
    if (($gamepad -ceq 'virtual') -and $shim.Count) { throw 'gamepad=virtual does not use shim_files: a shim in the game folder would hide the virtual controller from the game' }
    foreach ($name in 'shim_manifest', 'fix_manifest', 'fix_dir') { if ((Field $name) -and -not (Test-FtdPlainName (Field $name))) { throw "$name must be a plain file or folder name" } }
    $url = Field 'fix_url'; $sha = ''; $archive = ''
    if ($url) {
        if ($url -notmatch '^https://' -or $url -match '[\s"''<>`]') { throw 'fix_url must be an https:// address' }
        $sha = Field 'fix_sha256'
        if ($sha -notmatch '^[0-9A-Fa-f]{64}$') { throw 'fix_url needs fix_sha256, the SHA256 of the download (64 hex digits), so only the inspected file is ever installed' }
        $archive = Field 'fix_archive' (Get-FtdUrlFileName $url)
        foreach ($name in 'fix_inner', 'fix_marker') { if ((Field $name) -and -not (Test-FtdPlainName (Field $name) '_.-+')) { throw "$name must be a plain file name" } }
        if (-not (Test-FtdPlainName $archive '_.-+')) { throw 'fix_archive must be a plain file name (the address does not end in one, so name it)' }
    }
    # An optional newer Geo-11 driver, laid over the fix: some of the fix's own files (its d3d11.dll) are too old for a game
    # that has been updated since, so a few files are taken from a second, pinned download instead.
    $driverUrl = Field 'driver_url'; $driverSha = ''; $driverArchive = ''; $driverFiles = @()
    if (-not $driverUrl) {
        foreach ($name in 'driver_sha256', 'driver_archive', 'driver_root', 'driver_files') { if (Field $name) { throw "$name needs driver_url" } }
    } else {
        if (-not $url) { throw 'driver_url replaces files of a stereo fix, so the entry needs fix_url too' }
        if ($driverUrl -notmatch '^https://' -or $driverUrl -match '[\s"''<>`]') { throw 'driver_url must be an https:// address' }
        $driverSha = Field 'driver_sha256'
        if ($driverSha -notmatch '^[0-9A-Fa-f]{64}$') { throw 'driver_url needs driver_sha256, the SHA256 of the download (64 hex digits), so only the inspected file is ever installed' }
        $driverArchive = Field 'driver_archive' (Get-FtdUrlFileName $driverUrl)
        if (-not (Test-FtdPlainName $driverArchive '_.-+')) { throw 'driver_archive must be a plain file name (the address does not end in one, so name it)' }
        if ((Field 'driver_root') -and -not (Test-FtdPlainName (Field 'driver_root'))) { throw 'driver_root must be the name of one folder inside the driver download, with no slashes' }
        $driverFiles = @((Field 'driver_files') -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        if (-not $driverFiles.Count) { throw 'driver_url needs driver_files, the names of the files to take from the download' }
        foreach ($f in $driverFiles) { if (-not (Test-FtdPlainName $f '_.-+')) { throw 'driver_files must be plain file names' } }
    }
    $subtitle = Field 'subtitle'
    $machine = switch ($machineText) { 'x86' { 0x14c } 'x64' { 0x8664 } default { $null } }
    @{
        Id = $id; Title = $(if ($subtitle) { "${title}: $subtitle" } else { $title }); Name = $title; Subtitle = $subtitle; Source = $Source
        AppId = $app; Folder = (Field 'folder'); Exe = $exe; ExeDir = $exeDir; FixRoot = $fixRoot; Process = [IO.Path]::GetFileNameWithoutExtension($exe); Machine = $machine
        Profile = $profile; Keys = $keys
        HasFix = [bool]$url; FixUrl = $url; FixArchive = $archive; FixSha256 = $sha.ToUpperInvariant(); FixAuthor = (Field 'fix_author' 'its author')
        FixDir = (Field 'fix_dir' "$id-fix"); FixManifest = (Field 'fix_manifest' "geo11-install-$id.json")
        FixInner = (Field 'fix_inner'); FixMarker = (Field 'fix_marker' 'd3dx.ini')
        HasDriver = [bool]$driverUrl; DriverAuthor = (Field 'driver_author' 'its author'); DriverUrl = $driverUrl; DriverArchive = $driverArchive; DriverSha256 = $driverSha.ToUpperInvariant(); DriverRoot = (Field 'driver_root'); DriverFiles = $driverFiles
        ShimBuild = $(if ($machineText -eq 'x86') { Join-Path $FtdBin 'gamepad' } else { Join-Path $FtdBin 'gamepad64' }); ShimFiles = $shim; VirtualPad = ($gamepad -ceq 'virtual')
        ShimManifest = (Field 'shim_manifest' "gamepad-install-$id.json")
    }
}
# Is the ViGEmBus driver (the virtual gamepad bus) installed? Games with gamepad=virtual need it. It registers a device
# interface with a fixed GUID, which makes this check independent of the language Windows is set to.
function Test-FtdVirtualGamepadDriver {
    Test-Path -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceClasses\{96e42b22-f5e9-42f8-b043-ed0f932f014f}'
}
# The catalog and the user's file, merged: a user section with a catalog section's name replaces it in place.
function Get-FtdCatalog {
    $games = [ordered]@{}
    $FtdCatalogProblems.Clear()
    $catalogFile = Join-Path $FtdCatalogDir 'games.catalog.ini'
    if (-not (Test-Path -LiteralPath $catalogFile)) { $FtdCatalogProblems.Add("games.catalog.ini was not found in $FtdCatalogDir; only your games.user.ini entries are available") }
    foreach ($source in @(@{ File = $catalogFile; Name = 'catalog' }, @{ File = (Join-Path $FtdCatalogDir 'games.user.ini'); Name = 'user' })) {
        $seen = @{}
        foreach ($section in @(Read-FtdIni $source.File)) {
            try { $game = ConvertTo-FtdGame $section $source.Name }
            catch { $FtdCatalogProblems.Add("$($source.Name) games file: [$($section.Name)] skipped: $($_.Exception.Message)"); continue }
            if ($seen.ContainsKey($game.Id)) { $FtdCatalogProblems.Add("$($source.Name) games file: [$($game.Id)] appears twice; the later one is used") }
            $seen[$game.Id] = $true
            $games[$game.Id] = $game
        }
    }
    $list = @($games.Values)
    for ($i = 0; $i -lt $list.Count; $i++) { for ($j = $i + 1; $j -lt $list.Count; $j++) {
        if ($list[$i].AppId -eq $list[$j].AppId) { $FtdCatalogProblems.Add("games [$($list[$i].Id)] and [$($list[$j].Id)] have the same steam_app_id") }
    } }
    return $games
}
$FtdGames = Get-FtdCatalog
function Get-FtdGame([string]$Id) {
    if (-not $FtdGames.Contains($Id)) { throw "Unknown game '$Id'. Known: $($FtdGames.Keys -join ', ')." }
    $FtdGames[$Id]
}
# Every Steam library folder on this PC (empty if Steam is not installed).
function Get-FtdSteamLibraries {
    # Tests point this at fake libraries; normally Steam's own registry entry is used.
    $forced = $env:FLATTODEPTH_STEAM_LIBRARIES
    if ($forced) { return @($forced -split ';' | Where-Object { $_ }) }
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { return @() }
    $steam = $steam -replace '/', '\'
    $libraries = @($steam)
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        $libraries += [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]*)"') | ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' }
    }
    $libraries | Group-Object { $_.ToLowerInvariant().TrimEnd('\') } | ForEach-Object { $_.Group[0] }   # drop case-only duplicates
}
# Where the game's program is, and so where its controller shim and stereo fix files must go: the game's folder, or the
# one folder inside it that the catalog entry names (exe_dir=, for a game that keeps its program in a subfolder).
function Get-FtdInstallDirectory($Game, [string]$GameDirectory) {
    if ($Game.ExeDir) { Join-Path $GameDirectory $Game.ExeDir } else { $GameDirectory }
}
# The game's folder: an explicit path, or found through Steam. Steam's own record (appmanifest_<id>.acf) says which
# library really has the game, so a stale leftover folder in another library is never picked by mistake.
function Resolve-FtdGameDirectory($Game, [string]$Override) {
    if ($Override) { return (Resolve-Path -LiteralPath $Override).Path.TrimEnd('\') }
    $libraries = @(Get-FtdSteamLibraries)
    foreach ($library in $libraries) {
        $record = Join-Path $library "steamapps\appmanifest_$($Game.AppId).acf"
        if (-not (Test-Path -LiteralPath $record)) { continue }
        $name = [regex]::Match((Get-Content -LiteralPath $record -Raw), '"installdir"\s+"([^"]*)"').Groups[1].Value
        if (-not $name) { $name = $Game.Folder }
        $candidate = Join-Path $library "steamapps\common\$name"
        if (Test-Path -LiteralPath (Join-Path (Get-FtdInstallDirectory $Game $candidate) $Game.Exe)) { return $candidate.TrimEnd('\') }
    }
    foreach ($library in $libraries) {    # no Steam record (unusual): fall back to the usual folder name
        $candidate = Join-Path $library "steamapps\common\$($Game.Folder)"
        if (Test-Path -LiteralPath (Join-Path (Get-FtdInstallDirectory $Game $candidate) $Game.Exe)) { return $candidate.TrimEnd('\') }
    }
    throw "Could not find $($Game.Title) in any Steam library; pass -GameDirectory."
}
# Steam can move a game to another library. The files we added move with it, so only the install record needs
# pointing at the new folder. Returns true if it changed the record.
function Sync-FtdRecordDirectory([string]$RecordPath, [string]$GameDirectory) {
    if (-not (Test-Path -LiteralPath $RecordPath)) { return $false }
    $record = Get-Content -LiteralPath $RecordPath -Raw | ConvertFrom-Json
    $moved = $false
    if ($record.PSObject.Properties.Name -contains 'Files') {
        $files = @($record.Files)
        $recorded = [IO.Path]::GetFullPath($record.GameDirectory).TrimEnd('\')
        if (-not [string]::Equals($recorded, $GameDirectory.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase) -and $files.Count -and
            (Test-Path -LiteralPath (Join-Path $GameDirectory $files[0].Relative))) {
            $record.GameDirectory = $GameDirectory; $record.Files = $files; $moved = $true
        }
    } elseif ($record.PSObject.Properties.Name -contains 'Target') {      # first-version shim record: one full path
        $now = Join-Path $GameDirectory ([IO.Path]::GetFileName($record.Target))
        if (-not (Test-Path -LiteralPath $record.Target) -and (Test-Path -LiteralPath $now)) { $record.Target = $now; $moved = $true }
    }
    if ($moved) { $record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $RecordPath }
    return $moved
}
# 0x14c = 32-bit, 0x8664 = 64-bit.
function Get-PeMachine([string]$Path) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try { $reader.BaseStream.Position = 0x3c; $pe = $reader.ReadInt32(); $reader.BaseStream.Position = $pe + 4; $reader.ReadUInt16() }
    finally { $reader.Dispose() }
}
# Unpacks a .7z. Windows' own tar.exe reads them on current Windows 10 and 11; 7-Zip is the fallback for older builds.
function Expand-FtdArchive([string]$Archive, [string]$Destination) {
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (Test-Path -LiteralPath $tar) {
        try { & $tar -xf $Archive -C $Destination 2>$null; if ($LASTEXITCODE -eq 0) { return } } catch { }
    }
    foreach ($seven in @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe")) {
        if (Test-Path -LiteralPath $seven) {
            & $seven x -y "-o$Destination" $Archive | Out-Null
            if ($LASTEXITCODE -eq 0) { return }
        }
    }
    throw "Could not unpack $Archive. Windows' tar.exe could not read it; install 7-Zip (https://www.7-zip.org) and run this again."
}
