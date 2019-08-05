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

#include <DbgHelp.h>
#include <fstream>

#include "DLL.h"
#include "LegacyTypedefs.h"
#include "MinHookpp.h"

// ================================================================================================

std::ofstream s_log;

// ================================================================================================

static LONG WINAPI HandleException(EXCEPTION_POINTERS* info)
{
    s_log << "Oh fiddlesticks. There was a an unhandled exception." << std::endl;

    // Hopefully things aren't too corrupted for this to work...
    MINIDUMP_EXCEPTION_INFORMATION dump_info;
    dump_info.ClientPointers = TRUE;
    dump_info.ExceptionPointers = info;
    dump_info.ThreadId = GetCurrentThreadId();
    HANDLE crash_file = CreateFileA("legacy_window.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), crash_file,
        (MINIDUMP_TYPE)(MiniDumpWithPrivateReadWriteMemory |
                        MiniDumpWithIndirectlyReferencedMemory |
                        MiniDumpWithDataSegs |
                        MiniDumpWithHandleData),
                      &dump_info, nullptr, nullptr);
    CloseHandle(crash_file);
    ExitProcess(1);

    return EXCEPTION_CONTINUE_SEARCH;
}

// ================================================================================================

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
    {
        s_log.open("legacy_window.log", std::ios::out);
        s_log << "Legacy of Time - Windowed Hook" << std::endl << std::endl;

        // The really old VC++ runtime seems to install a handler that silently exits.
        SetUnhandledExceptionFilter(HandleException);

        MH_STATUS status = MH_Initialize();
        if (status != MH_OK) {
            s_log << "ERROR: Failed to initialize MinHook." << std::endl;
            s_log << MH_StatusToString(status) << std::endl;
            ExitProcess(1);
            break;
        }

        if (!DDrawInitHooks())
            break;
        if (!Win32InitHooks())
            break;
        break;
    }
    case DLL_PROCESS_DETACH:
        s_log << "NOTICE: legacy.exe closing..." << std::endl;

        DDrawDeInitHooks();
        Win32DeInitHooks();

        MH_STATUS status = MH_Uninitialize();
        if (status != MH_OK) {
            s_log << "Something went wrong when tearing down MinHook..." << std::endl;
            s_log << MH_StatusToString(status) << std::endl;
        }

        s_log.close();
        break;
    }

    return TRUE;
}
