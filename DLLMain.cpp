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
#include <ddraw.h>
#include <fstream>
#include <list>

#include "MinHookpp.h"

// ================================================================================================

typedef HRESULT(WINAPI* FDirectDrawCreate)(GUID FAR*, LPDIRECTDRAW FAR*, IUnknown FAR*);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSetCooperativeLevel)(LPDIRECTDRAW, HWND, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSetDisplayMode)(LPDIRECTDRAW, DWORD, DWORD, DWORD);

// ================================================================================================

static std::ofstream s_log;
static MHpp_Hook<FDirectDrawCreate>* s_ddrawCreateHook = nullptr;
static FDirectDrawSetCooperativeLevel s_ddrawSetCooperativeLevel = nullptr;
static FDirectDrawSetDisplayMode s_ddrawSetDisplayMode = nullptr;

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

    return EXCEPTION_EXECUTE_HANDLER;
}

// ================================================================================================

template<typename Args>
static bool MakeHook(LPCWSTR module, LPCSTR func, Args hook, MHpp_Hook<Args>*& out)
{
    s_log << "Attempting to hook " << module << "!" << func << std::endl;
    auto result = MHpp_Hook<FDirectDrawCreate>::Create(module, func, hook);
    if (std::get<0>(result) == MH_OK) {
        out = std::get<1>(result);
        return true;
    } else {
        s_log << "ERROR: " << MH_StatusToString(std::get<0>(result)) << std::endl;
        return false;
    }
}

// ================================================================================================

template<typename Args>
static inline void SwapImplementation(LPVOID* vftable, size_t index, Args& original,
                                      LPVOID replacement)
{
    original = (Args)vftable[index];
    vftable[index] = replacement;
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyDDrawSetCooperativeLevel(LPDIRECTDRAW ddraw, HWND wnd, DWORD flags)
{
    s_log << "IDirectDraw::SetCooperativeLevel: requested flags 0x" << std::hex << flags << std::endl;

    // Generally, we expect the flags to be: DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT | DDSCL_EXCLUSIVE
    flags &= ~DDSCL_FULLSCREEN;
    flags &= ~DDSCL_EXCLUSIVE;
    flags |= DDSCL_NORMAL;

    s_log << "IDirectDraw::SetCooperativeLevel: using flags 0x" << std::hex << flags << std::endl;
    return s_ddrawSetCooperativeLevel(ddraw, wnd, flags);
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyDDrawSetDisplayMode(LPDIRECTDRAW ddraw, DWORD width,
                                                           DWORD height, DWORD bpp)
{
    s_log << "IDirectDraw::SetDisplayMode: requested " << std::dec << width << "x" << height << std::endl;
    s_log << "IDirectDraw::SetDisplayMode: dropping request like hot potato" << std::endl;

    return DD_OK;
}

// ================================================================================================

static HRESULT WINAPI LegacyDDrawCreate(GUID FAR* lpGUID, LPDIRECTDRAW FAR* lplpDD, IUnknown FAR* pUnkOuter)
{
    s_log << "DirectDrawCreate: handling futz request" << std::endl;
    HRESULT result = s_ddrawCreateHook->original()(lpGUID, lplpDD, pUnkOuter);
    if (FAILED(result)) {
        s_log << "ERROR: DirectDrawCreate failed... skipping..." << std::endl;
        return result;
    }

    // temp
    SetUnhandledExceptionFilter(HandleException);

    // Rerouting robots^H^H^H^H^H^Hsome functions.
    LPDIRECTDRAW ddraw = *lplpDD;
    LPVOID* vftable = (LPVOID*)((int*)ddraw)[0];
    SwapImplementation(vftable, 20, s_ddrawSetCooperativeLevel, &LegacyDDrawSetCooperativeLevel);
    SwapImplementation(vftable, 21, s_ddrawSetDisplayMode, &LegacyDDrawSetDisplayMode);

    return result;
}

// ================================================================================================

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
    {
        s_log.open("legacy_window.log", std::ios::out);
        s_log << "Legacy of Time - Windowed Hook" << std::endl << std::endl;

        MH_STATUS status = MH_Initialize();
        if (status != MH_OK) {
            s_log << "ERROR: Failed to initialize MinHook." << std::endl;
            s_log << MH_StatusToString(status) << std::endl;
            ExitProcess(1);
            break;
        }

        if (!MakeHook(L"ddraw.dll", "DirectDrawCreate", LegacyDDrawCreate, s_ddrawCreateHook)) {
            ExitProcess(1);
            break;
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        s_log << "NOTICE: legacy.exe closing..." << std::endl;

        delete s_ddrawCreateHook;

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
