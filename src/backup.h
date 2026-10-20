#pragma once
#include <string>

// Documents\GTA Vice City User Files (empty string if Documents can't be resolved).
std::wstring DefaultSaveFolder();

// Copies saveFolder to %LOCALAPPDATA%\VCShim\backups\<YYYY-MM-DD_HH-MM-SS>\
// and prunes to the `keep` newest backups (0 = keep all). Only reads the save
// folder; never modifies it. Logs its own warnings; returns false on failure.
bool BackupSaveFolder(const std::wstring& saveFolder, int keep);
