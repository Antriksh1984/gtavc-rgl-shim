#pragma once
#include <windows.h>

// Diagnostic mode (DebugChild=1): acts as a minimal debugger for a process
// created with DEBUG_ONLY_THIS_PROCESS. Logs module loads, exceptions (with
// module+offset), and debug output until the process exits. Must run on the
// thread that called CreateProcess. Returns the process exit code.
DWORD RunDebugLoop();
