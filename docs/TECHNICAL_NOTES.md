# Technical notes

These notes cover how the Rockstar Games Launcher (RGL) build of GTA Vice City and the launcher behave, as far as the shim relies on them.

## The RGL `gta-vc.exe`

Static analysis with [`tools/inspect_pe.py`](../tools/inspect_pe.py) (read-only; requires `pip install pefile`). Analysed build: RGL GTA Vice City, `version.txt` = 1.0.0.15 (launcher title version 1.0.1.19).

```
python tools/inspect_pe.py "<RGL folder>\gta-vc.exe"
```

| Item | Result |
|---|---|
| Architecture | x86 (PE32), image base `0x400000`, no ASLR, no DEP |
| Entry point | RVA `0x640000` (VA `0xA40000`) in section **`.rkstr`** (RWX, `0x305` bytes), not `.text` |
| Entry bytes | `60 55 8B EC 81 EC 00 04 00 00 E8 …`: starts with `pushad`, a protection/unpack stub |
| `.text` | entropy 7.34, so the game code is encrypted on disk |
| `.tbm` | `0xFB000` bytes, entropy 8.00: encrypted payload |
| TLS callbacks | none (TLS directory present, no callbacks) |
| Imports | ADVAPI32, DDRAW, **DINPUT8** (1 import), KERNEL32, USER32, WINMM, d3d8, mss32, ole32 |
| `MTLX.dll` | not imported; the stub loads it at runtime |

`_rwcseg` / `_rwdseg` are RenderWare sections and are present in the original 1.0 exe too.

The protection is the same as on the RGL GTA III exe: same `.rkstr` stub (identical first bytes and size) and the same kind of encrypted `.tbm` payload, so the GTA III shim's design carries over unchanged.

A typical downgraded 1.0 exe is a plain, unpacked x86 build. Its entry point is in `.text` (entropy ≈ 6.5), and it has the same imports.

### Consequences for the design

- **Proxy choice.** `dinput8.dll` is a static import and not a KnownDLL, so a copy in the game folder wins. It has only six exports, all trivial to forward.
- **Hook point.** Patching the protection stub's first 5 bytes from `DllMain` means the stub never runs: nothing is decrypted and `MTLX.dll` is never loaded. The shim never calls the original code mid-function, so no trampoline is needed. The fail-safe restores the 5 bytes and jumps back, and the stub then sees exactly what the loader passed: a naked thunk preserves all registers and flags around the shim logic.
- **No IAT fallback.** `.text` is encrypted until the stub has run, so hooking a CRT import would never fire in time.

## Launcher behaviour (observed in the launcher log)

`Documents\Rockstar Games\Launcher\launcher.log`

- **Command line:** `"<RGL folder>\gta-vc.exe" @commandline.txt -fromRGL -rglLanguage=en-US`, working directory = RGL folder.
- **Before launch:** "Cloud Save sync requested" → conflict check → upload/download → "Finished syncing cloud saves". Then the game process is started.
- **Process tracking:** the launcher waits on the PID it started and logs "Game exited with code …". It accepts any exit code and then syncs again. "Game left no exit file" is logged because the RGL game never ran. That's harmless.
- **No handshake required:** the launcher doesn't need MTLX to connect for the session to count, so the sync before and after works with the shim in place.
- **Sync state:** `Documents\Rockstar Games\GTA Vice City\Profiles\<profile id>\cloudsavedata.dat` records what was last synced. Saves themselves (`GTAVCsf*.b`, plus settings in `gta_vc.set`) live only in `Documents\GTA Vice City User Files`.
- **Install location:** `HKLM\SOFTWARE\WOW6432Node\Rockstar Games\Grand Theft Auto: Vice City\InstallFolder` (note the colon), used by the installer for auto-detection.
- **Folder permissions:** the RGL installer gives normal users read/execute only on the game folder, hence the elevated installer and the `%LOCALAPPDATA%` log fallback.
- **Compatibility layer:** Windows passes the parent's active compatibility layers to child processes through `__COMPAT_LAYER`, and that overrides the child's own settings (a downgraded exe often has its own, e.g. `WIN7RTM`), so the shim clears it before `CreateProcess`. On the GTA III install this carried `DWM8And16BitMitigation`; the VC exe may carry whatever the launcher or the user set (`HIGHDPIAWARE` on the analysed machine). The log records what was cleared.

## The save-menu freeze

Found while building the GTA III shim, and kept here as is. With a plain `WaitForSingleObject` in the parent, the modded game froze as soon as its save menu opened. It didn't crash: the debug mode showed no second-chance exception, and Alt+F4 ended the game through a normal `PostQuitMessage`. The game was waiting on its parent process to process a window message.

Waiting with `MsgWaitForMultipleObjects` and dispatching messages fixed it. The debug loop does the same between debug events.

## Debug mode (`DebugChild=1`)

The shim creates the game with `DEBUG_ONLY_THIS_PROCESS` and acts as a minimal debugger. It sets `_NO_DEBUG_HEAP=1` so the game keeps its normal heap. It logs:

- first- and second-chance exceptions as `module+offset`, access-violation details, and `OutputDebugString` text;
- calls to `ExitProcess`, `PostQuitMessage` and `DestroyWindow` (software breakpoints that re-arm after a single step), with a heuristic stack trace;
- writes to `RsGlobal.quit` (`0x9B48D8 + 0x18` in the VC 1.0 exe, located from the `RsGlobal.appName = "GTA: Vice City"` store in `RsInitialise`), through a hardware write watch;
- a stack dump of every thread when the game window stops responding, on **Ctrl+Shift+F12**, or on the first quit call.
