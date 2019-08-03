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

static LRESULT WINAPI WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
//#define WM_DEBUG_LOG
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

#if 0
    MessageBoxA(nullptr, "Attach the Debugger now, fool.", "Debug Taim", MB_OK);
#endif

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

    HWND wnd = s_createWindowHook->original()(dwExStyle, lpClassName, lpWindowName, dwStyle,
                                              X, Y, nWidth, nHeight, hWndParent, hMenu,
                                              hInstance, lpParam);
    // FIXME: this might be the temporary intro video window.
    s_legacyHWND = wnd;
    return wnd;
}

// ================================================================================================

bool InitWin32Hooks()
{
    MAKE_HOOK(L"User32.dll", "RegisterClassExA", LegacyRegisterClass, s_registerClassHook);
    MAKE_HOOK(L"User32.dll", "CreateWindowExA", LegacyCreateWindow, s_createWindowHook);
}

// ================================================================================================

void DeInitWin32Hooks()
{
    delete s_registerClassHook;
    delete s_createWindowHook;
}
