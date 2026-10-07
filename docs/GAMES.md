# Games: what FlatToDepth can show, and how to add more

FlatToDepth turns a 2D game into a big stereoscopic 3D screen in your headset. The 3D comes from a **Geo-11 stereo fix**, a small set of files made for one particular game that makes it draw once per eye. FlatToDepth itself is not tied to any one game: it takes whatever picture a fixed game exports, shows it in VR, and plays the game with your VR controllers. It ships knowing two games (the two Ori games), and a **catalog** says which others it can handle.

This page covers what is in the catalog, how to see which of *your* Steam games might be worth trying, and how to add a game.

## Which games work

A game works with FlatToDepth when all of these are true:

1. It draws with **DirectX 11**. That is what the Geo-11 fixes work on. DirectX 9, DirectX 12, Vulkan and OpenGL games do not.
2. **A Geo-11 stereo fix exists for that game.** Fixes are made by hand, one game at a time (the [HelixMod blog](https://helixmod.blogspot.com) and the communities around it collect them), because each game's shaders need their own corrections. A game with no fix draws wrongly in 3D, or not at all.
3. Its controller input can be passed on. FlatToDepth reads your VR controllers and presents them to the game as an ordinary Xbox 360 gamepad: a virtual one that Windows itself shows to every game, through the free ViGEmBus driver (see [Controllers](#controllers-one-virtual-pad-for-every-game)). That works for any game, 32-bit or 64-bit, whichever way it reads its controllers (XInput, Windows.Gaming.Input, Rewired); it does not depend on the game at all. Games that need a keyboard and mouse are not helped by this.
4. It is installed through **Steam**: FlatToDepth finds games in your Steam libraries and starts them through Steam.

The **catalog** (`games.catalog.ini`) lists the games where someone has checked all of that. Right now it holds three games: *Ori and the Blind Forest: Definitive Edition* and *Ori and the Will of the Wisps*, the ones it was built with, and *Hollow Knight* (see [Hollow Knight](#hollow-knight) for what it needed). It is meant to grow, see [Adding to the catalog](#adding-to-the-catalog-for-everyone).

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

1. **Get a draft.** Run `bin\FlatToDepth.exe --scan --draft <steam app number>` (the number is in the scan's APP column). It prints an entry with the title, program, folder and bitness filled in. Paste it into `games.user.ini`.
2. **Get the stereo fix** for that game, one of two ways:
   - *You install it yourself* (leave the `fix_*` lines out). Follow the fix's own instructions. Afterwards its `d3dxdm.ini` needs `direct_mode = katanga_vr`, and `force_stereo=2` must be set in its `d3dx.ini`. FlatToDepth still does the menu, the controller and the settings.
   - *FlatToDepth downloads it for you.* Add `fix_url`, `fix_sha256` and optionally `fix_archive`, `fix_inner`, `fix_marker` and `fix_author`; see [the fields](#the-fields). `fix_sha256` is required: it pins exactly the file you inspected, so nothing else is ever installed. Fixes are licensed for personal use and are never redistributed by FlatToDepth.
3. **Install the stereo fix, if you gave a `fix_url`:** double-click `Install.cmd`, or run `Install.cmd -Games <id>` with the section name you chose. Close the game first. The controllers need nothing installed in the game's folder; `Install.cmd` only tells you whether the ViGEmBus driver is there.
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
| `exe_dir` | The one folder inside the game's folder that holds the program, for a game that does not keep it in the top folder (many games keep it in a folder such as `x64` or `bin`). The controller shim and the stereo fix are installed there. A name with no slashes. `FlatToDepth.exe --scan --draft` fills it in for you. |
| `folder` | The game's usual folder name in `steamapps\common`, used only if Steam has no record of it. |
| `machine` | `x86` or `x64`: whether the program is 32-bit or 64-bit. Only needed for an entry that installs a controller shim (see `shim_files`); the draft fills it in anyway. |
| `shim_files` | Optional, and rare: names of a replacement XInput DLL to install in the game folder instead of using the virtual pad, such as `xinput1_4.dll,xinput1_3.dll` (64-bit) or `xinput9_1_0.dll` (32-bit). Only `xinput*.dll` names. It needs no driver, but only reaches a game that calls that DLL, and gives the game no rumble from the virtual pad. An entry with `shim_files` is a shim entry. |
| `gamepad` | `virtual` (the default) or `shim`: how the VR controllers reach the game. `virtual` makes them a virtual Xbox controller in Windows through the ViGEmBus driver, which every game sees. `shim` installs the replacement XInput DLL named in `shim_files`. You rarely need to say it: an entry with no `shim_files` is virtual, one with `shim_files` is a shim. A virtual entry cannot have `shim_files` (a shim in the game folder would hide the virtual controller from the game). |
| `profile` | The game's settings file name. Default `flattodepth-<id>.ini`. |
| `key1` ... `key6` | Stereo-fix shortcuts for the tools panel, as `Label\|F1`. The panel presses that key in the game for you. Which keys do what depends on the fix: its readme or `d3dxdm.ini` lists them (convergence, HUD depth and so on). Only F1 to F12. |
| `fix_url` | An `https://` address to download the fix from. Optional; without it you install the fix yourself. |
| `fix_sha256` | The SHA256 of that download, 64 hex digits. **Required with `fix_url`.** `Get-FileHash <file>` in PowerShell prints it. |
| `fix_archive` | What to call the download on disk. Default: the file name at the end of the address. |
| `fix_inner` | If the download wraps its files in a second archive, that archive's name. |
| `fix_root` | If the download holds several builds side by side (a `x32` and an `x64` folder), the one folder whose files are installed. A name with no slashes. |
| `driver_url`, `driver_sha256` | Optional, only with a fix: a second, pinned download of the Geo-11 driver. A game that has been updated since its fix was made can need more of Windows than the fix's own `d3d11.dll` provides (a current Unity game will not even start), so the files named in `driver_files` are taken from this download instead. Hollow Knight does this. |
| `driver_files` | The files to take from that download, comma separated, such as `d3d11.dll,nvapi64.dll`. **Required with `driver_url`.** |
| `driver_root`, `driver_archive`, `driver_author` | The one folder inside the driver download to take them from (`x64`), the download's file name if its address does not end in one, and who made it (shown in the license question). |
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
key1=Convergence|F1
key2=HUD depth|F2
```

## Adding to the catalog, for everyone

`games.catalog.ini` is what ships with FlatToDepth, and it is the way a game becomes available to everybody. To propose one, open a pull request that adds a section. The bar is deliberately higher than for your own file, because other people's computers will download what it points to:

- **A real, working fix exists** for that exact game, and you have run it through FlatToDepth on a headset: the picture is in stereo with correct depth, the controller plays the game, and the game starts and returns to the menu properly. Say in the pull request what you tested it on.
- **`fix_url` is the fix author's own page** (https), not a mirror, and the entry has the **`fix_sha256` of the package you inspected**. If the author later changes the file, the installer refuses it until someone checks the new one.
- **Nothing from the fix is copied into this repository.** The fixes are personal-use only and are not FlatToDepth's to redistribute. The entry holds an address, a hash and a name; nothing else.
- `machine` is right for the game (the scan shows it), the shortcuts are the ones the fix really has, and `exe`, `steam_app_id` and `folder` match a clean Steam install.

Entries are checked by the project's tests (`tests/catalog_test.cpp`, `tests/catalog_test.ps1`): an entry that breaks the format, names a folder instead of a file, uses a non-https address or leaves out the hash does not load.

## How it fits together

- `games.catalog.ini` and `games.user.ini` are read by **both** `FlatToDepth.exe` (the menu, the tools panel, the settings) and the install scripts (`scripts\games.ps1`), with the same rules, so they cannot disagree about a game. The two readers share one set of test cases (`tests\catalog_cases.txt`).
- The in-headset menu lists the games Steam reports as installed. A game that is still downloading is not listed. If fewer than a page's worth are installed, the rest of the first page shows the games FlatToDepth supports that you do not have, greyed out. With more than two games the menu becomes a grid of six per page with **Previous** and **Next**.
- `FLATTODEPTH_STEAM_LIBRARIES` (Steam library folders separated by `;`) overrides where FlatToDepth looks for Steam games, if Steam keeps its libraries somewhere unusual.

## Limits worth knowing

- FlatToDepth cannot make a game 3D by itself. Without a fix made for it, a game in the list does not look right.
- The VR controllers need the ViGEmBus driver (below). Keyboard-only games are not covered.
- Only DirectX 11 games, one game at a time, and only on Windows with Steam.
- The 3D has been verified in a headset for the Ori games. In Will of the Wisps and Hollow Knight the controllers and rumble have been confirmed on a Steam Frame; their 3D has had little testing. Treat any other game as an experiment until someone has played it.

## Controllers: one virtual pad for every game

A game has to see your VR controllers as a gamepad. The Steam Frame controllers are, in effect, an Xbox 360 controller split in half, so FlatToDepth makes one: a **virtual Xbox 360 controller** in Windows, through the **ViGEmBus** driver, fed with what your controllers do (left stick and D-pad, right A/B/X/Y, triggers, bumpers, stick clicks, Menu and View). Windows shows it to every game like a real pad (XInput, XInput 9.1.0 for old 32-bit games, Windows.Gaming.Input, Raw Input), so it works the same for Ori and the Blind Forest (32-bit), Ori and the Will of the Wisps, Hollow Knight and any game you add, and the game's vibration comes back through it to your controllers. The pad exists while a game that wants it is running and goes away when it ends.


Older versions put a small replacement XInput DLL (a *shim*) in each game's folder instead. It only reached games that call that exact DLL (Will of the Wisps loaded it and never called it; Hollow Knight does not use XInput at all), so it has been replaced. An entry can still ask for one with `shim_files` (for a PC without the driver), and `Install.cmd` removes a shim from an earlier version when the entry is virtual.

ViGEmBus is a signed driver that DS4Windows and many emulators already install. FlatToDepth does not install it: get it from [github.com/nefarius/ViGEmBus](https://github.com/nefarius/ViGEmBus/releases) (the project is retired, but the last release still works) and run its installer once. `Install.cmd` tells you whether it is there; if it is not, a game that needs it simply has no VR controllers (play it with a gamepad or keyboard on the PC) and `logs\flattodepth.log` says `No virtual controller for this game: ...`.

A few things to know. The game window still has to be the active window on the desktop (Windows hands controller input only to the window in front). If a game also has an old shim in its folder, `Install.cmd` removes it, because a shim answers "no controller" for every pad and would hide the virtual one.
## Hollow Knight

Hollow Knight has a Geo-11 fix by masterotaku, but it was found not to start with the current game on a current Windows. The game is now built with Unity 6, which needs a function (`D3D11On12CreateDevice`) that the older Geo-11 `d3d11.dll` inside the fix does not have, so the process stops with *entry point not found* before anything is drawn. The catalog entry therefore lays the current Geo-11 driver's `d3d11.dll` and `nvapi64.dll` over the fix (`driver_url`, see above); the fix's shaders and settings are still its own.

Two things to know:

- **Controllers.** Unity 6 reads controllers through `Windows.Gaming.Input`, not XInput, which a shim could never reach; the virtual pad needs no help from the game (see [Controllers](#controllers-one-virtual-pad-for-every-game)). Without the ViGEmBus driver the VR controllers still move and resize the screen and open the tools panel, but a keyboard or a gamepad on the PC plays the game.
- **3D not yet confirmed in a headset.** The game starts, exports its picture with the newer driver and plays with the VR controllers; how the fix's shaders look with the newer driver has not been checked.
