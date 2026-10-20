// Entry-point hook. DllMain only patches 5 bytes at the exe's entry point with
// a JMP to ShimEntryThunk; all real work happens later in ShimMain, once the
// loader has finished and it's safe to create processes.
//
// The RGL gta-vc.exe entry point is a protection stub (section .rkstr). We
// patch it before it ever runs, so nothing gets decrypted or initialised. To
// switch to a different trigger (e.g. an IAT hook), replace InstallEntryHook
// and have the hook call ShimMain.
#include "shim.h"

#include <cstring>

HMODULE g_module = nullptr;

static BYTE* g_entry = nullptr;           // exe entry point
static BYTE g_savedBytes[5];              // original bytes we overwrote
extern "C" void* g_originalEntry = nullptr;  // jump target for the thunk

static void WriteCode(BYTE* dst, const BYTE* src, size_t len)
{
    // Entry code is normally read+execute; make it writable for the patch.
    DWORD old;
    VirtualProtect(dst, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(dst, src, len);
    VirtualProtect(dst, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, len);
}

void RestoreEntryBytes()
{
    if (g_entry)
        WriteCode(g_entry, g_savedBytes, sizeof(g_savedBytes));
}

// The loader jumps here instead of the real entry point. Registers and flags
// are preserved around ShimMain (some protectors read EBX/EAX at entry), and the
// stack is untouched, so the original entry sees exactly what the loader passed.
extern "C" __declspec(naked) void ShimEntryThunk()
{
    __asm {
        pushfd
        pushad
        call ShimMain
        popad
        popfd
        jmp dword ptr [g_originalEntry]
    }
}

static bool HostIsViceCity()
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    return lstrcmpiW(name ? name + 1 : path, L"gta-vc.exe") == 0;
}

static void InstallEntryHook()
{
    // Only hook the RGL gta-vc.exe, and never a child we launched ourselves.
    if (!HostIsViceCity() || GetEnvironmentVariableW(L"VCSHIM_CHILD", nullptr, 0) != 0)
        return;

    // Main exe base -> PE headers -> AddressOfEntryPoint.
    BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.AddressOfEntryPoint == 0)
        return;

    g_entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    g_originalEntry = g_entry;
    memcpy(g_savedBytes, g_entry, sizeof(g_savedBytes));

    // E9 rel32: JMP relative to the end of this 5-byte instruction.
    BYTE patch[5] = {0xE9};
    INT32 rel = INT32(reinterpret_cast<BYTE*>(&ShimEntryThunk) - (g_entry + 5));
    memcpy(patch + 1, &rel, sizeof(rel));
    WriteCode(g_entry, patch, sizeof(patch));
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = inst;
        DisableThreadLibraryCalls(inst);  // we don't need thread attach/detach
        InstallEntryHook();
    }
    return TRUE;
}
