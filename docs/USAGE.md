# Using FlatToDepth

New here? Start with the [install guide](INSTALL.md). This page covers day-to-day use, the controls and the settings.

## Starting

Start SteamVR and put the headset on, then double-click **`Start-FlatToDepth.cmd`** (or start *FlatToDepth* from your Steam library if you added it, see the install guide).

**The game menu.** A panel appears in front of you with a tile for each game you have installed (as Steam reports them). Point a controller at a tile and pull the trigger or squeeze the grip: FlatToDepth asks Steam to start that game (its launch options apply), shows "Starting..." until the game has a window and a picture, then shows the game with that game's own settings. When you quit the game you are back at the menu. A game that is already running is picked up automatically. With more than two games the tiles form a grid of six per page, with **Previous** and **Next**; when you have only a few installed, the rest of the first page shows supported games you do not have, greyed out. The games come from the catalog; see [Games](GAMES.md) to add your own. Hold both grips and press A (or press `R` in the FlatToDepth console window) to bring the menu back in front of you.

Command line, if you prefer it: `scripts\run.ps1` (menu), `scripts\run.ps1 -Game wotw` (skip the menu and bridge that game), `scripts\stop.ps1` (stop FlatToDepth and leave the game running). `FlatToDepth.exe --help` lists everything, including the games it knows.

## Games

The games FlatToDepth knows are listed in `games.catalog.ini`, with your own additions in `games.user.ini` ([how to add one](GAMES.md)). `FlatToDepth.exe --scan` shows which of your installed Steam games are supported and which might be worth trying. It ships with:

| Game | `--game` | Settings file | Stereo fix | Controllers reach the game as |
| --- | --- | --- | --- | --- |
| Ori and the Blind Forest: Definitive Edition (32-bit) | `blindforest` | `flattodepth-blindforest.ini` | Geo-11 fix | a virtual Xbox controller (needs [ViGEmBus](GAMES.md#controllers-one-virtual-pad-for-every-game)) |
| Ori and the Will of the Wisps (64-bit) | `wotw` | `flattodepth-wotw.ini` | Geo-11 fix | a virtual Xbox controller (needs [ViGEmBus](GAMES.md#controllers-one-virtual-pad-for-every-game)) |
| Hollow Knight (64-bit) | `hollow-knight` | `flattodepth-hollow-knight.ini` | Geo-11 fix | a virtual Xbox controller (needs [ViGEmBus](GAMES.md#controllers-one-virtual-pad-for-every-game)) |

Each game has its own settings file (`flattodepth-<game>.ini`), so window size, placement, eye order and crop never disturb another game. Each file is created from `flattodepth.default.ini` the first time the game is shown; delete one to reset that game. `flattodepth.ini` holds the game menu's own screen.

In the game, the stereo fix has function keys: for both Ori games **F1/F2** change the 3D convergence and HUD depth and **F3** toggles the HUD (Blind Forest also: F4 vignette, F5 bloom; Will of the Wisps: F4 blurriness, F5 vignette, F6 bloom presets; Hollow Knight: F1 convergence, F2 depth of field, F3 HUD depth, F4 film grain, F5 bloom). Which keys do what is up to each fix, and the catalog lists them for each game. In the headset, the [tools panel](#the-tools-panel) presses these keys for you. The game's camera is still the game's own.

## The screen: moving and resizing

The screen is moved the way SteamVR moves its windows, with a laser pointer and a grab bar.

- **Show the controls:** point either controller at the spot just under the screen. A grab bar and a resize handle (just outside the bottom-right corner) fade in there, and fade out soon after the laser leaves them. Pointing at the game itself shows nothing. (`auto_hide=0` in the settings file keeps them visible.)
- **Move:** point at the bar until it highlights and a laser appears, then hold **trigger or grip** on it. The screen stays attached to your laser, so sweep it anywhere around you and it turns to face you. Let go to drop it.
- **Push and pull:** while holding the bar, push the **thumbstick** forward to send the screen further away, pull back to bring it closer. The size stays the same, so it looks smaller when far. It cannot be pulled into your face or the controller.
- **Resize:** hold trigger or grip on the handle and drag. The screen grows out from its middle to both sides, and keeps its shape.
- **Recenter:** hold both grips and press A to put the screen (or the menu) back in front of you.
- **Tools:** the round button left of the grab bar (or both grips and B) opens the [tools panel](#the-tools-panel).
- **Swap eyes:** both grips and X (or `S` in the FlatToDepth console window), if the depth looks inside-out. It is saved for that game.
- **Saved automatically:** the size and place are stored in the game's settings file whenever you let go, and restored next time.

A controller that is pointing at the bar or the handle (laser visible) is withheld from the game until it points away, so UI clicks never become jumps. The other controller keeps playing. Grips on their own do nothing in the game. You can do all of this seated.

## The tools panel

There is no keyboard in the headset, so the panel carries the buttons you would have pressed on one, and the things you would otherwise edit in the settings file. Open it with the round button left of the grab bar (point just under the screen and it fades in beside the bar) or by holding both grips and pressing B. It floats in front of you, about 1.3 m wide; point a controller at a button and pull the trigger or squeeze the grip. The cross in its corner (or the same chord) closes it. The controller pointing at the panel is kept from the game, the other one keeps playing.

**Moving it.** Like the game window, the panel has a bar underneath it. Hold the trigger or a grip on that bar and move your hand: the panel follows your hand in every direction (up, down, sideways, nearer, further, and it swings round you when you turn your hand) and turns to face you. The thumbstick, while you hold, pushes it away or pulls it closer. The bar lights up under your laser and turns blue while you carry the panel. It opens in the place you left it (saved with the game's settings, as `[tools]`); the Recenter button or both grips + A brings it back in front of you if it is lost.

**Knowing what a button does.** Point a laser at a button and the box under the buttons explains it in plain words. For the game's own shortcuts that also says what the fix does when you press it: a shortcut like Convergence steps through a list of presets (the box lists them, for example `0 → 15 → 28 → 38.6`, and the button shows which step it is on, such as `F1 · 2/4`); a toggle just flips between two states (`F2 · toggle`, then `switched` and `normal`). This comes from the fix's own `d3dx.ini` in the game folder, so it is right for that fix. The step shown is counted from the presses made with this panel and the fix's starting value: a press on a keyboard is not seen, so it can be a step off after that. FlatToDepth's own buttons show their state in bold on the button: green means on, blue off; Curved screen and Float window also have steps (Off, Low, Medium, High) and show them with dots.

**Seeing what a setting does.** While no laser points at the panel it turns half see-through, so the game shows behind it and you can watch what a setting does; point at it again and it is solid.

| Button | What it does |
| --- | --- |
| **The game's own shortcuts** (for the Ori games: Convergence, HUD depth, HUD, Vignette, Bloom, and Blurriness in Will of the Wisps) | Presses that game's Geo-11 fix key for you (F1, F2, F3, ... as the button says; the catalog entry decides which buttons a game has). Each press steps through the fix's presets, exactly like the key. The key goes to the game window, so the game must be the window that has the keyboard on the desktop, which it is when you started it from the menu; if it is not, the panel says so and sends nothing (it never types into another program). |
| **Swap eyes** | Flips which half of the picture goes to which eye. Use it if the depth looks inside-out. Saved as `swap_eyes`. |
| **Curved screen** | Off, Low, Medium, High: bends the screen round you. Saved as `curvature`. SteamVR has no curved layers, so FlatToDepth draws the curve itself; the button is dimmed only if this PC cannot do that. |
| **Ambient glow** | A soft wash of the picture's own colours around the screen, instead of a black room. Saved as `glow`. |
| **Float window** | Off, Low, Medium, High. Each eye hides a thin sliver of one edge of the picture so that the screen's edges look nearer than the picture. That stops things which pop out of the screen from being cut off by it, which is the usual cause of eye strain at the edges. Saved as `float_window`. |
| **Rumble** | Turns the game's rumble on the controllers on or off. Saved as `[haptics] enabled`. |
| **Recenter** | Puts the screen back in front of you, like both grips and A. |

The line at the top of the panel says what the last press did. Every setting changed here is written to that game's settings file, so it is still there next time.

## Controls in the game

On a Steam Frame the controllers act as an ordinary gamepad: left stick and D-pad, right A/B/X/Y, triggers, bumpers, stick clicks, right Menu (Start), left View (Back). Keep the game window active on the desktop (FlatToDepth cannot click it for you); starting the game from the menu does that. Opening the Steam dashboard pauses game input until FlatToDepth has focus again. Headsets without the Steam Frame controller profile fall back to Oculus-Touch-style bindings with fewer buttons, and only the trigger can grab the screen.

## Rumble

When the game rumbles, the controllers vibrate. The game's two motors (heavy and light) are mixed so that both controllers follow the stronger one; set `[haptics] split=1` to give the left motor to the left controller and the right motor to the right one. `[haptics] strength` (0 to 2) scales it, and the panel's Rumble button turns it off. The panel and its bar also give a short click when you point at or press a button. Vibration is silent while the menu is up or the controllers are not in your hands. It works for both ways a game gets its controller: through a shim, and through the virtual Xbox controller (the driver passes the game's vibration back to FlatToDepth). If the runtime does not offer vibration for the controllers the log says `haptic output binding rejected` and the Rumble button is dimmed. (Until 2026-10 this always happened: the haptic path FlatToDepth asked for was misspelled, so rumble never worked on any game.)

## Curved screen

The panel's Curved screen button bends the screen into a section of a cylinder around you: Low is a gentle bend, High wraps right round. The bar and the resize handle follow the curve, and you can still push, pull and resize it. The curve is concentric with where you were when you placed or last moved the screen, so after moving it, it is centred on you again. A runtime that offers `XR_KHR_composition_layer_cylinder` draws it. SteamVR does not, so there FlatToDepth draws the curved screen itself: it texture-maps each eye's picture onto the cylinder and renders that into a full-view layer. That costs some graphics power (two eye-sized images every frame) only while the curve is on; a flat screen is unchanged. The ambient glow is drawn into the same image. If this PC cannot do it the log says `Curved screen unavailable` and the screen stays flat.

Only for a runtime with curved layers of its own: if a curved screen appears at the wrong distance (far too near, or behind you), it reads the curved layer's position the other way round: set `curve_pose_at_axis=1` in the game's settings file and restart FlatToDepth, and please report it so the default can be fixed.

## Settings

Edit with Notepad and restart FlatToDepth. Comments in the files explain each value.

| Setting | Meaning |
| --- | --- |
| `[screen] picture_width_m` | Width of the visible picture in metres (changed by resizing in VR) |
| `distance_m`, `vertical_offset_m` | Where the screen first appears relative to you |
| `swap_eyes` | `1` or `0`; flip it if the depth looks inside-out |
| `crop_aspect` | Keep only the centred part with this width:height; the default `1.777778` (16:9) removes the black bars a 16:9 game gets on an ultrawide monitor; `0` shows the whole picture |
| `curvature` | `0` flat, up to `1` wrapping right round you; the panel cycles 0, 0.3, 0.55, 1 |
| `curve_pose_at_axis` | `1` if a runtime's own curved layer appears at the wrong distance, see above (not used when FlatToDepth draws the curve) |
| `glow`, `glow_strength` | The ambient glow on or off, and how strong it is (0 to 1) |
| `float_window` | Fraction of the picture each eye hides at one edge, 0 (off) to 0.05; the panel cycles 0, 0.008, 0.016, 0.025 |
| `[ui] auto_hide` | `1` hides the bar and handle unless you point at them |
| `[ui] push_pull_rate` | How fast the thumbstick moves the screen (0.1 to 5) |
| `[haptics] enabled`, `strength`, `split` | Game rumble on the controllers: on or off, how strong (0 to 2), and whether each motor drives its own controller |
| `[steamvr] hide_game_theater` | `1` (the default): while FlatToDepth runs, SteamVR does not open a flat game's own window in front of FlatToDepth when the game starts. FlatToDepth turns SteamVR's `dashboard.autoShowGameTheater` off and puts it back when it ends (and `Uninstall.cmd` puts it back if a crash left it off). `0` leaves SteamVR alone. Read from `flattodepth.ini` only |
| `[tools] placed`, `x_m`, `y_m`, `z_m` | Where the tools panel opens, in metres right, up and forward of where you face. Written when you carry the panel; delete the four lines to put it back in front of you |

The stereo effect itself (depth strength, convergence) is controlled by the fix's own `d3dxdm.ini` in the game folder and the in-game F-keys. The installer starts with a modest separation of 25.

## Diagnostics

Run these from the FlatToDepth folder (`bin\` in a release, `build\` in a source checkout). Logs go to `logs\flattodepth.log`.

- `FlatToDepth.exe --scan` lists your installed Steam games and which of them FlatToDepth supports or might support (`--all` lists every game, `--draft <app number>` prints a catalog entry to start from). It needs no headset; see [Games](GAMES.md).
- `FlatToDepth.exe --probe` reports the OpenXR runtime, headset and recommended resolution.
- `FlatToDepth.exe --test` shows a calibration pattern with known eye order: the left eye has a single red marker, the right a cyan double marker, and the orange block should look nearer than the blue one.
- `FlatToDepthCaptureProbe.exe 15 picture.bmp` waits up to 15 seconds for a running game's 3D output, reports whether the two eyes differ, and saves a small image of it (handy to check framing).
- Every game gets the controllers as a virtual pad, so the controller line in `logs\flattodepth.log` ends with `virtual_pad=on`; `off` means the ViGEmBus driver is missing or refused the pad, and the log says why (`No virtual controller for this game: ...`). `game_rumble_writes` in the same line counts the times the game asked for rumble. (`game_XInput_reads` only counts for an entry that installs a shim.) `logs\steamvr-theater.log` records what the helper did to SteamVR's theater setting.

## How it works, and limits

The Geo-11 stereo fix renders the game twice, once per eye, and shares that picture on the GPU. FlatToDepth reads it directly (no screen capture) and gives each eye its half as a world-locked panel through OpenXR, so you can look around while the screen stays put. The picture sharing has no frame counter, so tearing or a frozen frame is possible; HDR and head-tracked perspective are not implemented. See [the technical assessment](assessment.md) and the [testing notes](testing.md) for what has and has not been verified.
