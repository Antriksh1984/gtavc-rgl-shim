<#
.SYNOPSIS
  Installs the GTA Vice City launcher shim (dinput8.dll + shim.ini) into the Rockstar
  Games Launcher GTA Vice City folder. Run elevated (Install.bat does this for you).

.DESCRIPTION
  - Finds the RGL GTA Vice City folder from the registry (or asks).
  - Asks for the modded 1.0 gta-vc.exe (file picker), checks it is 32-bit and
    lives in a different folder.
  - Refuses to overwrite a dinput8.dll that isn't the shim.
  - New install: writes shim.ini. Update: keeps shim.ini, optionally changes ModdedExe.
  Never touches gta-vc.exe or any save file.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install.ps1 -GameDir "D:\Games\Grand Theft Auto Vice City" -ModdedExe "D:\Games\VC-Modded\gta-vc.exe"
#>
param(
    [string]$GameDir,    # RGL GTA Vice City folder (contains the official gta-vc.exe)
    [string]$ModdedExe,  # modded 1.0 gta-vc.exe
    [string]$DllPath,    # default: dinput8.dll next to this script, else ..\build\Release
    [switch]$PauseAtEnd  # keep the window open at the end (used by Install.bat)
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Host.UI.RawUI.WindowTitle = "GTA Vice City Launcher Shim - Install"

$code = 0
try {
    Write-Host ""
    Write-Host "  GTA Vice City Launcher Shim - Install" -ForegroundColor Cyan
    Write-Host "  =====================================" -ForegroundColor Cyan
    Write-Host ""

    # --- Shim DLL -----------------------------------------------------------
    if (-not $DllPath) {
        $DllPath = Join-Path $PSScriptRoot "dinput8.dll"
        if (-not (Test-Path -LiteralPath $DllPath)) {
            $DllPath = Join-Path $PSScriptRoot "..\build\Release\dinput8.dll"
        }
    }
    if (-not (Test-Path -LiteralPath $DllPath)) {
        throw "Shim DLL not found at '$DllPath'."
    }
    $version = (Get-Item -LiteralPath $DllPath).VersionInfo.FileVersion
    Write-Host "Shim version: $version"

    Assert-GameNotRunning

    # --- RGL game folder ----------------------------------------------------
    $GameDir = Resolve-GameDir $GameDir "Install the shim there?" -CheckRgl

    $iniPath = Join-Path $GameDir "shim.ini"
    $iniText = if (Test-Path -LiteralPath $iniPath) { Read-Ini $iniPath } else { $null }

    # --- Modded 1.0 exe -----------------------------------------------------
    # Updating offers to keep the ModdedExe already in shim.ini.
    $current = if ($iniText) { Get-IniValue $iniText "ModdedExe" } else { $null }
    $ModdedExe = Resolve-ModdedExe $ModdedExe $current $GameDir
    Write-Host "Modded game: $ModdedExe"

    # --- Copy the DLL -------------------------------------------------------
    $target = Join-Path $GameDir "dinput8.dll"
    if ((Test-Path -LiteralPath $target) -and -not (Test-IsShim $target)) {
        throw "'$target' already exists and is not the shim (another mod?). Remove or rename it first."
    }
    $isUpdate = Test-Path -LiteralPath $target
    try {
        Copy-Item -LiteralPath $DllPath -Destination $target -Force
    } catch [UnauthorizedAccessException] {
        # The RGL installer makes the game folder read-only for normal users.
        throw "Access denied writing to '$GameDir'. Run Install.bat (it asks for administrator rights)."
    }
    Write-Host "Copied dinput8.dll -> $target" -ForegroundColor Green

    # --- shim.ini -----------------------------------------------------------
    if ($iniText) {
        # Keep the user's settings; only point ModdedExe at the chosen exe.
        $updated = [regex]::Replace($iniText, "(?m)^(\s*ModdedExe\s*=).*$", { param($m) $m.Groups[1].Value + $ModdedExe })
        if ($updated -notmatch "(?m)^\s*ModdedExe\s*=") { $updated = $updated.TrimEnd() + "`r`nModdedExe=$ModdedExe`r`n" }
        Write-Ini $iniPath $updated
        Write-Host "Updated $iniPath (other settings kept)" -ForegroundColor Green
    } else {
        Write-Ini $iniPath @"
[Shim]
; 0 = launch the vanilla Rockstar Launcher game instead
Enabled=1
; Your modded/downgraded 1.0 gta-vc.exe
ModdedExe=$ModdedExe
; Extra command-line arguments for the modded game
ExtraArgs=
; Empty = default Documents\GTA Vice City User Files
SaveFolder=
; Copy saves to %LOCALAPPDATA%\VCShim\backups before each launch
BackupSaves=1
KeepBackups=10
; Diagnostics only: run the game under the shim's debugger and log details
DebugChild=0
"@
        Write-Host "Wrote $iniPath" -ForegroundColor Green
    }

    Write-Host ""
    Write-Host ("Shim {0} {1}." -f $version, $(if ($isUpdate) { "updated" } else { "installed" })) -ForegroundColor Green
    Write-Host "Launch GTA Vice City from the Rockstar Games Launcher as usual: your modded game will start."
    Write-Host "Hold Shift while clicking Play to start the original game instead."
    Write-Host "Log: $env:LOCALAPPDATA\VCShim\shim.log"
} catch {
    Write-Host ""
    Write-Host "Install failed: $($_.Exception.Message)" -ForegroundColor Red
    $code = 1
}

if ($PauseAtEnd) {
    Write-Host ""
    Read-Host "Press Enter to close" | Out-Null
}
exit $code
