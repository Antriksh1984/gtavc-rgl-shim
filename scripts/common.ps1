# Shared helpers for install.ps1 / uninstall.ps1 (dot-sourced).

# Our DLL contains this UTF-16 marker; any other dinput8.dll belongs to something else.
function Test-IsShim([string]$Path) {
    # Latin-1 maps bytes 1:1, so this is a plain byte search at any alignment.
    $latin1 = [Text.Encoding]::GetEncoding(28591)
    $marker = $latin1.GetString([Text.Encoding]::Unicode.GetBytes("VCSHIM_CHILD"))
    return $latin1.GetString([IO.File]::ReadAllBytes($Path)).Contains($marker)
}

# The Rockstar Games Launcher records the install folder in the registry. The
# key names contain a colon, hence -LiteralPath.
function Find-RglGameDir {
    $keys = @(
        @{ Path = "HKLM:\SOFTWARE\WOW6432Node\Rockstar Games\Grand Theft Auto: Vice City"; Name = "InstallFolder" },
        @{ Path = "HKLM:\SOFTWARE\Rockstar Games\Grand Theft Auto: Vice City"; Name = "InstallFolder" },
        @{ Path = "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Grand Theft Auto: Vice City"; Name = "InstallLocation" }
    )
    foreach ($k in $keys) {
        $v = (Get-ItemProperty -LiteralPath $k.Path -Name $k.Name -ErrorAction SilentlyContinue).($k.Name)
        if ($v -and (Test-Path -LiteralPath (Join-Path $v "gta-vc.exe"))) { return $v.TrimEnd('\') }
    }
    return $null
}

function Read-YesNo([string]$Question, [bool]$Default = $true) {
    $hint = if ($Default) { "[Y/n]" } else { "[y/N]" }
    $answer = Read-Host "$Question $hint"
    if ([string]::IsNullOrWhiteSpace($answer)) { return $Default }
    return $answer.Trim().ToLower().StartsWith("y")
}

# File picker for an exe; returns $null if cancelled or no dialog can be shown.
function Select-Exe([string]$Title, [string]$InitialDir) {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = $Title
        $dlg.Filter = "GTA Vice City executable (gta-vc.exe)|gta-vc.exe|Executables (*.exe)|*.exe"
        if ($InitialDir -and (Test-Path -LiteralPath $InitialDir)) { $dlg.InitialDirectory = $InitialDir }
        # A top-most owner makes the dialog open in front of the console window.
        $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true }
        try {
            if ($dlg.ShowDialog($owner) -eq [System.Windows.Forms.DialogResult]::OK) { return $dlg.FileName }
        } finally { $owner.Dispose() }
    } catch {
        Write-Warning "Couldn't open a file dialog; type the path instead."
    }
    return $null
}

# Accepts a folder or a path to an exe inside it; strips quotes and trailing slashes.
function ConvertTo-Folder([string]$Path) {
    $Path = "$Path".Trim().Trim('"').Trim().TrimEnd('\')
    if ($Path -match '\.exe$' -and (Test-Path -LiteralPath $Path -PathType Leaf)) { $Path = Split-Path $Path -Parent }
    return $Path
}

# Error message if $Dir can't be the Rockstar Launcher GTA Vice City folder, else $null.
function Test-GameDir([string]$Dir) {
    if (-not $Dir) { return "No folder given." }
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { return "Folder '$Dir' not found." }
    if (-not (Test-Path -LiteralPath (Join-Path $Dir "gta-vc.exe"))) { return "No gta-vc.exe in '$Dir'." }
    return $null
}

# Finds the Rockstar Launcher GTA Vice City folder: $Initial if given, else the
# registry (confirmed by the user), else a browse/type menu until it's valid.
# $Action words the confirmation ("Install the shim there?").
function Resolve-GameDir([string]$Initial, [string]$Action, [switch]$CheckRgl) {
    $dir = $null
    if ($Initial) {
        $dir = ConvertTo-Folder $Initial
        $err = Test-GameDir $dir
        if ($err) { Write-Warning $err; $dir = $null }
    } else {
        $found = Find-RglGameDir
        if ($found) {
            Write-Host "Found Rockstar Launcher GTA Vice City at:"
            Write-Host "  $found" -ForegroundColor Yellow
            if (Read-YesNo $Action) { $dir = $found }
        } else {
            Write-Host "Couldn't find the Rockstar Launcher GTA Vice City folder automatically." -ForegroundColor Yellow
        }
    }

    while ($true) {
        if (-not $dir) {
            Write-Host ""
            Write-Host "Choose the Rockstar Launcher GTA Vice City folder (the one with the official gta-vc.exe):"
            Write-Host "  [1] Browse for its gta-vc.exe"
            Write-Host "  [2] Type or paste the folder path"
            Write-Host "  [Q] Quit"
            switch ((Read-Host "Choice").Trim().ToLower()) {
                "1" { $p = Select-Exe "Select the official gta-vc.exe in the Rockstar Launcher GTA Vice City folder" ""
                      if ($p) { $dir = Split-Path $p -Parent } }
                "2" { $dir = ConvertTo-Folder (Read-Host "Folder path") }
                "q" { throw "Cancelled." }
                default { Write-Warning "Type 1, 2 or Q." }
            }
            if (-not $dir) { continue }
            $err = Test-GameDir $dir
            if ($err) { Write-Warning $err; $dir = $null; continue }
        }
        $dir = (Resolve-Path -LiteralPath $dir).Path
        # title.rgl marks a Rockstar Launcher install; anything else is suspicious.
        if ($CheckRgl -and -not (Test-Path -LiteralPath (Join-Path $dir "title.rgl"))) {
            Write-Warning "'$dir' doesn't look like a Rockstar Games Launcher install (no title.rgl)."
            if (-not (Read-YesNo "Use it anyway?" $false)) { $dir = $null; continue }
        }
        return $dir
    }
}

# Error message if $Exe can't be the modded game, else $null. Warns about
# harmless-but-wrong setups.
function Test-ModdedExe([string]$Exe, [string]$GameDir) {
    if (-not $Exe) { return "No file given." }
    if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { return "File '$Exe' not found." }
    $folder = Split-Path (Resolve-Path -LiteralPath $Exe).Path -Parent
    if ($folder -eq $GameDir) { return "That's the Rockstar Launcher game itself. Pick the modded copy in its own folder." }
    if ((Get-PeMachine $Exe) -ne 0x14C) { return "'$Exe' is not a 32-bit Windows program (GTA Vice City 1.0 is 32-bit)." }
    $dinput = Join-Path $folder "dinput8.dll"
    if (-not $script:warnedStrayShim -and (Test-Path -LiteralPath $dinput) -and (Test-IsShim $dinput)) {
        $script:warnedStrayShim = $true
        Write-Warning "The modded folder contains a copy of this shim's dinput8.dll. It isn't needed there; remove it."
    }
    return $null
}

# Finds the modded exe: $Initial if given, else offers to keep $Current (from
# an existing shim.ini), else a browse/type menu until it's valid.
function Resolve-ModdedExe([string]$Initial, [string]$Current, [string]$GameDir) {
    $exe = $null
    if ($Initial) {
        $exe = $Initial.Trim().Trim('"')
    } elseif ($Current -and -not (Test-ModdedExe $Current $GameDir)) {
        Write-Host "Current modded game: $Current" -ForegroundColor Yellow
        if (Read-YesNo "Keep using it?") { return $Current }
    }

    while ($true) {
        if ($exe) {
            # A folder was given: look for gta-vc.exe inside it.
            if (Test-Path -LiteralPath $exe -PathType Container) { $exe = Join-Path $exe "gta-vc.exe" }
            $err = Test-ModdedExe $exe $GameDir
            if (-not $err) { return (Resolve-Path -LiteralPath $exe).Path }
            Write-Warning $err
        }
        Write-Host ""
        Write-Host "Choose your modded/downgraded 1.0 gta-vc.exe:"
        Write-Host "  [1] Browse for it"
        Write-Host "  [2] Type or paste its path (the exe or its folder)"
        Write-Host "  [Q] Quit"
        switch ((Read-Host "Choice").Trim().ToLower()) {
            "1" { $exe = Select-Exe "Select your modded 1.0 gta-vc.exe" (Split-Path $GameDir -Parent) }
            "2" { $exe = (Read-Host "Path").Trim().Trim('"') }
            "q" { throw "Cancelled." }
            default { Write-Warning "Type 1, 2 or Q."; $exe = $null }
        }
    }
}

# Machine type from the PE header: 0x14C = x86 (32-bit), 0x8664 = x64.
function Get-PeMachine([string]$Path) {
    $fs = [IO.File]::OpenRead($Path)
    try {
        $br = New-Object IO.BinaryReader($fs)
        if ($br.ReadUInt16() -ne 0x5A4D) { return $null }   # "MZ"
        $fs.Position = 0x3C
        $fs.Position = $br.ReadInt32()
        if ($br.ReadUInt32() -ne 0x00004550) { return $null } # "PE\0\0"
        return $br.ReadUInt16()
    } finally { $fs.Close() }
}

# The shim DLL is locked while either game is running.
function Assert-GameNotRunning {
    $running = Get-Process -Name gta-vc -ErrorAction SilentlyContinue
    if ($running) { throw "GTA Vice City is running. Quit the game first, then run this again." }
}

# shim.ini is UTF-16 so GetPrivateProfileStringW reads non-ASCII paths correctly.
function Read-Ini([string]$Path) { return [IO.File]::ReadAllText($Path) }
function Write-Ini([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.Encoding]::Unicode)
}
function Get-IniValue([string]$Text, [string]$Key) {
    $m = [regex]::Match($Text, "(?m)^\s*$Key\s*=\s*(.*?)\s*$")
    if ($m.Success) { return $m.Groups[1].Value } else { return $null }
}
