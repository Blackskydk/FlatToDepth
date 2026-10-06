# Games: what FlatToDepth can show, and how to add more

FlatToDepth turns a 2D game into a big stereoscopic 3D screen in your headset. The 3D comes from a **Geo-11 stereo fix**, a small set of files made for one particular game that makes it draw once per eye. FlatToDepth itself is not tied to any one game: it takes whatever picture a fixed game exports, shows it in VR, and plays the game with your VR controllers. It ships knowing two games (the two Ori games), and a **catalog** says which others it can handle.

This page covers what is in the catalog, how to see which of *your* Steam games might be worth trying, and how to add a game.

## Which games work

A game works with FlatToDepth when all of these are true:

1. It draws with **DirectX 11**. That is what the Geo-11 fixes work on. DirectX 9, DirectX 12, Vulkan and OpenGL games do not.
2. **A Geo-11 stereo fix exists for that game.** Fixes are made by hand, one game at a time (the [HelixMod blog](https://helixmod.blogspot.com) and the communities around it collect them), because each game's shaders need their own corrections. A game with no fix draws wrongly in 3D, or not at all.
3. Its controller input can be passed on. FlatToDepth reads your VR controllers and presents them to the game as an ordinary gamepad, through a small replacement for the game's XInput DLL. That covers **64-bit games** (`xinput1_4.dll` or `xinput1_3.dll`) and **32-bit games that use `xinput9_1_0.dll`**. A 32-bit game that loads `xinput1_3.dll` or `xinput1_4.dll` is not covered yet: the game would show in 3D but its controller would not work. The scan below tells you which DLL a game loads. Games that need a keyboard and mouse, or read the controller some other way, are not helped by this.
4. It is installed through **Steam**: FlatToDepth finds games in your Steam libraries and starts them through Steam.

The **catalog** (`games.catalog.ini`) lists the games where someone has checked all of that. Right now it holds two games, *Ori and the Blind Forest: Definitive Edition* and *Ori and the Will of the Wisps*, the ones it was built with. It is meant to grow, see [Adding to the catalog](#adding-to-the-catalog-for-everyone).

## See what is on your PC

Run the scan from the FlatToDepth folder (no headset needed):

```
bin\FlatToDepth.exe --scan
```

(In a source checkout it is `build\FlatToDepth.exe`.) It looks at every game Steam says is installed, reads each game's program files without running them, and prints something like:

```
FlatToDepth game scan: 41 installed Steam games: 2 supported, 3 candidates, 36 other

STATUS     APP      GAME                                    BITS  GRAPHICS          CONTROLLER
supported  387290   Ori and the Blind Forest: Definitive Ed x86   DirectX 11        xinput9_1_0
supported  1057090  Ori and the Will of the Wisps           x64   DirectX 11        xinput1_4
candidate  999001   Example Game                            x64   DirectX 11        xinput1_3
...
```

| Status | Meaning |
| --- | --- |
| **supported** | It is in the catalog (or your `games.user.ini`). It appears in the in-headset menu. |
| **candidate** | It draws with DirectX 11, so a Geo-11 fix *could* work. Nobody has made it an entry yet. Whether a fix exists for it is something only you or the community can find out. |
| **unlikely** | Its program does not use DirectX 11 (DirectX 9, DirectX 12, Vulkan, OpenGL...). Shown with `--scan --all`. |
| **unknown** | No program was found, or it is not one FlatToDepth can read. Shown with `--scan --all`. |

The scan is a guide, not a promise. It looks at what a program imports and mentions, so a game that loads DirectX 11 in an unusual way can be missed, and a game that merely *mentions* DirectX 11 (many engines keep it as a fallback) can show as a candidate without using it.

## Add a game to your own list

Everything you add goes in **`games.user.ini`**, next to `flattodepth.ini` in the FlatToDepth folder. FlatToDepth never overwrites it, and updates replace `games.catalog.ini` but not this file. If a section in your file has the same name as one in the catalog, yours replaces it.

1. **Get a draft.** Run `bin\FlatToDepth.exe --scan --draft <steam app number>` (the number is in the scan's APP column). It prints an entry with the title, program, folder, bitness and the right shim filled in. Paste it into `games.user.ini`.
2. **Get the stereo fix** for that game, one of two ways:
   - *You install it yourself* (leave the `fix_*` lines out). Follow the fix's own instructions. Afterwards its `d3dxdm.ini` needs `direct_mode = katanga_vr`, and `force_stereo=2` must be set in its `d3dx.ini`. FlatToDepth still does the menu, the controller and the settings.
   - *FlatToDepth downloads it for you.* Add `fix_url`, `fix_sha256` and optionally `fix_archive`, `fix_inner`, `fix_marker` and `fix_author`; see [the fields](#the-fields). `fix_sha256` is required: it pins exactly the file you inspected, so nothing else is ever installed. Fixes are licensed for personal use and are never redistributed by FlatToDepth.
3. **Install the controller shim (and the fix, if you gave a `fix_url`):** double-click `Install.cmd`, or run `Install.cmd -Games <id>` with the section name you chose. It needs `machine` (`x86` or `x64`) and `shim_files` in the entry. Close the game first.
4. **Start FlatToDepth** (`Start-FlatToDepth.cmd`). The game is in the menu. Choose it. If depth looks inside-out use **Swap eyes** in the [tools panel](USAGE.md#the-tools-panel); if the picture is cut off or has bars, set `crop_aspect` in the game's settings file (below).

The game's settings (screen size and place, eye order, curve, glow and so on) live in **`flattodepth-<id>.ini`**, created from `flattodepth.default.ini` the first time the game is shown.

If something in `games.user.ini` is wrong, the game is skipped, the in-headset menu says so, and `logs\flattodepth.log` says which entry and why. One bad entry never stops the others.

### The fields

A section is one game. The name in `[brackets]` is its id: 1 to 32 lower-case letters, digits, `-` or `_`. Lines starting with `;` or `#` are comments; there are no comments after a value on the same line.

| Field | |
| --- | --- |
| `title` | Shown in the menu. **Required.** |
| `subtitle` | A second line on the menu tile, such as `Definitive Edition`. |
| `steam_app_id` | The game's Steam app number. **Required.** |
| `exe` | The game's program, a name with no folder, like `game.exe`. **Required.** |
| `folder` | The game's usual folder name in `steamapps\common`, used only if Steam has no record of it. |
| `machine` | `x86` or `x64`: whether the program is 32-bit or 64-bit. Needed to install the controller shim. |
| `shim_files` | The controller shim names to install in the game folder, such as `xinput1_4.dll,xinput1_3.dll`. Only `xinput*.dll` names. 64-bit: `xinput1_4.dll`, `xinput1_3.dll`. 32-bit: `xinput9_1_0.dll`. |
| `profile` | The game's settings file name. Default `flattodepth-<id>.ini`. |
| `key1` ... `key6` | Stereo-fix shortcuts for the tools panel, as `Label\|F1`. The panel presses that key in the game for you. Which keys do what depends on the fix: its readme or `d3dxdm.ini` lists them (convergence, HUD depth and so on). Only F1 to F12. |
| `fix_url` | An `https://` address to download the fix from. Optional; without it you install the fix yourself. |
| `fix_sha256` | The SHA256 of that download, 64 hex digits. **Required with `fix_url`.** `Get-FileHash <file>` in PowerShell prints it. |
| `fix_archive` | What to call the download on disk. Default: the file name at the end of the address. |
| `fix_inner` | If the download wraps its files in a second archive, that archive's name. |
| `fix_marker` | A file that shows the fix has been unpacked. Default `d3dx.ini`. |
| `fix_author` | Who made it, shown when you are asked to accept its license. |
| `fix_dir`, `fix_manifest`, `shim_manifest` | Names of FlatToDepth's working folder and install records for this game. Leave them out unless an install already exists under other names. |

A complete entry for a game whose fix you install yourself:

```ini
[example-game]
title=Example Game
steam_app_id=999001
exe=example.exe
machine=x64
shim_files=xinput1_4.dll,xinput1_3.dll
key1=Convergence|F1
key2=HUD depth|F2
```

## Adding to the catalog, for everyone

`games.catalog.ini` is what ships with FlatToDepth, and it is the way a game becomes available to everybody. To propose one, open a pull request that adds a section. The bar is deliberately higher than for your own file, because other people's computers will download what it points to:

- **A real, working fix exists** for that exact game, and you have run it through FlatToDepth on a headset: the picture is in stereo with correct depth, the controller plays the game, and the game starts and returns to the menu properly. Say in the pull request what you tested it on.
- **`fix_url` is the fix author's own page** (https), not a mirror, and the entry has the **`fix_sha256` of the package you inspected**. If the author later changes the file, the installer refuses it until someone checks the new one.
- **Nothing from the fix is copied into this repository.** The fixes are personal-use only and are not FlatToDepth's to redistribute. The entry holds an address, a hash and a name; nothing else.
- `machine` and `shim_files` are right for the game (the scan shows the DLL it loads), the shortcuts are the ones the fix really has, and `exe`, `steam_app_id` and `folder` match a clean Steam install.

Entries are checked by the project's tests (`tests/catalog_test.cpp`, `tests/catalog_test.ps1`): an entry that breaks the format, names a folder instead of a file, uses a non-https address or leaves out the hash does not load.

## How it fits together

- `games.catalog.ini` and `games.user.ini` are read by **both** `FlatToDepth.exe` (the menu, the tools panel, the settings) and the install scripts (`scripts\games.ps1`), with the same rules, so they cannot disagree about a game. The two readers share one set of test cases (`tests\catalog_cases.txt`).
- The in-headset menu lists the games Steam reports as installed. A game that is still downloading is not listed. If fewer than a page's worth are installed, the rest of the first page shows the games FlatToDepth supports that you do not have, greyed out. With more than two games the menu becomes a grid of six per page with **Previous** and **Next**.
- `FLATTODEPTH_STEAM_LIBRARIES` (Steam library folders separated by `;`) overrides where FlatToDepth looks for Steam games, if Steam keeps its libraries somewhere unusual.

## Limits worth knowing

- FlatToDepth cannot make a game 3D by itself. Without a fix made for it, a game in the list does not look right.
- The controller shim only exists for the XInput DLL names above. Other input paths (keyboard-only games, other controller APIs) are not covered.
- Only DirectX 11 games, one game at a time, and only on Windows with Steam.
- Everything here has so far been verified for the two Ori games only. Treat any other game as an experiment until someone has played it.
