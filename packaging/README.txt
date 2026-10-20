GTA Vice City Launcher Shim  v{VERSION}
===================================

Play your modded, downgraded GTA Vice City 1.0 through the Rockstar Games Launcher,
so your saves keep syncing to Rockstar's cloud.

When you click Play in the launcher, the shim starts your modded 1.0 game
instead of the launcher's version. The launcher sees a normal game session,
so it syncs your saves before you play and uploads them after you quit.


WHAT YOU NEED
-------------
- GTA Vice City installed through the Rockstar Games Launcher.
- Your modded/downgraded 1.0 copy of GTA Vice City in a SEPARATE folder.
  (Both versions use the same save folder, Documents\GTA Vice City User Files.)
- Cloud saves turned on in the launcher (Settings > Grand Theft Auto: Vice City).


INSTALL
-------
1. Extract this whole zip to a folder (don't run it from inside the zip).
2. Quit the game if it's running.
3. Double-click Install.bat and click Yes on the administrator prompt.
   (The launcher's game folder is read-only without admin rights.)
4. The installer looks up where the launcher installed GTA Vice City and asks
   you to confirm. If it can't find it, or you answer "n", you choose the
   folder yourself:
     [1] Browse for its gta-vc.exe   (file dialog)
     [2] Type or paste the folder path
5. Choose your modded 1.0 gta-vc.exe the same way: browse for it, or
   type/paste the path to the exe or its folder.
6. Launch GTA Vice City from the Rockstar Games Launcher. Your modded game starts.

Running Install.bat again updates the shim and keeps your settings.

You can also skip the questions by passing the paths (e.g. from a
command prompt):
  Install.bat "D:\Games\Grand Theft Auto Vice City" "D:\Games\VC Modded\gta-vc.exe"
  Uninstall.bat "D:\Games\Grand Theft Auto Vice City"

The installer only adds two files to the launcher's GTA Vice City folder:
dinput8.dll and shim.ini. It never changes gta-vc.exe or your saves.


PLAY THE ORIGINAL VERSION INSTEAD
---------------------------------
Hold Shift from the moment you click Play until the game window appears.
To switch the shim off permanently, set Enabled=0 in shim.ini (edit it as
administrator) or run Uninstall.bat.


UNINSTALL
---------
Double-click Uninstall.bat and click Yes. It removes dinput8.dll (only if
it's the shim) and shim.ini. Your save backups are kept.


LOGS AND SAVE BACKUPS
---------------------
Paste this into Explorer's address bar:  %LOCALAPPDATA%\VCShim

  shim.log    What the shim did on each launch. Check here first if
              something goes wrong.
  backups\    A copy of your save folder made before every launch. The
              newest 10 are kept (KeepBackups in shim.ini). These are not
              uploaded to the cloud.


AFTER A LAUNCHER UPDATE OR "VERIFY FILES"
-----------------------------------------
The launcher may remove the shim's files. If the original game starts
without you holding Shift, just run Install.bat again.


TROUBLESHOOTING
---------------
- The original game starts instead of the modded one:
  open shim.log. It explains why (for example, a wrong ModdedExe path).
  The shim always falls back to the original game rather than crashing.
- "dinput8.dll already exists and is not the shim":
  another mod in the LAUNCHER's GTA Vice City folder uses that file name.
  The shim can't be installed alongside it.
- Mods in your MODDED folder that use dinput8.dll (e.g. Ultimate ASI
  Loader) are fine. Each game loads DLLs from its own folder, so the
  shim (in the launcher's folder) and your mods never meet. Don't copy the
  shim's dinput8.dll into the modded folder.
- The game freezes or crashes:
  set DebugChild=1 in shim.ini and reproduce the problem. If it freezes,
  press Ctrl+Shift+F12, wait 2 seconds, then Alt+F4. shim.log will then
  contain detailed diagnostics. Set DebugChild back to 0 afterwards.
- Odd exit codes in shim.log are normal: 1.0 games can return a random
  value when you quit, and the launcher accepts any exit code.


SHIM.INI SETTINGS
-----------------
  Enabled=1         0 = always start the original game
  ModdedExe=...     full path to your modded 1.0 gta-vc.exe
  ExtraArgs=        extra command-line arguments for the modded game
  SaveFolder=       empty = Documents\GTA Vice City User Files
  BackupSaves=1     back up saves before each launch
  KeepBackups=10    how many backups to keep (0 = all)
  DebugChild=0      diagnostics only
