# Tests for the games catalog as the install scripts read it. Runs on Windows PowerShell 5.1 and PowerShell 7, on any OS
# (it touches nothing real):  powershell -NoProfile -File tests\catalog_test.ps1
# The validation cases are shared with tests\catalog_test.cpp (tests\catalog_cases.txt), so the menu and the installers
# cannot disagree about what a valid entry is.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $root 'scripts\games.ps1')
$script:failures = 0
function Check([bool]$condition, [string]$message) {
    if ($condition) { Write-Host "ok    $message" } else { Write-Host "FAIL  $message" -ForegroundColor Red; $script:failures++ }
}
$temp = Join-Path ([IO.Path]::GetTempPath()) ('flattodepth-catalog-test-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $temp | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Text([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, $utf8) }
function Convert-Case([string]$Text) {
    $Text.Replace('<NL>', "`n").Replace('{sha}', ('a' * 64)).Replace('{SHA}', ('A' * 64)).Replace('{z64}', ('z' * 64)).Replace('{a32}', ('a' * 32)).Replace('{a33}', ('a' * 33)).Replace('{x24}', ('x' * 24)).Replace('{x25}', ('x' * 25))
}
function Try-Game([string]$Text) {     # the first section of some INI text as a game, or the reason it is refused
    $file = Join-Path $temp 'case.ini'; Write-Text $file $Text
    $sections = @(Read-FtdIni $file)
    if (-not $sections.Count) { return [PSCustomObject]@{ Game = $null; Why = 'no section' } }
    try { [PSCustomObject]@{ Game = (ConvertTo-FtdGame $sections[0] 'test'); Why = '' } } catch { [PSCustomObject]@{ Game = $null; Why = $_.Exception.Message } }
}
try {
    # --- Plain INI --------------------------------------------------------------------------------------------------
    $ini = Join-Path $temp 'plain.ini'
    Write-Text $ini ([string][char]0xFEFF + "; comment`r`n# another`r`nstray=1`r`n[ First ]`r`nKey = Value with spaces `r`nkey=last wins`r`n`r`nUrl=https://x.example/a?b=c`r`n[second]`r`nempty=`r`nnoequals`r`n")
    $s = @(Read-FtdIni $ini)
    Check (($s.Count -eq 2) -and ($s[0].Name -eq 'First') -and ($s[1].Name -eq 'second')) 'sections are read in order, names trimmed; keys before any section are ignored'
    Check (($s[0].Values['key'] -eq 'last wins') -and ($s[0].Values['url'] -eq 'https://x.example/a?b=c')) 'keys are case-insensitive, the last wins, only the first = splits a line'
    Check (($s[1].Values.ContainsKey('empty')) -and ($s[1].Values['empty'] -eq '') -and -not $s[1].Values.ContainsKey('noequals')) 'empty values are kept, lines without = are skipped'
    Check ((@(Read-FtdIni (Join-Path $temp 'nothing.ini'))).Count -eq 0) 'a missing file has no sections'
    Write-Text $ini 'key=value'; Check ((@(Read-FtdIni $ini)).Count -eq 0) 'text with no section header has no sections'
    Check ((Get-FtdUrlFileName 'https://example.org/a/b/Fix_1.7z') -eq 'Fix_1.7z' -and (Get-FtdUrlFileName 'https://example.org/Fix.7z?x=1#y') -eq 'Fix.7z' -and (Get-FtdUrlFileName 'https://example.org/a%20b.7z') -eq 'a b.7z' -and
        (Get-FtdUrlFileName 'https://example.org/dir/') -eq '' -and (Get-FtdUrlFileName 'https://example.org') -eq '' -and (Get-FtdUrlFileName 'https://example.org/a%zz.7z') -eq 'a%zz.7z' -and (Get-FtdUrlFileName 'https://example.org/ends%') -eq 'ends%') 'file names from download addresses'

    # --- The shared validation cases ----------------------------------------------------------------------------------
    $cases = Join-Path $PSScriptRoot 'catalog_cases.txt'
    Check (Test-Path -LiteralPath $cases) 'the shared cases file is there'
    $good = 0; $bad = 0
    foreach ($line in ([IO.File]::ReadAllLines($cases, $utf8))) {
        if (-not $line -or $line.StartsWith('#')) { continue }
        $f = $line.Split("`t")
        if ($f.Count -ne 4) { Check $false "a case has four tab-separated fields: $($line.Substring(0, [Math]::Min(40, $line.Length)))"; continue }
        $r = Try-Game (Convert-Case $f[3])
        if ($f[0] -eq 'good') { $good++; Check (($r.Why -eq '') -and $r.Game) "accepted: $($f[1]) (got: $($r.Why))" }
        else { $bad++; Check (($r.Why -ne '') -and $r.Why.Contains($f[2])) "refused: $($f[1]) because $($f[2]) (got: $($r.Why))" }
    }
    Check (($good -ge 10) -and ($bad -ge 50)) "the shared cases were read ($good good, $bad bad)"

    # --- What a good entry becomes ------------------------------------------------------------------------------------
    $r = Try-Game "[hollow-knight]`ntitle=Hollow Knight`nsteam_app_id=367520`nexe=hollow_knight.exe`n"
    $g = $r.Game
    Check (($g.Id -eq 'hollow-knight') -and ($g.Title -eq 'Hollow Knight') -and ($g.AppId -eq 367520) -and ($g.Exe -eq 'hollow_knight.exe') -and ($g.Process -eq 'hollow_knight')) 'the smallest entry: id, title, app, program and process name'
    Check (($g.Profile -eq 'flattodepth-hollow-knight.ini') -and ($g.Keys.Count -eq 0) -and -not $g.HasFix -and ($null -eq $g.Machine) -and ($g.ShimFiles.Count -eq 0)) 'everything else has a sensible default'
    Check (($g.FixDir -eq 'hollow-knight-fix') -and ($g.FixManifest -eq 'geo11-install-hollow-knight.json') -and ($g.ShimManifest -eq 'gamepad-install-hollow-knight.json') -and ($g.FixMarker -eq 'd3dx.ini') -and ($g.FixInner -eq '')) 'install records and folders are named after the game'
    $g = (Try-Game "[a]`ntitle=Ori`nsubtitle=Definitive Edition`nsteam_app_id=1`nexe=a.exe`nmachine=x64`nkey1=Convergence|F1`nkey2=HUD | f12`nshim_files=xinput1_4.dll, XInput1_3.dll`nfix_url=https://example.org/dir/Some%2DFix+1.7z?x=1`nfix_sha256=$('ab' * 32)`nfix_author=Somebody`n").Game
    Check (($g.Title -eq 'Ori: Definitive Edition') -and ($g.Machine -eq 0x8664) -and ($g.Keys.Count -eq 2) -and ($g.Keys[0].Label -eq 'Convergence') -and ($g.Keys[1].Key -eq 'F12') -and (($g.ShimFiles -join ',') -eq 'xinput1_4.dll,XInput1_3.dll')) 'optional fields: the menu title joins the subtitle, bitness, shortcuts and shims'
    Check ($g.HasFix -and ($g.FixArchive -eq 'Some-Fix+1.7z') -and ($g.FixSha256 -ceq ('AB' * 32)) -and ($g.FixAuthor -eq 'Somebody') -and ((Split-Path $g.ShimBuild -Leaf) -eq 'gamepad64')) 'the download: its file name comes from the address, the hash is upper-cased for comparison, 64-bit games use the 64-bit shim'
    Check ((Split-Path (Try-Game "[a]`ntitle=T`nsteam_app_id=1`nexe=a.exe`nmachine=x86`n").Game.ShimBuild -Leaf) -eq 'gamepad') '32-bit games use the 32-bit shim'
    Check ((Try-Game "[a]`ntitle=T`nsteam_app_id=1`nexe=a.exe`n").Game.FixAuthor -eq 'its author') 'without a named author the licence prompt says "its author"'

    # --- Merging the shipped catalog and the user's file ----------------------------------------------------------------
    $dir = Join-Path $temp 'merge'; New-Item -ItemType Directory -Path $dir | Out-Null
    Write-Text (Join-Path $dir 'games.catalog.ini') "[one]`ntitle=One`nsteam_app_id=1`nexe=one.exe`n[two]`ntitle=Two`nsteam_app_id=2`nexe=two.exe`n[three]`ntitle=Three`nsteam_app_id=3`nexe=three.exe`n"
    Write-Text (Join-Path $dir 'games.user.ini') "[two]`ntitle=Two, mine`nsteam_app_id=2`nexe=two2.exe`n[extra]`ntitle=Extra`nsteam_app_id=9`nexe=extra.exe`n[broken]`ntitle=No app`nexe=b.exe`n"
    $FtdCatalogDir = $dir
    $merged = Get-FtdCatalog
    Check ((@($merged.Keys) -join ',') -eq 'one,two,three,extra') 'a user section replaces the catalog one in place; new ones go last'
    Check (($merged['two'].Title -eq 'Two, mine') -and ($merged['two'].Source -eq 'user') -and ($merged['one'].Source -eq 'catalog')) 'and each says where it came from'
    Check (($FtdCatalogProblems.Count -eq 1) -and ($FtdCatalogProblems[0] -like '*[[]broken[]] skipped*')) 'a broken user entry is skipped and reported, nothing else is'
    Write-Text (Join-Path $dir 'games.user.ini') "[a]`ntitle=A`nsteam_app_id=7`nexe=a.exe`n[a]`ntitle=A again`nsteam_app_id=7`nexe=a.exe`n[b]`ntitle=B`nsteam_app_id=7`nexe=b.exe`n"
    $merged = Get-FtdCatalog
    Check (($merged['a'].Title -eq 'A again') -and (($FtdCatalogProblems -join '|') -like '*appears twice*') -and (($FtdCatalogProblems -join '|') -like '*same steam_app_id*')) 'a repeated section and two games for one Steam app are reported'
    Remove-Item -LiteralPath (Join-Path $dir 'games.catalog.ini'); Remove-Item -LiteralPath (Join-Path $dir 'games.user.ini')
    $merged = Get-FtdCatalog
    Check (($merged.Count -eq 0) -and (($FtdCatalogProblems -join '|') -like '*games.catalog.ini was not found*')) 'no catalog file is reported'
    $threw = $false; try { [void](Get-FtdGame 'nope') } catch { $threw = $_.Exception.Message -like "*Unknown game 'nope'*" }
    Check $threw 'asking for a game that is not there says so'

    # --- The catalog FlatToDepth ships ----------------------------------------------------------------------------------------
    $FtdCatalogDir = $root
    $FtdGames = Get-FtdCatalog
    foreach ($problem in $FtdCatalogProblems) { Write-Host "catalog problem: $problem" }
    Check ($FtdCatalogProblems.Count -eq 0) 'the shipped catalog has no problems'
    Check ((@($FtdGames.Keys)[0] -eq 'blindforest') -and (@($FtdGames.Keys)[1] -eq 'wotw')) 'the first two games come first'
    $bf = $FtdGames['blindforest']; $wotw = $FtdGames['wotw']
    Check (($bf.AppId -eq 387290) -and ($bf.Exe -eq 'oriDE.exe') -and ($bf.Profile -eq 'flattodepth-blindforest.ini') -and ($bf.Machine -eq 0x14c) -and $bf.HasFix -and ($bf.Title -eq 'Ori and the Blind Forest: Definitive Edition')) 'Blind Forest'
    Check (($wotw.AppId -eq 1057090) -and ($wotw.Exe -eq 'oriwotw.exe') -and ($wotw.Profile -eq 'flattodepth-wotw.ini') -and ($wotw.Machine -eq 0x8664) -and $wotw.HasFix) 'Will of the Wisps'
    Check (($bf.VirtualPad -and ($bf.ShimFiles.Count -eq 0) -and $wotw.VirtualPad -and ($wotw.ShimFiles.Count -eq 0)) -and ($bf.ShimManifest -eq 'gamepad-install.json') -and ($bf.FixManifest -eq 'geo11-install.json')) 'the Ori games get the virtual pad; their old shim install record keeps its name so the installer can remove it'
    Check (($bf.FixSha256 -ceq '05AC228130069D497CCF6FA5F529F74D8C55D1D5377407E5CBCEAA09E2D7F1EC') -and ($wotw.FixSha256 -ceq 'BA31AAD3CF9828F95CC5B7B1522DF60D51B5C86184B577D7B0691754B819EEC4')) 'the pinned fix hashes are unchanged'
    Check (($bf.Keys.Count -eq 5) -and ($bf.Keys[0].Key -eq 'F1') -and ($wotw.Keys.Count -eq 6) -and ($wotw.Keys[5].Label -eq 'Bloom')) 'the Geo-11 shortcuts'
    $hk = $FtdGames['hollow-knight']
    Check (($null -ne $hk) -and ($hk.AppId -eq 367520) -and ($hk.Exe -eq 'hollow_knight.exe') -and ($hk.Machine -eq 0x8664) -and $hk.HasFix -and ($hk.Keys.Count -eq 5) -and ($hk.Keys[1].Label -eq 'Depth of field') -and ($hk.Keys[4].Key -eq 'F5') -and
        ($hk.FixSha256 -ceq '7ADF0E49C328D46118A3415300CA93C67AA473C63D2ECACCE76987CB8704C3DF') -and ($hk.FixInner -eq 'FixFiles.7z') -and $hk.VirtualPad -and ($hk.ShimFiles.Count -eq 0)) 'Hollow Knight'
    Check ($hk.HasDriver -and ($hk.DriverSha256 -ceq '5F9C75963BF5D28A97916D829FB7080709431A24700870CF778354DBE1C0A350') -and ($hk.DriverRoot -eq 'x64') -and (($hk.DriverFiles -join ',') -eq 'd3d11.dll,nvapi64.dll') -and
        -not $FtdGames['blindforest'].HasDriver -and -not $FtdGames['wotw'].HasDriver) 'Hollow Knight takes d3d11.dll and nvapi64.dll from the current Geo-11 driver; the other games use their fix as it is'
    Check (-not $FtdGames['blindforest'].ExeDir -and -not $FtdGames['wotw'].ExeDir -and -not $hk.ExeDir) 'games with their program in the top folder have no exe_dir'
    # A game that keeps its program in a subfolder, and a fix that unpacks into one.
    $sub = (Try-Game "[split]`ntitle=Split`nsteam_app_id=2000`nexe=Split.exe`nexe_dir=x64`nmachine=x64`nfix_url=https://example.org/Split+Fix.7z`nfix_sha256=$('AB' * 32)`nfix_root=x64`n")
    Check (($sub.Why -eq '') -and ($sub.Game.ExeDir -eq 'x64') -and ($sub.Game.FixRoot -eq 'x64') -and $sub.Game.HasFix -and -not $sub.Game.HasDriver) 'exe_dir and fix_root are read'
    Check ((@($FtdGames.Values | ForEach-Object { $_.AppId } | Select-Object -Unique)).Count -eq $FtdGames.Count) 'Steam apps are unique'
    # The example entry in docs\GAMES.md is what people will copy, so it has to load.
    $guide = [IO.File]::ReadAllText((Join-Path $root 'docs\GAMES.md'), $utf8)
    $from = $guide.IndexOf("``````ini`n")
    if ($from -lt 0) { $from = $guide.IndexOf("``````ini`r`n") }
    $to = if ($from -ge 0) { $guide.IndexOf('```', $from + 6) } else { -1 }
    $r = if (($from -ge 0) -and ($to -gt $from)) { Try-Game ($guide.Substring($from + 6, $to - $from - 6).TrimStart("ini`r`n".ToCharArray())) } else { $null }
    Check ($r -and ($r.Why -eq '') -and ($r.Game.Id -eq 'example-game') -and ($r.Game.AppId -eq 999001) -and ($r.Game.Keys.Count -eq 2) -and -not $r.Game.HasFix) "the guide's example entry is valid ($(if ($r) { $r.Why } else { 'not found' }))"
    Check ((Test-Path -LiteralPath (Join-Path $root 'flattodepth.default.ini')) -and (Test-Path -LiteralPath (Join-Path $root 'docs\GAMES.md'))) 'the generic defaults and the guide ship with FlatToDepth'
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:failures) { Write-Host "$script:failures catalog check(s) failed" -ForegroundColor Red; exit 1 }
Write-Host 'PASS games catalog (PowerShell): INI reader, shared validation cases, entries, merging, the shipped catalog'
