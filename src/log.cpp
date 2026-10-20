#include "log.h"

#include <windows.h>
#include <shlobj.h>
#include <cstdarg>
#include <cstdio>

static std::wstring g_logPath;

static bool CanAppend(const std::wstring& path)
{
    // OPEN_ALWAYS creates the file if missing; FILE_APPEND_DATA only appends.
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(h);
    return true;
}

void LogInit(const std::wstring& path)
{
    // Keep the log small: past 1 MB, move it aside and start fresh.
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        (info.nFileSizeHigh > 0 || info.nFileSizeLow > 1024 * 1024))
        MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);

    if (CanAppend(path)) {
        g_logPath = path;
        return;
    }

    // Game folder not writable (e.g. under Program Files): use LocalAppData.
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        std::wstring dir = std::wstring(local) + L"\\VCShim";
        CoTaskMemFree(local);
        CreateDirectoryW(dir.c_str(), nullptr);
        if (CanAppend(dir + L"\\shim.log"))
            g_logPath = dir + L"\\shim.log";
    }
}

void Log(const wchar_t* fmt, ...)
{
    if (g_logPath.empty())
        return;

    wchar_t msg[2048];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[2200];
    _snwprintf_s(line, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u.%03u  %s\r\n",
                 t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                 t.wMilliseconds, msg);

    // Log as UTF-8 so non-ASCII paths stay readable.
    char utf8[6600];
    int len = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (len <= 1)
        return;

    HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(h, utf8, DWORD(len - 1), &written, nullptr);
    CloseHandle(h);
}
