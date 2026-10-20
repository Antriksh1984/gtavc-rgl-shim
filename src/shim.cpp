#include "shim.h"
#include "backup.h"
#include "config.h"
#include "debugwatch.h"
#include "log.h"
#include "version.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Set in our environment before CreateProcess so the child inherits it;
// DllMain skips hooking when it sees it (guards against relaunch loops).
static const wchar_t kChildEnv[] = L"VCSHIM_CHILD";
static const wchar_t kCompatEnv[] = L"__COMPAT_LAYER";

static std::wstring ModuleDir(HMODULE mod)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
    return fs::path(std::wstring(path, n)).parent_path().wstring();
}

// Logs top-level windows owned by this (official gta-vc.exe) process. A window
// on a thread that stops pumping messages can deadlock broadcast SendMessage.
static void LogOwnWindows()
{
    EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId()) {
            wchar_t cls[128] = L"", title[128] = L"";
            GetClassNameW(hwnd, cls, 128);
            GetWindowTextW(hwnd, title, 128);
            Log(L"Own window %p class '%s' title '%s' thread %lu%s%s", hwnd, cls, title, tid,
                tid == GetCurrentThreadId() ? L" (main thread)" : L"",
                IsWindowVisible(hwnd) ? L" visible" : L"");
        }
        return TRUE;
    }, 0);
}

// Waits for the process while still dispatching window messages, so any
// window owned by this thread keeps answering SendMessage from the game.
static void WaitPumpingMessages(HANDLE process)
{
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &process, FALSE, INFINITE, QS_ALLINPUT);
        if (r != WAIT_OBJECT_0 + 1)
            return;  // process exited (or the wait failed)
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// Returns only on bypass or failure; on success it ends the process.
static void RunShim()
{
    std::wstring dllDir = ModuleDir(g_module);
    LogInit(dllDir + L"\\shim.log");
    Log(L"==== VC shim v%s start, pid %lu", SHIM_VERSION_WSTR, GetCurrentProcessId());
    Log(L"Command line: %s", GetCommandLineW());

    // High bit of GetAsyncKeyState = key is down right now.
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        Log(L"Shift held: bypassing, running vanilla RGL game");
        return;
    }

    ShimConfig cfg;
    std::wstring error;
    if (!LoadConfig(dllDir + L"\\shim.ini", cfg, error)) {
        Log(L"ERROR: %s. Falling back to vanilla.", error.c_str());
        return;
    }
    if (!cfg.enabled) {
        Log(L"Enabled=0: running vanilla RGL game");
        return;
    }

    std::error_code ec;
    if (!fs::is_regular_file(cfg.moddedExe, ec)) {
        Log(L"ERROR: ModdedExe '%s' does not exist. Falling back to vanilla.", cfg.moddedExe.c_str());
        return;
    }
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (fs::equivalent(cfg.moddedExe, self, ec)) {
        Log(L"ERROR: ModdedExe points at this RGL exe itself. Falling back to vanilla.");
        return;
    }

    if (cfg.backupSaves) {
        std::wstring saves = cfg.saveFolder.empty() ? DefaultSaveFolder() : cfg.saveFolder;
        BackupSaveFolder(saves, cfg.keepBackups);  // warnings only, never blocks launch
    }

    // Job object: when our process dies (or the launcher kills it), the last
    // job handle closes and Windows kills the modded game with it.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                         &limits, sizeof(limits))) {
        Log(L"ERROR: job object setup failed (%lu). Falling back to vanilla.", GetLastError());
        if (job)
            CloseHandle(job);
        return;
    }

    std::wstring workDir = fs::path(cfg.moddedExe).parent_path().wstring();
    std::wstring cmd = L"\"" + cfg.moddedExe + L"\"";
    if (!cfg.extraArgs.empty())
        cmd += L" " + cfg.extraArgs;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');  // CreateProcessW may write into the command line

    // Names only (values may hold launcher tokens): shows what the child inherits.
    if (wchar_t* env = GetEnvironmentStringsW()) {
        std::wstring names;
        for (wchar_t* p = env; *p; p += wcslen(p) + 1) {
            const wchar_t* eq = wcschr(p + 1, L'=');  // +1: skip "=C:" style entries
            if (*p != L'=' && eq)
                names += std::wstring(p, size_t(eq - p)) + L" ";
        }
        FreeEnvironmentStringsW(env);
        Log(L"Inherited env vars: %s", names.c_str());
    }

    // Windows applies the RGL exe's compatibility layer (DWM8And16BitMitigation)
    // and records it in __COMPAT_LAYER, which children inherit and which
    // overrides their own compat settings. Clear it so the modded exe gets the
    // same layers as when launched directly.
    wchar_t compat[512] = L"";
    if (GetEnvironmentVariableW(kCompatEnv, compat, 512) > 0) {
        Log(L"Clearing inherited %s=%s for the child", kCompatEnv, compat);
        SetEnvironmentVariableW(kCompatEnv, nullptr);
    }

    // Children inherit the error mode; log it in case it hides crash dialogs.
    Log(L"Error mode: 0x%X", GetErrorMode());

    SetEnvironmentVariableW(kChildEnv, L"1");
    DWORD flags = CREATE_SUSPENDED;
    if (cfg.debugChild) {
        // Diagnostic mode: we become the game's debugger. _NO_DEBUG_HEAP keeps
        // Windows from switching the game to the slower debug heap.
        flags |= DEBUG_ONLY_THIS_PROCESS;
        SetEnvironmentVariableW(L"_NO_DEBUG_HEAP", L"1");
        Log(L"DebugChild=1: running the game under the shim's debugger");
    }
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    // Start suspended so it is in the job before it runs a single instruction.
    if (!CreateProcessW(cfg.moddedExe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                        flags, nullptr, workDir.c_str(), &si, &pi)) {
        Log(L"ERROR: CreateProcess('%s') failed (%lu). Falling back to vanilla.",
            cfg.moddedExe.c_str(), GetLastError());
        SetEnvironmentVariableW(kChildEnv, nullptr);
        if (compat[0])
            SetEnvironmentVariableW(kCompatEnv, compat);
        CloseHandle(job);
        return;
    }
    Log(L"Launched '%s' (pid %lu), cwd '%s'", cmd.c_str(), pi.dwProcessId, workDir.c_str());

    if (!AssignProcessToJobObject(job, pi.hProcess))
        Log(L"WARN: AssignProcessToJobObject failed (%lu); the game won't be killed with us",
            GetLastError());
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    AllowSetForegroundWindow(pi.dwProcessId);  // let the game take focus from us

    LogOwnWindows();

    // From here the launcher just sees this process running until the game ends.
    DWORD exitCode = 0;
    if (cfg.debugChild) {
        exitCode = RunDebugLoop();
    } else {
        WaitPumpingMessages(pi.hProcess);
        GetExitCodeProcess(pi.hProcess, &exitCode);
    }

    // More than one process in the job means the game started a child process.
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acct = {};
    if (QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &acct, sizeof(acct), nullptr))
        Log(L"Job saw %lu process(es) in total", acct.TotalProcesses);
    Log(L"Modded game exited with code %lu; exiting with the same code", exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(job);
    ExitProcess(exitCode);
}

extern "C" void __cdecl ShimMain()
{
    try {
        RunShim();
    } catch (...) {
        Log(L"ERROR: unexpected C++ exception. Falling back to vanilla.");
    }
    // Bypass or failure: put the original bytes back; the thunk then jumps there.
    RestoreEntryBytes();
    Log(L"Original entry point restored, handing over to RGL gta-vc.exe");
}
