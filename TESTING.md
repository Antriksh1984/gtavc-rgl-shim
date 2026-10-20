# Manual test checklist

Log: `%LOCALAPPDATA%\VCShim\shim.log` (or `shim.log` in the game folder, if that's writable).
Backups: `%LOCALAPPDATA%\VCShim\backups\`.
Paste this into Explorer's address bar to open the shim folder: `%LOCALAPPDATA%\VCShim`.

Editing `shim.ini` needs an editor started as administrator.

| # | Test | Steps | Expected | Result |
|---|------|-------|----------|--------|
| 1 | Launch from RGL | Click Play in the launcher | The modded 1.0 game opens, with no second window. The log shows `Launched '<modded folder>\gta-vc.exe'`. The launcher shows the game as running. | |
| 2 | Save, quit, sync | Save in the modded game, quit to desktop | The log shows `Modded game exited with code N` (any value is normal), the launcher shows a cloud sync, and a new folder exists under `backups\` | |
| 2b | Save menu doesn't freeze | Walk into a save house and save to a slot | The save completes and the game keeps running. In the GTA III shim, a freeze here was a real bug: the shim didn't process messages while waiting. | |
| 3 | Shift bypass | Hold Shift while clicking Play, until a window appears | The vanilla RGL game runs. The log shows `Shift held: bypassing` and `Original entry point restored` | |
| 4 | Bad path | Set `ModdedExe=D:\nope\gta-vc.exe` in `shim.ini`, then launch | The vanilla RGL game runs. The log shows `ERROR: ModdedExe '...' does not exist`. **Put the path back afterwards.** | |
| 5 | Job object | While playing modded, end the official `gta-vc.exe` in Task Manager. On the **Details** tab, the official one is the one whose path is the RGL folder. | The modded game closes too | |
| 6 | Verify files | Launcher → GTA Vice City → Settings → Verify game file integrity | Note whether `dinput8.dll` and `shim.ini` survive. If not, re-run install. | |
| 6b | Mods load | Launch from RGL, then quit | Your mods are active in game (e.g. `cleo.log` in the modded folder gets a new timestamp, SilentPatch fixes apply). The shim starts the exe in its own folder, so ASI/CLEO loaders work as when launched directly. | |
| 7 | Cloud restore | Back up `GTA Vice City User Files` and `Documents\Rockstar Games\GTA Vice City\Profiles` outside OneDrive. Move the `GTAVCsf*.b` saves and `Profiles\<id>\cloudsavedata.dat` aside, so it looks like a fresh PC, then launch | The launcher downloads the cloud saves before the game starts, and the modded game lists and loads them | |

Before test 7, back up your saves by hand, not only through the shim. `cloudsavedata.dat` is the launcher's record of what was last synced. If you remove only the saves, the launcher may treat them as deleted rather than missing. The shim only copies saves and never deletes them, but the launcher's sync is outside its control.

If the game freezes, set `DebugChild=1` and reproduce it. While it's frozen, press **Ctrl+Shift+F12**, wait 2 seconds, then press Alt+F4. The log will then contain stack dumps of every thread.

When reporting a problem, include the last ~30 lines of `shim.log` and which test failed.
