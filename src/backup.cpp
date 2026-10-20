#include "backup.h"
#include "log.h"

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static std::wstring KnownFolder(REFKNOWNFOLDERID id)
{
    // Resolves redirected folders too (e.g. Documents moved to OneDrive).
    PWSTR p = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)))
        result = p;
    CoTaskMemFree(p);
    return result;
}

std::wstring DefaultSaveFolder()
{
    std::wstring docs = KnownFolder(FOLDERID_Documents);
    return docs.empty() ? docs : docs + L"\\GTA Vice City User Files";
}

// Matches our own backup folder names, YYYY-MM-DD_HH-MM-SS (optionally _N),
// so pruning can never touch anything we didn't create.
static bool IsBackupName(const std::wstring& n)
{
    static const wchar_t pattern[] = L"dddd-dd-dd_dd-dd-dd";
    if (n.size() < 19)
        return false;
    for (size_t i = 0; i < 19; ++i) {
        if (pattern[i] == L'd' ? !iswdigit(n[i]) : n[i] != pattern[i])
            return false;
    }
    return n.size() == 19 || n[19] == L'_';
}

static void Prune(const fs::path& root, int keep)
{
    std::error_code ec;
    std::vector<fs::path> backups;
    for (auto& e : fs::directory_iterator(root, ec)) {
        if (e.is_directory(ec) && IsBackupName(e.path().filename().wstring()))
            backups.push_back(e.path());
    }
    if (int(backups.size()) <= keep)
        return;

    // Names sort chronologically; delete the oldest ones.
    std::sort(backups.begin(), backups.end());
    for (size_t i = 0; i < backups.size() - size_t(keep); ++i) {
        fs::remove_all(backups[i], ec);
        if (ec)
            Log(L"WARN: could not prune %s: %S", backups[i].c_str(), ec.message().c_str());
        else
            Log(L"Pruned old backup %s", backups[i].c_str());
    }
}

bool BackupSaveFolder(const std::wstring& saveFolder, int keep)
{
    std::error_code ec;
    if (saveFolder.empty() || !fs::is_directory(saveFolder, ec)) {
        Log(L"WARN: save folder '%s' not found, skipping backup", saveFolder.c_str());
        return false;
    }

    std::wstring local = KnownFolder(FOLDERID_LocalAppData);
    if (local.empty()) {
        Log(L"WARN: cannot resolve LocalAppData, skipping backup");
        return false;
    }
    fs::path root = fs::path(local) / L"VCShim" / L"backups";
    fs::create_directories(root, ec);

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04u-%02u-%02u_%02u-%02u-%02u",
               t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    fs::path dest = root / stamp;
    for (int n = 2; fs::exists(dest, ec); ++n)
        dest = root / (std::wstring(stamp) + L"_" + std::to_wstring(n));

    // Recursive copy: reads the saves, writes only under dest.
    fs::copy(saveFolder, dest, fs::copy_options::recursive, ec);
    if (ec) {
        Log(L"WARN: backup to %s failed: %S", dest.c_str(), ec.message().c_str());
        return false;
    }

    size_t files = 0;
    for (auto& e : fs::recursive_directory_iterator(dest, ec))
        files += e.is_regular_file(ec) ? 1 : 0;
    Log(L"Backed up %zu file(s) from '%s' to %s", files, saveFolder.c_str(), dest.c_str());

    if (keep > 0)
        Prune(root, keep);
    return true;
}
