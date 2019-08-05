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

#include <fstream>

#include "DLL.h"
#include "LegacyTypedefs.h"
#include "MinHookpp.h"

// ================================================================================================

extern std::ofstream s_log;

static WNDPROC s_legacyWndProc = nullptr;
static HWND s_legacyHWND;

static MHpp_Hook<FRegisterClassExA>* s_registerClassHook = nullptr;
static MHpp_Hook<FCreateWindowExA>* s_createWindowHook = nullptr;
static MHpp_Hook<FGetWindowRect>* s_getWindowRectHook = nullptr;
static MHpp_Hook<FGetClientRect>* s_getClientRectHook = nullptr;
static MHpp_Hook<FOutputDebugStringA>* s_outputDebugStringHook = nullptr;

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

    s_log << std::hex << "wParam: 0x" << wParam << " lParam: 0x" << lParam << " Result: 0x"
          << result << std::endl;
    return result;

#undef DECLARE_WM
}

// ================================================================================================

static LRESULT WINAPI LegacyWndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
//#define LEGACY_WM_DEBUG_LOG
#ifdef LEGACY_WM_DEBUG_LOG
    WNDPROC BaseWndProc = &WndProcDebug;
#else
    WNDPROC BaseWndProc = s_legacyWndProc;
#endif

    switch (msg) {
    case WM_CREATE: {
        // If we are here, we are definitely the s_legacyHWND. That value is currently null and is
        // depended on by our hook for future window creations that will take place before this
        // window's CreateWindowExA returns.
        s_legacyHWND = wnd;

        // QuickTime videos are played in the Legacy.exe WndProc. This worked fine in Windows 98,
        // however, later versions of Windows get mad if you don't service the message queue
        // every five seconds. Thankfully, we have this built-in workaround from MS.
        DisableProcessWindowsGhosting();

        return CallWindowProcA(BaseWndProc, wnd, msg, wParam, lParam);
    }

    // Legacy's WndProc prevents the Window from moving.
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
        return CallWindowProcA(DefWindowProcA, wnd, msg, wParam, lParam);

#ifdef LEGACY_WM_DEBUG_LOG
    // These are called so frequently the log is useless.
    case WM_NCHITTEST:
    case WM_MOUSEFIRST:
    case WM_SETCURSOR:
        return CallWindowProcA(s_legacyWndProc, wnd, msg, wParam, lParam);
#endif

    default:
        return CallWindowProcA(BaseWndProc, wnd, msg, wParam, lParam);
    }
}

// ================================================================================================

static ATOM WINAPI LegacyRegisterClass(_In_ CONST WNDCLASSEXA* wndclass)
{
    s_log << "RegisterClassExA: original WndProc " << std::hex << wndclass->lpfnWndProc << std::endl;
    s_legacyWndProc = wndclass->lpfnWndProc;

#if 0
    MessageBoxA(nullptr, "Attach the Debugger now, fool.", "Debug Taim", MB_OK);
#endif

    // Naughty touching
    const_cast<WNDCLASSEXA*>(wndclass)->lpfnWndProc = &LegacyWndProc;
    return s_registerClassHook->original()(wndclass);
}

// ================================================================================================

static HWND WINAPI LegacyCreateWindow(_In_ DWORD dwExStyle, _In_opt_ LPCSTR lpClassName,
                                      _In_opt_ LPCSTR lpWindowName, _In_ DWORD dwStyle,
                                      _In_ int X, _In_ int Y, _In_ int nWidth, _In_ int nHeight,
                                      _In_opt_ HWND hWndParent, _In_opt_ HMENU hMenu,
                                      _In_opt_ HINSTANCE hInstance, _In_opt_ LPVOID lpParam)
{
    s_log << "CreateWindowExA: requested... dwExStyle: 0x" << std::hex << dwExStyle
          << " dwStyle: 0x" << std::hex << dwStyle << std::endl;

    if (lpWindowName && strcmp(lpWindowName, "Legacy of Time") == 0) {
        dwStyle |= WS_CAPTION | WS_THICKFRAME;
        dwStyle &= ~WS_POPUP;

        // The game only requests a 640x480 window. Unfortunately, there is nonclient area, such as
        // title bars and menus to consider as well.
        RECT window_rect = { 0, 0, nWidth, nHeight };
        AdjustWindowRect(&window_rect, dwStyle, FALSE);
        nWidth = window_rect.right - window_rect.left;
        nHeight = window_rect.bottom - window_rect.top;

        // Make life interesting by spawning somewhere nice.
        X = CW_USEDEFAULT;
        Y = CW_USEDEFAULT;

        s_log << "CreateWindowExA: creating Legacy window... dwStyle: 0x" << std::hex << dwStyle
              << " nWidth: " << std::dec << nWidth << " nHeight: " << nHeight << std::endl;

        HWND wnd = s_createWindowHook->original()(dwExStyle, lpClassName, lpWindowName, dwStyle,
                                                  X, Y, nWidth, nHeight, hWndParent, hMenu,
                                                  hInstance, lpParam);
        // NOTE: this is something of a fool's errand here... The call to CreateWindowExA pumps the
        // WndProc several times, which actually spawns another call to CreateWindowExA. Yay.
        // To fix this issue, we intercept a WM and set it early. Leaving this here for completeness,
        // however...
        s_legacyHWND = wnd;
        return wnd;
    } else if (lpClassName && strncmp(lpClassName, "QTIdle", 6) == 0) {
        // We need for the X and Y coordinates of the window to be the screenspace client origin
        // of the legacy window.
        {
            POINT origin{ 0 };
            MapWindowPoints(s_legacyHWND, HWND_DESKTOP, &origin, 1);
            X = origin.x;
            Y = origin.y;
        }

        s_log << "CreateWindowExA: creating intro-video window... X: "
              << std::dec << X << " Y: " << Y << std::endl;
    } else {
        s_log << "CreateWindowExA: passing non-Legacy window creation to Win32" << std::endl;
    }

    return s_createWindowHook->original()(dwExStyle, lpClassName, lpWindowName, dwStyle,
                                          X, Y, nWidth, nHeight, hWndParent, hMenu,
                                          hInstance, lpParam);
}

// ================================================================================================

static BOOL WINAPI LegacyGetWindowRect(_In_ HWND hWnd, _Out_ LPRECT lpRect)
{
    // QuickTime calls this method every frame it plays and uses these values as an offset,
    // which it propagates to IDirectDrawSurface::Lock. This causes an error when we try to lock
    // a region of the surface outside of the 640x480 area.
    SetRect(lpRect, 0, 0, 640, 480);
    return TRUE;
}

// ================================================================================================

static BOOL WINAPI LegacyGetClientRect(_In_ HWND hWnd, _Out_ LPRECT lpRect)
{
    // See above
    SetRect(lpRect, 0, 0, 640, 480);
    return TRUE;
}

// ================================================================================================

static VOID WINAPI LegacyOutputDebugString(_In_opt_ LPCSTR lpOutputString)
{
    // Legacy.exe has some (limited) debug information available. Also, the gog.com version of
    // Legacy seems to contain its own ddraw.dll with some debug information.
    s_log << "OutputDebugStringA: " << lpOutputString << std::flush;
}

// ================================================================================================

HWND Win32GetClientHWND()
{
    return s_legacyHWND;
}

// ================================================================================================

bool Win32SetThreadCount(int nthreads)
{
    HANDLE process = GetCurrentProcess();
    DWORD_PTR paff, saff;
    GetProcessAffinityMask(process, &paff, &saff);

    DWORD_PTR desired_affinity = 0;
    int ncpus = 0;
    int nthreadscur = 0;
    for (DWORD_PTR mask = 1<<0; mask <= saff; mask <<= 1) {
        if (saff & mask)
            ncpus++;
        if (paff & mask)
            nthreadscur++;
    }

    if (nthreads == -1 || ncpus <= nthreads) {
        desired_affinity = saff;
    } else {
        int i = nthreads;
        for (DWORD_PTR mask = 1<<0; mask <= saff && i; mask <<= 1) {
            if (saff & mask) {
                desired_affinity |= mask;
                i--;
            }
        }
    }

    s_log << "Win32SetThreadCount: System CPUs: " << std::dec << ncpus << " Current Concurrency: "
          << nthreadscur << std::endl;
    if (ncpus < nthreads) {
        s_log << "Win32SetThreadCount: ERROR! Requested more concurrency than available!"
              << std::endl;
    }
    if (nthreadscur != nthreads) {
        s_log << "Win32SetThreadCount: Setting Concurrency: " << ((nthreads == -1) ? ncpus : nthreads)
              << " AffinityMask: 0x" << std::hex << desired_affinity << std::endl;
        SetProcessAffinityMask(process, desired_affinity);
    }
    return true;
}

// ================================================================================================

bool Win32InitHooks()
{
    MAKE_HOOK(L"User32.dll", "RegisterClassExA", LegacyRegisterClass, s_registerClassHook);
    MAKE_HOOK(L"User32.dll", "CreateWindowExA", LegacyCreateWindow, s_createWindowHook);
    MAKE_HOOK(L"User32.dll", "GetWindowRect", LegacyGetWindowRect, s_getWindowRectHook);
    MAKE_HOOK(L"User32.dll", "GetClientRect", LegacyGetClientRect, s_getClientRectHook);
    MAKE_HOOK(L"Kernel32.dll", "OutputDebugStringA", LegacyOutputDebugString, s_outputDebugStringHook);
    return true;
}

// ================================================================================================

void Win32DeInitHooks()
{
    delete s_registerClassHook;
    delete s_createWindowHook;
    delete s_getWindowRectHook;
    delete s_getClientRectHook;
    delete s_outputDebugStringHook;
}
