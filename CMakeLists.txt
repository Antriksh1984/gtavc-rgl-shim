cmake_minimum_required(VERSION 3.20)
project(vc_shim VERSION 1.0.0 LANGUAGES CXX RC)

# GTA Vice City is a 32-bit process; an x64 DLL would simply fail to load.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Build for x86: cmake -S . -B build -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT (/MT): no VC++ redistributable needed on the target machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# version.h feeds both the version resource and the log banner.
configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

add_library(vcshim SHARED
  src/dllmain.cpp
  src/proxy.cpp
  src/shim.cpp
  src/config.cpp
  src/backup.cpp
  src/debugwatch.cpp
  src/log.cpp
  src/exports.def
  src/version.rc
)
target_include_directories(vcshim PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(vcshim PROPERTIES OUTPUT_NAME dinput8)
target_compile_definitions(vcshim PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(vcshim PRIVATE /W4 /utf-8)
target_link_libraries(vcshim PRIVATE shell32 ole32)
