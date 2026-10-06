# Third-party notices

FlatToDepth's own code is under the [MIT License](LICENSE). It works with, and in the release download includes, the
following. Everything else it needs is already on your PC (Windows, Steam, SteamVR, the games).

## Included in the release download

**OpenXR loader** (`bin/openxr_loader.dll`), by the Khronos Group, from the official `OpenXR.Loader` package,
licensed `Apache-2.0 OR MIT`. Source and licenses: <https://github.com/KhronosGroup/OpenXR-SDK>.

## Not included: the Geo-11 stereo fixes

The 3D effect comes from a **Geo-11 stereo fix** made for each game. The two the catalog can install were made by
Alejandro Rodriguez Solis ([HelixMod](https://helixmod.blogspot.com)). They are licensed for **personal, non-commercial use only** and may
not be redistributed or re-uploaded, so **they are not part of this repository or any release**. The installer
(`scripts/install-geo11.ps1`) downloads them from their author's own host onto your PC only after you accept the
license, verifies the download against a pinned SHA256, and saves the author's `LICENSE.txt` in the game folder.
Geo-11 itself is a DirectX 11 stereo driver built on 3Dmigoto's hooking; see the
[Geo-11 announcement](https://helixmod.blogspot.com/2022/06/announcing-new-geo-11-3d-driver.html).

The fixes the catalog installs today:

- Ori and the Blind Forest: <https://helixmod.blogspot.com/2015/04/ori-and-blind-forest-dx11.html>
- Ori and the Will of the Wisps: <https://helixmod.blogspot.com/2021/04/ori-and-will-of-wisps-3d-vision-ready.html>

## Acknowledgements

FlatToDepth reads the shared GPU picture that Geo-11's `katanga_vr` output mode publishes, the same interface used by
[Katanga](https://github.com/bo3b/katanga) and [VRScreenCap](https://github.com/artumino/VRScreenCap). The
protocol was studied from those projects; FlatToDepth's code is its own C++ implementation of that small interface.

## Trademarks and no affiliation

Every game name belongs to its owner; for example *Ori and the Blind Forest* and *Ori and the Will of the Wisps* belong to
Moon Studios and Xbox Game Studios / Microsoft. Steam, SteamVR and Steam Frame belong to Valve. FlatToDepth is an
independent project, is not affiliated with or endorsed by any of them, and contains no game assets or code. You need to
own the games.
