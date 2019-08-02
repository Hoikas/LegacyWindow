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
typedef ATOM(WINAPI* FRegisterClassExA)(_In_ CONST WNDCLASSEXA*);
typedef HWND(WINAPI* FCreateWindowExA)(_In_ DWORD, _In_opt_ LPCSTR, _In_opt_ LPCSTR, _In_ DWORD,
                                       _In_ int, _In_ int, _In_ int, _In_ int, _In_opt_ HWND,
                                       _In_opt_ HMENU, _In_opt_ HINSTANCE, _In_opt_ LPVOID);

// ================================================================================================

static std::ofstream s_log;
static WNDPROC s_legacyWndProc = nullptr;

static MHpp_Hook<FDirectDrawCreate>* s_ddrawCreateHook = nullptr;
static MHpp_Hook<FRegisterClassExA>* s_registerClassHook = nullptr;
static MHpp_Hook<FCreateWindowExA>* s_createWindowHook = nullptr;
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
    // Crap... std::ofstream won't print out wchar strings.
    char module_temp[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, module, -1, module_temp, sizeof(module_temp), nullptr, nullptr);

    s_log << "Attempting to hook " << module_temp << "!" << func << std::endl;
    auto result = MHpp_Hook<Args>::Create(module, func, hook);
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

static LRESULT WINAPI WndProcDebug(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
#define DECLARE_WM(x) \
    case x: \
        s_log << #x << " "; \
        break;

    LRESULT result = CallWindowProcA(s_legacyWndProc, wnd, msg, wParam, lParam);

    s_log << "WndProc: Forwarded ";
    switch (msg) {
#include "WM.inl"
    default:
        s_log << "0x" << std::hex << msg << " ";
        break;
    }

    s_log << std::hex << "wParam: " << wParam << " lParam: " << lParam << " Result: "
          << result << std::endl;
    return result;

#undef DECLARE_WM
}

// ================================================================================================

static LRESULT WINAPI WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
        return CallWindowProcA(DefWindowProcA, wnd, msg, wParam, lParam);

#ifdef WM_DEBUG_LOG
    // These are called so frequently the log is useless.
    case WM_NCHITTEST:
    case WM_MOUSEFIRST:
    case WM_SETCURSOR:
        return CallWindowProcA(s_legacyWndProc, wnd, msg, wParam, lParam);
#endif

    default:
#ifdef WM_DEBUG_LOG
        return CallWindowProcA(&WndProcDebug, wnd, msg, wParam, lParam);
#else
        return CallWindowProcA(s_legacyWndProc, wnd, msg, wParam, lParam);
#endif
    }
}

// ================================================================================================

static ATOM WINAPI LegacyRegisterClass(_In_ CONST WNDCLASSEXA* wndclass)
{
    s_log << "RegisterClassExA: original WndProc " << std::hex << wndclass->lpfnWndProc << std::endl;
    s_legacyWndProc = wndclass->lpfnWndProc;

    // Naughty touching
    const_cast<WNDCLASSEXA*>(wndclass)->lpfnWndProc = &WndProc;
    return s_registerClassHook->original()(wndclass);
}

// ================================================================================================

static HWND WINAPI LegacyCreateWindow(_In_ DWORD dwExStyle, _In_opt_ LPCSTR lpClassName,
                                      _In_opt_ LPCSTR lpWindowName, _In_ DWORD dwStyle,
                                      _In_ int X, _In_ int Y, _In_ int nWidth, _In_ int nHeight,
                                      _In_opt_ HWND hWndParent, _In_opt_ HMENU hMenu,
                                      _In_opt_ HINSTANCE hInstance, _In_opt_ LPVOID lpParam)
{
    s_log << "CreateWindowExA: requested... dwExStyle: 0x" << std::hex << dwExStyle <<
             " dwStyle: 0x" << std::hex << dwStyle << std::endl;

    // According to science (read: logging), JMP3 actually makes two windows.
    // First, it makes a window for the intro video--this one looks a bit weird for some reason,
    // Then, it makes the main game window. Unfortunately, this means we can just do stuff like
    // pass-through CW_USEDEFAULT because the game window will appear to "jump" around.
    dwStyle |= WS_CAPTION | WS_THICKFRAME;

    return s_createWindowHook->original()(dwExStyle, lpClassName, lpWindowName, dwStyle,
                                          X, Y, nWidth, nHeight, hWndParent, hMenu,
                                          hInstance, lpParam);
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

#define MAKE_HOOK(m, f, r, t) \
    if (!MakeHook(m, f, r, t)) { \
        ExitProcess(1); \
        break; \
    }
        MAKE_HOOK(L"ddraw.dll", "DirectDrawCreate", LegacyDDrawCreate, s_ddrawCreateHook);
        MAKE_HOOK(L"User32.dll", "RegisterClassExA", LegacyRegisterClass, s_registerClassHook);
        MAKE_HOOK(L"User32.dll", "CreateWindowExA", LegacyCreateWindow, s_createWindowHook);
#undef MAKE_HOOK
        break;
    }
    case DLL_PROCESS_DETACH:
        s_log << "NOTICE: legacy.exe closing..." << std::endl;

        delete s_ddrawCreateHook;
        delete s_registerClassHook;
        delete s_createWindowHook;

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
