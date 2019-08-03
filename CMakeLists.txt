project(LegacyWindow)
cmake_minimum_required(VERSION 3.8)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(DirectX REQUIRED)
include_directories(${DirectX_DDRAW_INCLUDE_DIR})

add_library(minhook STATIC minhook/src/hde/hde32.c
                           minhook/src/buffer.c
                           minhook/src/hook.c
                           minhook/src/trampoline.c)
include_directories(minhook/include)

add_executable(EXE WIN32 WinMain.cpp)
set_target_properties(EXE PROPERTIES OUTPUT_NAME "legacy_window")
target_link_libraries(EXE Shlwapi)

add_library(DLL SHARED DLLMain.cpp LegacyDDraw.cpp LegacyWin32.cpp)
set_target_properties(DLL PROPERTIES OUTPUT_NAME "legacy_windowhook")
target_link_libraries(DLL DbgHelp)
target_link_libraries(DLL minhook)
