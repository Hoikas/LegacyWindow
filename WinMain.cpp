/* Copyright (C) 2019 Adam Johnson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "LegacyWindow.h"

#include <Shlwapi.h>

// ================================================================================================

static const char s_hookDllPath[] = "legacy_windowhook.dll";

// ================================================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Sanity check
    if (PathFileExistsA(s_hookDllPath) == FALSE) {
        MessageBoxA(nullptr, "Hook DLL not found!", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Try to fire up Legacy of Time...
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));

    BOOL result = CreateProcessA("legacy.exe", nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                                 nullptr, nullptr, &si, &pi);
    if (result == FALSE) {
        wchar_t* msg = nullptr;
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            nullptr,
            GetLastError(),
            LANG_USER_DEFAULT,
            (LPWSTR)&msg,
            0,
            nullptr
        );
        MessageBoxW(nullptr, msg, L"Error", MB_OK | MB_ICONERROR);

        LocalFree(msg);
        return 1;
    }

    do {
        // Legacy of Time EXE has started, need to prepare to inject the DLL
        void* ptr = VirtualAllocEx(pi.hProcess, 0, sizeof(s_hookDllPath), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!ptr) {
            MessageBoxA(nullptr, "Failed to allocate memory in legacy.exe", "Error", MB_OK | MB_ICONERROR);
            break;
        }
        SIZE_T bytesWritten;
        WriteProcessMemory(pi.hProcess, ptr, s_hookDllPath, sizeof(s_hookDllPath), &bytesWritten);

        // DLL path written to remote EXE, now we need to load the dll
        FARPROC load_library = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
        HANDLE thread = CreateRemoteThread(pi.hProcess, nullptr, 0,
                                           (LPTHREAD_START_ROUTINE)load_library, ptr, 0, 0);
        if (!thread) {
            MessageBoxA(nullptr, "Failed to start new thread in legacy.exe", "Error", MB_OK | MB_ICONERROR);
            break;
        }

        // Wait a reasonable-ish time for LoadLibrary to complete BEFORE allowing Legacy to begin
        if (WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0) {
            MessageBoxA(nullptr, "Hook DLL has deadlocked", "Error", MB_OK | MB_ICONERROR);
            break;
        }

        // We seem to be ready to go.
        ResumeThread(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
    } while (0);

    // If the Legacy EXE is still running, we did not finish sanely.
    bool legacy_running = WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0;
    if (legacy_running)
        TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return legacy_running ? 1 : 0;
}
