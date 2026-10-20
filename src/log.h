#pragma once
#include <string>

// Opens shim.log at `path` (falls back to %LOCALAPPDATA%\VCShim\shim.log if
// that folder isn't writable). Rotates to shim.log.old once it passes 1 MB.
void LogInit(const std::wstring& path);

// printf-style, one timestamped UTF-8 line per call.
void Log(const wchar_t* fmt, ...);
