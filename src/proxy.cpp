// Forwards every dinput8 export to the real system DLL, loaded lazily on
// first use (never from DllMain). Export names are mapped in exports.def.
#include <windows.h>

static HMODULE RealDll()
{
    static HMODULE real = [] {
        // GetSystemDirectoryW returns SysWOW64 for a 32-bit process on 64-bit
        // Windows, so this is always the matching 32-bit dinput8.dll.
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n == 0 || n + 13 >= MAX_PATH)
            return HMODULE(nullptr);
        lstrcatW(path, L"\\dinput8.dll");
        return LoadLibraryW(path);
    }();
    return real;
}

template <typename Fn>
static Fn RealProc(const char* name)
{
    HMODULE dll = RealDll();
    return dll ? reinterpret_cast<Fn>(GetProcAddress(dll, name)) : nullptr;
}

extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid,
                                        LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    static Fn fn = RealProc<Fn>("DirectInput8Create");
    return fn ? fn(inst, version, riid, out, outer) : E_FAIL;
}

HRESULT WINAPI Proxy_DllCanUnloadNow()
{
    using Fn = HRESULT(WINAPI*)();
    static Fn fn = RealProc<Fn>("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}

HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out)
{
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    static Fn fn = RealProc<Fn>("DllGetClassObject");
    return fn ? fn(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI Proxy_DllRegisterServer()
{
    using Fn = HRESULT(WINAPI*)();
    static Fn fn = RealProc<Fn>("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer()
{
    using Fn = HRESULT(WINAPI*)();
    static Fn fn = RealProc<Fn>("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}

// Returns the system's c_dfDIJoystick data format (LPCDIDATAFORMAT).
const void* WINAPI Proxy_GetdfDIJoystick()
{
    using Fn = const void*(WINAPI*)();
    static Fn fn = RealProc<Fn>("GetdfDIJoystick");
    return fn ? fn() : nullptr;
}

} // extern "C"
