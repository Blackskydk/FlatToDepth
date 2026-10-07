# Building FlatToDepth from source

Most people should use the [release download](INSTALL.md). Build from source to change the code or to review exactly what you run.

## Requirements

- Windows 10 or 11, 64-bit
- Visual Studio 2022 or newer (or the Build Tools) with the **Desktop development with C++** workload; this includes the Windows SDK
- PowerShell 5.1 or newer, and CMake on your `PATH` (or let the bootstrap script download it)
- Internet access for the first build, to fetch the official OpenXR loader

## Build

```powershell
./scripts/bootstrap.ps1          # downloads the OpenXR loader (and a portable CMake) into .deps/
./scripts/build.cmd              # FlatToDepth.exe, the tests and the diagnostic tools      -> build\
./scripts/build-gamepad.cmd      # the 32-bit controller shim (32-bit games)           -> build\gamepad\
./scripts/build-gamepad64.cmd    # the 64-bit controller shim (64-bit games)           -> build\gamepad64\
```

The scripts look for Visual Studio 2026 Community by default. Set `FLATTODEPTH_VS_INSTALL` to your Visual Studio folder (for example `C:\Program Files\Microsoft Visual Studio\2022\Community`) if it is elsewhere. `FLATTODEPTH_BUILD_DIR` builds into a different folder than `build` (useful while FlatToDepth is running from `build`). If you have no Windows SDK installed, `./scripts/bootstrap.ps1 -PortableSdk` downloads one into `.deps/`.

Run it straight from the checkout: `./scripts/run.ps1`, or `./scripts/install.ps1` to set the games up. Everything in `scripts\` works the same in a source checkout and in a release zip.

## Test

```powershell
cd build
ctest --output-on-failure
```

| Test | Needs |
| --- | --- |
| `window_pointer_control` | nothing: the moving, resizing and input masking logic (flat and curved screens), the tools button, the UI artwork |
| `curved_screen` | nothing special: the curved screen's geometry, and its Direct3D 11 renderer run on WARP (the software device), checking the texture orientation, that a curve's edges are nearer and taller than its middle, the floating window and the glow. `FlatToDepthCurvedTest --hardware --dump <folder>` runs it on the real graphics card and writes the images to look at |
| `tools_panel_fx` | nothing: the tools panel and its buttons, the key presser, the floating window, the ambient glow maths, cylinder layer building, and the settings files (it reads the shipped `flattodepth.default.ini`). `FlatToDepthToolsTest --dump tools.bmp glow.bmp` writes the panel and glow textures as pictures |
| `game_menu` | nothing: the menu's layout and pages, placement, hover and click rules, the artwork |
| `games_catalog_scan` | nothing: the games catalog and its validation (cases shared with the PowerShell reader), the Steam library scan, the executable reader and the `--scan` report, on synthetic executables and a fake Steam library |
| `virtual_pad` | nothing to build; with the ViGEmBus driver installed it plugs a virtual pad in and reads it back through Windows' XInput, otherwise it checks only the protocol numbers and skips the rest |
| `steamvr_helper` | nothing: finding SteamVR, and when the game-theater setting is turned off and put back; with SteamVR running it also reads (never changes) the setting |
| `tests/catalog_test.ps1` (run it directly: `powershell -File tests/catalog_test.ps1`) | nothing: the same catalog, read by the install scripts' PowerShell reader, with the same shared cases |
| `controller_transport_window` | both shims built (`build-gamepad.cmd`, `build-gamepad64.cmd`); also checks rumble travelling from the shim back to the bridge, and that a shim finds the bridge's real mapping name (that part is skipped if a real bridge is running on the PC) |
| `tests/scripts_test.ps1` (run it directly: `powershell -File tests/scripts_test.ps1`) | nothing: Steam library resolution, games moved between libraries, the installer in dry-run mode, the license gate, and a real run of `package.ps1` on stand-in files; all with fake folders |
| `gpu_share_split` | a real GPU (skipped on CI, which has none); also checks the ambient glow's GPU averaging and readback |

None of these need a headset. They cannot show that stereo looks right or that a game takes the controller input; `testing.md` lists what has been verified on real hardware and what is still open.

## Releases

Continuous integration (`.github/workflows/build.yml`) builds and tests every push. To publish a release, tag a commit:

```powershell
git tag v0.2.0
git push origin v0.2.0
```

The workflow then builds everything, runs `scripts/package.ps1`, and publishes `FlatToDepth-v0.2.0-win64.zip` and its SHA256 on the GitHub Releases page. You can try the packaging locally with `./scripts/package.ps1 -Version dev`; the zip appears in `dist\`.

The stereo fixes are never part of the repository or the release (their license forbids redistribution); `games.catalog.ini` holds their download addresses and pinned SHA256 fingerprints. If an author re-uploads a fix, the fingerprint must be updated there after checking the new file.

## Layout

| Path | What |
| --- | --- |
| `src/` | the program (`main.cpp`), the pointer and menu logic (`window_control.hpp`, `game_picker.hpp`), the game list and Steam records (`games.hpp`, `catalog.hpp`, `scan.hpp`), UI artwork (`ui_*.hpp`, `picker_art.hpp`), and the controller shim (`xinput_proxy.cpp`) |
| `tests/` | the tests |
| `scripts/` | build, package, install and launch scripts |
| `docs/` | guides, the [technical assessment](assessment.md) and [testing notes](testing.md) |
