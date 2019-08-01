project(LegacyWindow)
cmake_minimum_required(VERSION 3.8)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(DirectX REQUIRED)
include_directories(${DirectX_DDRAW_INCLUDE_DIR})

add_executable(EXE WIN32 WinMain.cpp)
set_target_properties(EXE PROPERTIES OUTPUT_NAME "legacy_window")
target_link_libraries(EXE Shlwapi)

add_library(DLL SHARED DLLMain.cpp)
set_target_properties(DLL PROPERTIES OUTPUT_NAME "legacy_windowhook")
