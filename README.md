# FlatToDepth

[![build](https://github.com/Blackskydk/FlatToDepth/actions/workflows/build.yml/badge.svg)](https://github.com/Blackskydk/FlatToDepth/actions/workflows/build.yml)

**FlatToDepth turns 2D PC games into 3D-with-depth in VR.** The game appears as a huge stereoscopic screen you can move, push away and resize with your VR controllers, and you play it with those same controllers. The depth comes from a **Geo-11 stereo fix**, a small set of files made for one particular DirectX 11 game that makes it draw once per eye. FlatToDepth takes that picture and shows each eye's view in the matching eye of your headset through OpenXR. It is not tied to any one game: a [catalog of games](docs/GAMES.md) says which ones it can handle, a scan shows which of your Steam games are worth trying, and you can add your own.

It ships ready for **Ori and the Blind Forest: Definitive Edition** and **Ori and the Will of the Wisps**, the games it was first built and tested with.

- **Real 3D depth.** The game is rendered twice, once per eye. No screen capture, no desktop mirroring.
- **An in-headset game menu.** Start FlatToDepth, point at a game, pull the trigger. It launches the game through Steam and shows it; when you quit, you are back at the menu. It lists the supported games you have installed, with pages once there are more than a few.
- **Your Steam library, scanned.** `FlatToDepth.exe --scan` looks through the games you have installed and tells you which are supported, which draw with DirectX 11 and could be worth a stereo fix, and prints a draft entry to start from. Games come from a catalog (`games.catalog.ini`) plus your own `games.user.ini`. See [Games](docs/GAMES.md).
- **SteamVR-style screen controls.** Point under the screen for a grab bar: drag to move it anywhere, push or pull the thumbstick to move it closer or further, and drag the corner handle to resize it. It is saved per game.
- **VR controllers as the gamepad.** The Steam Frame controllers act as an ordinary gamepad in the games, and the game's rumble is played on them.
- **Tools without a keyboard.** An in-headset panel presses the stereo fix's keys (convergence, HUD depth and so on, whatever the fix has) for you, swaps the eyes, and switches the screen options below. Both grips and B opens it.
- **A nicer room.** An optional curved screen, an ambient glow of the picture's colours around it, and a floating-window setting that stops things popping out of the screen from being cut off by its edges.
- **Ultrawide friendly.** The black bars a 16:9 game gets on an ultrawide monitor are cropped away.

> **Status: experimental.** Built and tested on a **Valve Steam Frame** with SteamVR. Ori and the Blind Forest is the thoroughly tested game; Will of the Wisps support is newer, no other game has been verified, and the curved screen, glow, floating window, rumble and tools panel are new and have not yet been seen in a headset (see the [testing notes](docs/testing.md)). Other SteamVR headsets may work with fewer controller buttons and are untested. Expect rough edges, and please report them.

## Install

1. Download the latest `FlatToDepth-<version>-win64.zip` from [**Releases**](https://github.com/Blackskydk/FlatToDepth/releases/latest) and extract it somewhere permanent (for example `C:\FlatToDepth`).
2. Close the games and double-click **`Install.cmd`**. It finds the supported games in your Steam library, installs each one's Geo-11 stereo fix (downloaded from its author after you accept its license) and the controller shim.
3. Start SteamVR, put the headset on, double-click **`Start-FlatToDepth.cmd`**, point at a game and pull the trigger.

That is the whole thing. The [**install guide**](docs/INSTALL.md) has the details, how to start it from inside the headset, updating, uninstalling and troubleshooting.

**Requirements:** Windows 10/11 64-bit, Steam with SteamVR (set as the OpenXR runtime), a SteamVR headset, and at least one supported game from Steam (the two Ori games, or others you add).

## Documentation

| | |
| --- | --- |
| [Install guide](docs/INSTALL.md) | Step by step, updating, troubleshooting |
| [Usage guide](docs/USAGE.md) | Controls, moving and resizing the screen, settings, diagnostics |
| [Games](docs/GAMES.md) | Which games work, scanning your Steam library, adding a game, contributing to the catalog |
| [Building from source](docs/BUILDING.md) | Build, test, and how releases are made |
| [Technical assessment](docs/assessment.md) | How it works, risks and design decisions |
| [Testing notes](docs/testing.md) | What has been verified on real hardware, and what has not |

## Licenses and what is not included

FlatToDepth's code is [MIT licensed](LICENSE). It contains no game assets and does not include any Geo-11 stereo fix: those are other people's work, usually licensed for personal, non-commercial use only, and are downloaded by the installer straight from their author after you accept the license. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). You must own the games. This is an independent project, not affiliated with any game's publisher, or with Valve.
