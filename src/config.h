#pragma once
#include <string>

struct ShimConfig {
    bool enabled = true;
    std::wstring moddedExe;   // full path to the 1.0 gta-vc.exe
    std::wstring extraArgs;   // appended to the modded exe's command line
    std::wstring saveFolder;  // empty = Documents\GTA Vice City User Files
    bool backupSaves = true;
    int keepBackups = 10;     // 0 = keep all
    bool debugChild = false;  // diagnostic: run the game under a logging debugger
};

// Reads [Shim] from iniPath. Returns false (with `error` set) if the file is
// missing or ModdedExe is empty; path values get %ENV% expansion.
bool LoadConfig(const std::wstring& iniPath, ShimConfig& out, std::wstring& error);
