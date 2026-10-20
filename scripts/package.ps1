<#
.SYNOPSIS
  Builds the Release DLL and packages a ready-to-share zip in dist\:

    VC-Shim-v<version>\
      Install.bat
      Uninstall.bat
      README.txt
      files\dinput8.dll, install.ps1, uninstall.ps1, common.ps1
    VC-Shim-v<version>.zip (+ .sha256.txt)

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\package.ps1
#>
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
. (Join-Path $PSScriptRoot "common.ps1")

# --- Build (x86 Release) ------------------------------------------------------
$build = Join-Path $root "build"
cmake -S $root -B $build -A Win32 | Out-Null
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

$dll = Join-Path $build "Release\dinput8.dll"
if ((Get-PeMachine $dll) -ne 0x14C) { throw "dinput8.dll is not x86; GTA Vice City needs a 32-bit DLL." }
$version = (Get-Item -LiteralPath $dll).VersionInfo.FileVersion
$name = "VC-Shim-v$version"

# --- Assemble the release folder -----------------------------------------------
$dist = Join-Path $root "dist"
$out = Join-Path $dist $name
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $out "files") -Force | Out-Null

# Batch files need CRLF; README gets the version filled in.
function Copy-Text([string]$Src, [string]$Dst, [Text.Encoding]$Enc, [hashtable]$Vars = @{}) {
    $text = [IO.File]::ReadAllText($Src) -replace "`r?`n", "`r`n"
    foreach ($k in $Vars.Keys) { $text = $text.Replace("{$k}", $Vars[$k]) }
    [IO.File]::WriteAllText($Dst, $text, $Enc)
}
$ascii = New-Object Text.UTF8Encoding($false)
$utf8Bom = New-Object Text.UTF8Encoding($true)  # PowerShell 5.1 reads BOM-less .ps1 as ANSI

$pkg = Join-Path $root "packaging"
Copy-Text (Join-Path $pkg "Install.bat")   (Join-Path $out "Install.bat")   $ascii
Copy-Text (Join-Path $pkg "Uninstall.bat") (Join-Path $out "Uninstall.bat") $ascii
Copy-Text (Join-Path $pkg "README.txt")    (Join-Path $out "README.txt")    $ascii @{ VERSION = $version }
foreach ($s in "install.ps1", "uninstall.ps1", "common.ps1") {
    Copy-Text (Join-Path $PSScriptRoot $s) (Join-Path $out "files\$s") $utf8Bom
}
Copy-Item -LiteralPath $dll -Destination (Join-Path $out "files\dinput8.dll")

# --- Zip + checksum ---------------------------------------------------------------
$zip = Join-Path $dist "$name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path $out -DestinationPath $zip
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
"$hash  $name.zip" | Set-Content -LiteralPath "$zip.sha256.txt" -Encoding ascii

Write-Host ""
Write-Host "Packaged $name" -ForegroundColor Green
Write-Host "  Folder: $out"
Write-Host "  Zip:    $zip"
Write-Host "  SHA256: $hash"
