@echo off
setlocal
title GTA Vice City Launcher Shim - Install

rem Usage: Install.bat ["<Rockstar Launcher GTA Vice City folder>"] ["<modded gta-vc.exe>"]
rem Without arguments the installer finds the folder itself or lets you pick.

rem Full path to the installer, worked out here (not in the elevated process,
rem which starts in System32) and handed to PowerShell as-is.
set "PS1=%~dp0files\install.ps1"
if not exist "%PS1%" goto :missing

rem Optional paths; a trailing backslash would escape the closing quote later.
set "SHIM_GAMEDIR=%~1"
set "SHIM_MODDEDEXE=%~2"
if defined SHIM_GAMEDIR call :trimslash

rem fltmc only succeeds with admin rights: if we have them, run right here.
fltmc >nul 2>&1 && goto :elevated

rem The Rockstar Launcher game folder is read-only for normal users, so start
rem the installer in a new elevated PowerShell window (UAC prompt). The
rem elevated process gets a fresh environment, so paths go on its command line.
echo Requesting administrator rights...
powershell -NoProfile -Command "$a = '-NoProfile -ExecutionPolicy Bypass -File \"' + $env:PS1 + '\" -PauseAtEnd'; if ($env:SHIM_GAMEDIR) { $a += ' -GameDir \"' + $env:SHIM_GAMEDIR + '\"' }; if ($env:SHIM_MODDEDEXE) { $a += ' -ModdedExe \"' + $env:SHIM_MODDEDEXE + '\"' }; Start-Process powershell -Verb RunAs -ArgumentList $a"
if errorlevel 1 goto :denied
exit /b 0

:elevated
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -PauseAtEnd -GameDir "%SHIM_GAMEDIR%" -ModdedExe "%SHIM_MODDEDEXE%"
exit /b %errorlevel%

:denied
echo.
echo  Administrator rights were not granted, so nothing was installed.
echo.
pause
exit /b 1

:missing
echo.
echo  Can't find the installer files at:
echo    %PS1%
echo.
echo  Extract the whole zip to a folder first, then run Install.bat from there.
echo.
pause
exit /b 1

:trimslash
rem Separate routine: cmd expands a whole line before running it, so the
rem substring below would be a syntax error if SHIM_GAMEDIR were empty.
if "%SHIM_GAMEDIR:~-1%"=="\" set "SHIM_GAMEDIR=%SHIM_GAMEDIR:~0,-1%"
exit /b
