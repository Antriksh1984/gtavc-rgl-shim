#include "debugwatch.h"
#include "log.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct Module {
    std::wstring name;
    DWORD size = 0;
};
std::map<DWORD_PTR, Module> g_modules;      // base address -> module
std::map<DWORD, HANDLE> g_threads;          // thread id -> handle (owned by the system)
HANDLE g_process = nullptr;

// Software breakpoints on the functions a game uses to quit; each logs up to
// maxHits calls, re-arming itself after a single step.
struct Breakpoint {
    const wchar_t* dll;
    const char* func;
    int maxHits;
    DWORD_PTR addr = 0;
    BYTE original = 0;
    bool armed = false;
    int hits = 0;
};
Breakpoint g_bps[] = {
    {L"kernel32.dll", "ExitProcess", 1},
    {L"user32.dll", "PostQuitMessage", 5},
    {L"user32.dll", "DestroyWindow", 10},
};
std::map<DWORD, Breakpoint*> g_rearm;  // thread id -> breakpoint to re-insert after one step

// GTA Vice City 1.0: RsGlobal is at 0x9B48D8 { appName, width, height, maxWidth,
// maxHeight, maxFPS, quit, ... } (RsInitialise stores the "GTA: Vice City"
// appName there). The main loop exits when `quit` becomes non-zero, so a write
// watch shows who set it.
const DWORD_PTR kRsGlobal = 0x9B48D8;
const DWORD_PTR kQuitFlag = kRsGlobal + 0x18;
int g_quitHits = 0;

std::wstring FileName(HANDLE file)
{
    wchar_t path[MAX_PATH] = L"?";
    if (file)
        GetFinalPathNameByHandleW(file, path, MAX_PATH, FILE_NAME_NORMALIZED);
    const wchar_t* slash = wcsrchr(path, L'\\');
    return slash ? slash + 1 : path;
}

template <typename T>
bool ReadMem(DWORD_PTR addr, T& out)
{
    SIZE_T got = 0;
    return ReadProcessMemory(g_process, LPCVOID(addr), &out, sizeof(T), &got) && got == sizeof(T);
}

DWORD ImageSize(DWORD_PTR base)
{
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS nt;
    if (!ReadMem(base, dos) || !ReadMem(base + dos.e_lfanew, nt))
        return 0;
    return nt.OptionalHeader.SizeOfImage;
}

void AddModule(DWORD_PTR base, HANDLE file)
{
    g_modules[base] = {FileName(file), ImageSize(base)};
    if (file)
        CloseHandle(file);
}

// "module+0xOFFSET", or empty if the address isn't inside a known module.
std::wstring Describe(DWORD_PTR addr)
{
    auto it = g_modules.upper_bound(addr);
    if (it == g_modules.begin())
        return L"";
    --it;
    if (it->second.size && addr - it->first >= it->second.size)
        return L"";
    wchar_t buf[300];
    swprintf_s(buf, L"%s+0x%IX", it->second.name.c_str(), addr - it->first);
    return buf;
}

// Heuristic stack trace: every dword on the stack that points into a module.
void LogStack(DWORD_PTR esp)
{
    DWORD words[256] = {};
    SIZE_T got = 0;
    ReadProcessMemory(g_process, LPCVOID(esp), words, sizeof(words), &got);
    int shown = 0;
    for (size_t i = 0; i < got / 4 && shown < 24; ++i) {
        std::wstring d = Describe(words[i]);
        if (!d.empty()) {
            Log(L"DEBUG:     [esp+0x%03X] %08X  %s", unsigned(i * 4), words[i], d.c_str());
            ++shown;
        }
    }
}

// Logs every game thread's current location and stack.
void DumpAllThreads(const wchar_t* why)
{
    Log(L"DEBUG: ==== thread dump (%s), %zu thread(s)", why, g_threads.size());
    for (auto& [tid, h] : g_threads) {
        SuspendThread(h);  // a running thread's context isn't reliable
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(h, &ctx)) {
            Log(L"DEBUG:   thread %lu at %s (eip %08X)", tid, Describe(ctx.Eip).c_str(), ctx.Eip);
            LogStack(ctx.Esp);
        }
        ResumeThread(h);
    }
    Log(L"DEBUG: ==== end of thread dump");
}

DWORD g_gamePid = 0;
bool g_hangDumped = false, g_bpDumped = false;

bool GetCtx(DWORD tid, CONTEXT& ctx, DWORD flags)
{
    auto it = g_threads.find(tid);
    if (it == g_threads.end())
        return false;
    ctx.ContextFlags = flags;
    return GetThreadContext(it->second, &ctx) != 0;
}

void WriteInt3(Breakpoint& bp)
{
    BYTE int3 = 0xCC;
    SIZE_T n;
    bp.armed = WriteProcessMemory(g_process, LPVOID(bp.addr), &int3, 1, &n) != 0;
    FlushInstructionCache(g_process, LPCVOID(bp.addr), 1);
}

void ArmBreakpoints()
{
    for (auto& bp : g_bps) {
        // System DLLs share one base address per boot, so our addresses match the game's.
        HMODULE mod = GetModuleHandleW(bp.dll);
        bp.addr = mod ? DWORD_PTR(GetProcAddress(mod, bp.func)) : 0;
        if (!bp.addr || !ReadMem(bp.addr, bp.original))
            continue;
        WriteInt3(bp);
        if (bp.armed)
            Log(L"DEBUG: breakpoint on %S at %08IX", bp.func, bp.addr);
    }
}

// Returns true if the breakpoint was one of ours (and has been handled).
bool HandleBreakpoint(DWORD tid, DWORD_PTR addr)
{
    for (auto& bp : g_bps) {
        if (!bp.armed || bp.addr != addr)
            continue;
        CONTEXT ctx;
        if (!GetCtx(tid, ctx, CONTEXT_FULL))
            return false;
        DWORD ret = 0, arg = 0;
        ReadMem(ctx.Esp, ret);
        ReadMem(ctx.Esp + 4, arg);
        Log(L"DEBUG: >>> %S(0x%X) called from %s, thread %lu", bp.func, arg,
            Describe(ret).c_str(), tid);
        LogStack(ctx.Esp);
        // First PostQuitMessage/ExitProcess (Alt+F4 ends in PostQuitMessage):
        // snapshot where every thread is.
        if (!g_bpDumped && strcmp(bp.func, "DestroyWindow") != 0) {
            g_bpDumped = true;
            DumpAllThreads(L"quit path");
        }

        // Put the original byte back and re-run that instruction. Unless this
        // was the last hit we want, single-step (TF) once and re-insert the int3.
        SIZE_T n;
        WriteProcessMemory(g_process, LPVOID(bp.addr), &bp.original, 1, &n);
        FlushInstructionCache(g_process, LPCVOID(bp.addr), 1);
        bp.armed = false;
        ctx.Eip = DWORD(bp.addr);
        if (++bp.hits < bp.maxHits) {
            ctx.EFlags |= 0x100;
            g_rearm[tid] = &bp;
        }
        SetThreadContext(g_threads[tid], &ctx);
        return true;
    }
    return false;
}

// Hardware write watch on RsGlobal.quit (DR0, 4 bytes), per thread.
void WatchQuitFlag(HANDLE thread)
{
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread, &ctx))
        return;
    ctx.Dr0 = kQuitFlag;
    ctx.Dr7 = (ctx.Dr7 & ~0x000F0003u) | 0x1 | (0x1u << 16) | (0x3u << 18);  // L0, write, 4 bytes
    SetThreadContext(thread, &ctx);
}

bool HandleSingleStep(DWORD tid)
{
    CONTEXT ctx;
    if (!GetCtx(tid, ctx, CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS))
        return false;

    // Our own trap step after a breakpoint hit: re-insert that breakpoint.
    auto re = g_rearm.find(tid);
    if (re != g_rearm.end()) {
        WriteInt3(*re->second);
        g_rearm.erase(re);
        if (!(ctx.Dr6 & 1))
            return true;
    }
    if (!(ctx.Dr6 & 1))
        return false;
    DWORD value = 0, width = 0, height = 0;
    ReadMem(kQuitFlag, value);
    ReadMem(kRsGlobal + 0x04, width);
    ReadMem(kRsGlobal + 0x08, height);
    if (++g_quitHits <= 20) {
        Log(L"DEBUG: >>> RsGlobal.quit = %lu written by %s, thread %lu (RsGlobal %lux%lu)",
            value, Describe(ctx.Eip).c_str(), tid, width, height);
        if (value != 0)
            LogStack(ctx.Esp);
    }
    ctx.Dr6 = 0;
    SetThreadContext(g_threads[tid], &ctx);
    return true;
}

// True if any visible top-level window of the game has stopped answering.
bool GameWindowHung()
{
    struct Ctx { DWORD pid; bool hung; } c = {g_gamePid, false};
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == c->pid && IsWindowVisible(hwnd) && IsHungAppWindow(hwnd))
            c->hung = true;
        return TRUE;
    }, LPARAM(&c));
    return c.hung;
}

// Work done while the debuggee is running and no event is pending.
void OnIdle()
{
    MSG msg;  // keep this thread's windows (if any) responsive
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    static ULONGLONG lastCheck = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastCheck < 1000)
        return;
    lastCheck = now;

    // Manual trigger: Ctrl+Shift+F12 while the game is frozen.
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
        (GetAsyncKeyState(VK_F12) & 0x8000))
        DumpAllThreads(L"Ctrl+Shift+F12");

    if (!g_hangDumped && GameWindowHung()) {
        g_hangDumped = true;
        DumpAllThreads(L"game window not responding");
    }
}

std::wstring ReadDebugString(const OUTPUT_DEBUG_STRING_INFO& info)
{
    WORD len = info.nDebugStringLength;
    if (len == 0 || len > 4096)
        return L"";
    std::wstring out;
    SIZE_T got = 0;
    if (info.fUnicode) {
        out.resize(len);
        ReadProcessMemory(g_process, info.lpDebugStringData, &out[0], len * 2, &got);
    } else {
        std::string a(len, '\0');
        ReadProcessMemory(g_process, info.lpDebugStringData, &a[0], len, &got);
        out.assign(a.begin(), a.end());
    }
    while (!out.empty() && (out.back() == L'\0' || out.back() == L'\n' || out.back() == L'\r'))
        out.pop_back();
    return out;
}

} // namespace

DWORD RunDebugLoop()
{
    int exceptions = 0, strings = 0;
    bool firstBreak = true;

    for (;;) {
        DEBUG_EVENT ev = {};
        // Short timeout so we can pump messages and watch for a hang in between.
        if (!WaitForDebugEvent(&ev, 100)) {
            if (GetLastError() == ERROR_SEM_TIMEOUT) {
                OnIdle();
                continue;
            }
            Log(L"DEBUG: WaitForDebugEvent failed (%lu)", GetLastError());
            return 1;
        }
        DWORD cont = DBG_CONTINUE;

        switch (ev.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            g_process = ev.u.CreateProcessInfo.hProcess;  // owned by the system
            g_gamePid = ev.dwProcessId;
            g_threads[ev.dwThreadId] = ev.u.CreateProcessInfo.hThread;
            AddModule(DWORD_PTR(ev.u.CreateProcessInfo.lpBaseOfImage), ev.u.CreateProcessInfo.hFile);
            WatchQuitFlag(ev.u.CreateProcessInfo.hThread);
            break;

        case CREATE_THREAD_DEBUG_EVENT:
            g_threads[ev.dwThreadId] = ev.u.CreateThread.hThread;
            WatchQuitFlag(ev.u.CreateThread.hThread);
            break;

        case EXIT_THREAD_DEBUG_EVENT:
            g_threads.erase(ev.dwThreadId);
            break;

        case LOAD_DLL_DEBUG_EVENT:
            AddModule(DWORD_PTR(ev.u.LoadDll.lpBaseOfDll), ev.u.LoadDll.hFile);
            break;

        case EXCEPTION_DEBUG_EVENT: {
            const EXCEPTION_RECORD& er = ev.u.Exception.ExceptionRecord;
            DWORD_PTR at = DWORD_PTR(er.ExceptionAddress);
            if (er.ExceptionCode == EXCEPTION_BREAKPOINT) {
                // The loader's initial breakpoint: all static imports are loaded now.
                if (firstBreak) {
                    firstBreak = false;
                    ArmBreakpoints();
                    break;
                }
                if (HandleBreakpoint(ev.dwThreadId, at))
                    break;
            }
            if (er.ExceptionCode == EXCEPTION_SINGLE_STEP && HandleSingleStep(ev.dwThreadId))
                break;

            // Let the game (and its handlers) see every real exception.
            cont = DBG_EXCEPTION_NOT_HANDLED;
            if (++exceptions <= 200) {
                std::wstring extra;
                if (er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er.NumberParameters >= 2) {
                    wchar_t buf[96];
                    swprintf_s(buf, L" (%s 0x%IX)",
                               er.ExceptionInformation[0] == 0 ? L"read" :
                               er.ExceptionInformation[0] == 1 ? L"write" : L"execute",
                               er.ExceptionInformation[1]);
                    extra = buf;
                }
                Log(L"DEBUG: %s-chance exception 0x%08lX at %s%s, thread %lu",
                    ev.u.Exception.dwFirstChance ? L"first" : L"SECOND",
                    er.ExceptionCode, Describe(at).c_str(), extra.c_str(), ev.dwThreadId);
            }
            break;
        }

        case OUTPUT_DEBUG_STRING_EVENT:
            if (++strings <= 300) {
                std::wstring s = ReadDebugString(ev.u.DebugString);
                if (!s.empty())
                    Log(L"DEBUG: output: %s", s.c_str());
            }
            break;

        case EXIT_PROCESS_DEBUG_EVENT: {
            DWORD code = ev.u.ExitProcess.dwExitCode;
            Log(L"DEBUG: process exited with code %lu (0x%08lX), %d exception(s)",
                code, code, exceptions);
            ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
            return code;
        }
        }

        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
    }
}
