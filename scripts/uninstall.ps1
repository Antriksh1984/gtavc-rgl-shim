<#
.SYNOPSIS
  Removes the files the installer added (dinput8.dll if it's the shim, shim.ini)
  from the Rockstar Games Launcher GTA Vice City folder. Run elevated (Uninstall.bat
  does this for you). Logs and save backups in %LOCALAPPDATA%\VCShim are kept.
#>
param(
    [string]$GameDir,
    [switch]$RemoveLogs, # also delete shim.log / shim.log.old in the game folder
    [switch]$PauseAtEnd  # keep the window open at the end (used by Uninstall.bat)
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Host.UI.RawUI.WindowTitle = "GTA Vice City Launcher Shim - Uninstall"

$code = 0
try {
    Write-Host ""
    Write-Host "  GTA Vice City Launcher Shim - Uninstall" -ForegroundColor Cyan
    Write-Host "  =======================================" -ForegroundColor Cyan
    Write-Host ""

    Assert-GameNotRunning

    $GameDir = Resolve-GameDir $GameDir "Remove the shim from there?"

    $removed = 0
    $dll = Join-Path $GameDir "dinput8.dll"
    if (Test-Path -LiteralPath $dll) {
        if (Test-IsShim $dll) {
            Remove-Item -LiteralPath $dll
            Write-Host "Removed $dll" -ForegroundColor Green
            $removed++
        } else {
            Write-Warning "$dll is not the shim; left it alone."
        }
    }

    $files = @("shim.ini")
    if ($RemoveLogs) { $files += "shim.log", "shim.log.old" }
    foreach ($f in $files) {
        $p = Join-Path $GameDir $f
        if (Test-Path -LiteralPath $p) {
            Remove-Item -LiteralPath $p
            Write-Host "Removed $p" -ForegroundColor Green
            $removed++
        }
    }

    Write-Host ""
    if ($removed -eq 0) {
        Write-Host "Nothing to remove: the shim isn't installed in '$GameDir'."
    } else {
        Write-Host "Shim removed. The Rockstar Launcher will start the original game again." -ForegroundColor Green
    }
    Write-Host "Your save backups are kept in $env:LOCALAPPDATA\VCShim\backups (delete them by hand if you like)."
} catch {
    Write-Host ""
    Write-Host "Uninstall failed: $($_.Exception.Message)" -ForegroundColor Red
    $code = 1
}

if ($PauseAtEnd) {
    Write-Host ""
    Read-Host "Press Enter to close" | Out-Null
}
exit $code
