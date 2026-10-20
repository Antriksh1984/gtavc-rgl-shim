#include "config.h"

#include <windows.h>

static std::wstring ReadString(const std::wstring& ini, const wchar_t* key)
{
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"Shim", key, L"", buf, 1024, ini.c_str());
    std::wstring s = buf;

    // Trim whitespace and optional surrounding quotes.
    size_t a = s.find_first_not_of(L" \t");
    size_t b = s.find_last_not_of(L" \t");
    s = (a == std::wstring::npos) ? L"" : s.substr(a, b - a + 1);
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
        s = s.substr(1, s.size() - 2);
    return s;
}

static std::wstring ExpandPath(const std::wstring& s)
{
    if (s.empty())
        return s;
    wchar_t buf[MAX_PATH * 2];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, MAX_PATH * 2);
    return (n == 0 || n > MAX_PATH * 2) ? s : std::wstring(buf);
}

bool LoadConfig(const std::wstring& iniPath, ShimConfig& out, std::wstring& error)
{
    // GetPrivateProfile* silently returns defaults for a missing file, so check first.
    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"shim.ini not found at " + iniPath;
        return false;
    }

    const wchar_t* ini = iniPath.c_str();
    out.enabled     = GetPrivateProfileIntW(L"Shim", L"Enabled", 1, ini) != 0;
    out.backupSaves = GetPrivateProfileIntW(L"Shim", L"BackupSaves", 1, ini) != 0;
    out.keepBackups = int(GetPrivateProfileIntW(L"Shim", L"KeepBackups", 10, ini));
    out.debugChild  = GetPrivateProfileIntW(L"Shim", L"DebugChild", 0, ini) != 0;
    out.moddedExe   = ExpandPath(ReadString(iniPath, L"ModdedExe"));
    out.extraArgs   = ReadString(iniPath, L"ExtraArgs");
    out.saveFolder  = ExpandPath(ReadString(iniPath, L"SaveFolder"));

    if (out.enabled && out.moddedExe.empty()) {
        error = L"ModdedExe is empty in " + iniPath;
        return false;
    }
    return true;
}
