@echo off
setlocal
title GTA Vice City Launcher Shim - Uninstall

rem Usage: Uninstall.bat ["<Rockstar Launcher GTA Vice City folder>"]

rem Full path to the uninstaller, worked out here and handed to PowerShell.
set "PS1=%~dp0files\uninstall.ps1"
if not exist "%PS1%" goto :missing

rem Optional folder; a trailing backslash would escape the closing quote later.
set "SHIM_GAMEDIR=%~1"
if defined SHIM_GAMEDIR call :trimslash

rem fltmc only succeeds with admin rights: if we have them, run right here.
fltmc >nul 2>&1 && goto :elevated

rem Removing files from the Rockstar Launcher game folder needs admin rights.
echo Requesting administrator rights...
powershell -NoProfile -Command "$a = '-NoProfile -ExecutionPolicy Bypass -File \"' + $env:PS1 + '\" -PauseAtEnd'; if ($env:SHIM_GAMEDIR) { $a += ' -GameDir \"' + $env:SHIM_GAMEDIR + '\"' }; Start-Process powershell -Verb RunAs -ArgumentList $a"
if errorlevel 1 goto :denied
exit /b 0

:elevated
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -PauseAtEnd -GameDir "%SHIM_GAMEDIR%"
exit /b %errorlevel%

:denied
echo.
echo  Administrator rights were not granted, so nothing was removed.
echo.
pause
exit /b 1

:missing
echo.
echo  Can't find the uninstaller files at:
echo    %PS1%
echo.
echo  Extract the whole zip to a folder first, then run Uninstall.bat from there.
echo.
pause
exit /b 1

:trimslash
rem Separate routine: cmd expands a whole line before running it, so the
rem substring below would be a syntax error if SHIM_GAMEDIR were empty.
if "%SHIM_GAMEDIR:~-1%"=="\" set "SHIM_GAMEDIR=%SHIM_GAMEDIR:~0,-1%"
exit /b
