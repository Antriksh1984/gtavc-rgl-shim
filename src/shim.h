#pragma once
#include <windows.h>

extern HMODULE g_module;  // this DLL, set in DllMain

// Puts the original entry-point bytes back (defined in dllmain.cpp).
void RestoreEntryBytes();

// Called from the entry-point thunk. Either runs the modded game and calls
// ExitProcess (never returns), or restores the entry bytes and returns so the
// thunk jumps to the original entry point (bypass / fail-safe).
extern "C" void __cdecl ShimMain();
