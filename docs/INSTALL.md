# Installing FlatToDepth

FlatToDepth shows DirectX 11 games that have a Geo-11 stereo fix as a large stereoscopic 3D screen in your VR headset, and lets your VR controllers play them. It comes ready for **Ori and the Blind Forest: Definitive Edition**, **Ori and the Will of the Wisps** and **Hollow Knight**; other games are added through the [games catalog](GAMES.md). This page takes you from nothing to playing. It should take about ten minutes.

> **Status:** experimental, hobby-grade software. It was built and tested on a **Valve Steam Frame** with SteamVR, and Ori and the Blind Forest is the most thoroughly tested game. Other SteamVR headsets may work with limited controller support; they are untested.

## What you need

| | |
| --- | --- |
| PC | Windows 10 (a recent build) or Windows 11, 64-bit, with a GPU that runs the game and SteamVR together |
| VR | A headset that works with **SteamVR** (tested: Steam Frame) |
| Steam | Steam with **SteamVR** installed (Steam library, Tools) |
| Games | At least one supported game installed from Steam: the games it ships with, or others from the [catalog](GAMES.md) |
| Controllers | The free **ViGEmBus** driver, installed once (see below) |
| Disk | A few MB for FlatToDepth, plus each game's stereo fix (tens to about a hundred MB each) |

The one thing to install is the free **ViGEmBus** driver, which lets FlatToDepth present your VR controllers to the games as an ordinary Xbox controller (get it from [github.com/nefarius/ViGEmBus](https://github.com/nefarius/ViGEmBus/releases) and run its installer once; `Install.cmd` tells you whether it is there). Nothing else: no Visual Studio, no other drivers.

## Install

### 1. Download

Open the [**Releases** page](https://github.com/Blackskydk/FlatToDepth/releases/latest) and download `FlatToDepth-<version>-win64.zip`.

Optional: check the download. The release also lists a `.sha256` file; in PowerShell, `Get-FileHash FlatToDepth-<version>-win64.zip` should print the same value.

### 2. Unzip it somewhere permanent

Right-click the zip, **Extract All...**, and choose a folder you will keep, for example `C:\FlatToDepth`. Do not run it from inside the zip, and do not put it in `C:\Program Files` (FlatToDepth writes its settings and logs next to itself).

> If Windows marks the files as downloaded from the internet, right-click the zip first, choose **Properties**, tick **Unblock**, then extract.

### 3. Run the installer

Close the games, then double-click **`Install.cmd`**.

Windows may say *"Windows protected your PC"* because FlatToDepth is a small unsigned open-source program. Choose **More info**, then **Run anyway**. The program is built from the public source by GitHub's build servers (see [Is it safe?](#is-it-safe)).

The installer:

1. checks that SteamVR is installed and is the active OpenXR runtime (it tells you how to fix it if not; it never changes system settings),
2. finds the supported games (the catalog, plus any in your `games.user.ini`) in your Steam libraries and asks which to set up,
3. for each one, downloads the **Geo-11 stereo fix** from its author, after you accept the author's license (see below), when the game's entry says where to get one,
4. makes the VR controllers work as a gamepad in every game: nothing goes in the game folder, FlatToDepth makes a virtual Xbox controller while the game runs. That needs the free **ViGEmBus** driver; the installer tells you if it is missing (see [Controllers](GAMES.md#controllers-one-virtual-pad-for-every-game)). A replacement-DLL shim from an earlier version is removed.

Everything it adds is recorded, so **`Uninstall.cmd`** can remove it again.

**About the stereo fix.** The 3D effect comes from a *Geo-11* fix made for each game. The three in the catalog today were made by Alejandro Rodriguez Solis ([HelixMod](https://helixmod.blogspot.com)); for Hollow Knight the installer also takes the current Geo-11 driver files (`d3d11.dll`, `nvapi64.dll`) from their author, davegl1234, because the fix's own copy does not start with the current game. Such fixes are licensed for **personal, non-commercial use only** and may not be redistributed, so FlatToDepth does not include them. The installer downloads them straight from the author's own page onto your PC, checks the download against a known fingerprint (SHA256), and asks you to type `YES` to accept the license. A copy of the license is saved in the game folder as `LICENSE.txt`.

To run the installer without questions (it still needs your acceptance of the license): `Install.cmd -Yes -AcceptFixLicense`. To see what it would do without changing anything: `Install.cmd -DryRun`. To set up only some games: `Install.cmd -Games blindforest,wotw` (the names are the section names in the catalog).

### 4. Check SteamVR

Start **SteamVR** and put the headset on. In SteamVR, open **Settings** and make sure **SteamVR is set as the OpenXR runtime** (Settings, OpenXR, *Set SteamVR as OpenXR Runtime*). The installer tells you if it is not.

## Play

1. Start SteamVR, put the headset on.
2. Double-click **`Start-FlatToDepth.cmd`** on the PC.
3. A menu appears in front of you with a tile for each game. **Point a controller at a game and pull the trigger** (or squeeze the grip).
4. FlatToDepth starts the game through Steam, then shows it as a big 3D screen. When you quit the game you are back at the menu.

Controls, resizing and moving the screen are in the [usage guide](USAGE.md).

### Start it from inside the headset

To launch everything without touching the PC, add FlatToDepth to Steam once:

1. In Steam: **Games**, **Add a Non-Steam Game to My Library...**, **Browse...**, set the file type to **All files**, and pick **`scripts\start-flattodepth.cmd`** from your FlatToDepth folder. Add it.
2. Rename it to *FlatToDepth* if you like (right-click it in the library, Properties).

It now shows up in your Steam library and in the SteamVR dashboard like any other game. Start it from there and the game menu appears.

### Or launch a game straight from its Steam page

If you would rather skip the menu: right-click the game in Steam, **Properties**, **General**, **Launch Options**, and paste (using your own folder):

```
"C:\FlatToDepth\scripts\launch-game.cmd" %command%
```

The game then starts normally, and FlatToDepth starts alongside it when SteamVR is running and exits when you quit the game.

## Updating

Download the new zip and extract it **over the same folder** (agree to replace files). Your settings (`flattodepth*.ini`, `games.user.ini`) and the install records (`state\`) are kept. Run `Install.cmd` again so the controller setup is brought up to date too (a replacement-DLL shim left by an earlier version is removed from the game folders).

## Uninstalling

Close the games, double-click **`Uninstall.cmd`** (removes the stereo fix and any controller shim from the game folders, but only files that are still exactly as installed, and puts back SteamVR's game-theater setting if a crash left it off), then delete the FlatToDepth folder. If you added it to Steam, remove that entry too.

## Troubleshooting

Logs are in the FlatToDepth folder: `logs\flattodepth.log` (always) and `logs\launch.log` (when launching from a Steam launch option).

| Problem | What to try |
| --- | --- |
| **The menu never appears; the headset shows a black void** | Is SteamVR running with the headset awake, and is SteamVR the active OpenXR runtime (see step 4)? `logs\flattodepth.log` will say `ERROR` and why if it could not start. |
| **The game starts, but I see only "Waiting for ... to start drawing"** | The stereo fix is missing or not in `katanga_vr` mode. Re-run `Install.cmd`. Only one 3D exporter can run at a time: close other VR-screen or 3D tools. |
| **Depth looks inside-out** (far things pop out) | Press **Swap eyes** in the tools panel (hold both grips and press **B**), or hold both grips and press **X**. Or open the game's settings file (`flattodepth-<game>.ini`, for example `flattodepth-blindforest.ini`) in Notepad, change `swap_eyes=1` to `swap_eyes=0`, save, and restart FlatToDepth. |
| **The picture is cut off, or has black bars** | FlatToDepth crops to a centred 16:9 picture by default (most games are). For another shape set `crop_aspect` in the settings file; `0` shows everything. |
| **Controllers do not play the game** | `logs\flattodepth.log` should show `virtual_pad=on` in the controller line; if it says `off`, install the ViGEmBus driver (the log says why). The game window must be the active window on the desktop. A different headset may not have all the buttons. |
| **The flat game's own window opens in front of the VR screen** | FlatToDepth turns SteamVR's game theater off for this; check that `[steamvr] hide_game_theater` is `1` in `flattodepth.ini` and that `logs\steamvr-theater.log` shows it ran. |
| **The screen is gone, huge, or far away** | Hold **both grips and press A** to bring it back in front of you. Or delete the settings file to reset it. |
| **The installer says a file "would be replaced"** | A stereo fix was installed by hand earlier. FlatToDepth leaves it alone; make sure its `d3dxdm.ini` has `direct_mode = katanga_vr`. |
| **"does not match the pinned SHA256"** | The author updated the download. Nothing was installed. Please [open an issue](https://github.com/Blackskydk/FlatToDepth/issues) so the fingerprint can be updated. |
| **"Could not unpack ... 7z"** | Older Windows 10 cannot open `.7z` files with its built-in tools. Install [7-Zip](https://www.7-zip.org) and run the installer again. |
| **Antivirus complains about a DLL** | The stereo fix's `d3d11.dll` (and `nvapi64.dll`) are *wrapper* DLLs that sit in the game folder, which looks like game hacking to some scanners. Only use this with single-player games that have no anti-cheat (both Ori games are). If your antivirus quarantines them, add an exception for the game folder. |
| **A game I added is not in the menu** | The menu lists games Steam says are installed. Run `bin\FlatToDepth.exe --scan` to see what FlatToDepth sees, and check `logs\flattodepth.log` for `Games:` lines: a mistake in `games.user.ini` skips that game and says why. See [GAMES.md](GAMES.md). |
| **Choosing a game in the menu does nothing** | FlatToDepth asks Steam to start it, so Steam must be running and signed in. You can also start the game yourself; FlatToDepth notices it. |

Still stuck? [Open an issue](https://github.com/Blackskydk/FlatToDepth/issues/new?template=bug_report.md) and attach `logs\flattodepth.log`.

## Is it safe?

- FlatToDepth is open source; every release zip is built from the code in this repository by GitHub Actions, and you can read the build log on the release's Actions run.
- It changes only the game folders you choose and its own folder, and keeps a record of every file it adds.
- It never edits Windows settings, the registry, or Steam's files. The one setting it touches outside its own folders is SteamVR's game theater, which it turns off while it runs (so the flat game's own window does not open in front of the VR screen) and puts back afterwards; `hide_game_theater=0` in `flattodepth.ini` leaves SteamVR alone.
- It is not signed with a code-signing certificate, which is why Windows shows its warning.

## Build it yourself

See [BUILDING.md](BUILDING.md).
