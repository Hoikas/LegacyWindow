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
#include <iterator>
#include <mutex>

#include "DLL.h"
#include "LegacyTypedefs.h"
#include "MinHookpp.h"

// ================================================================================================

enum
{
    e_overrideWindowRect = (1<<0),
    e_leftMouseDown = (1<<1),
    e_mouseDragging = (1<<2),
};

#define IDM_RESOLUTION_START 0x1000
#define IDM_SHOW_FPS 0x1100
#define IDM_SHOW_FRAMETIME 0x1101

struct _DialogWndData
{
    HINSTANCE m_hInstance{ };
    LPCSTR m_template{ };
    DLGPROC m_dlgproc{ };
    LPARAM m_initParam{ };
    HWND m_previousBltTarget{ };
};

// ================================================================================================

extern std::ofstream s_log;

static WNDPROC s_legacyWndProc{ };
static HWND s_legacyHWND{ };
static HWND s_legacyDialog{  };
static HMENU s_legacyMenu{ };
static HMENU s_hookMenu{ };
static POINT s_gameResolution{ 640, 480 };
static POINT s_gameResolutionOptions[] {
    { 640, 480 },
    { 800, 600 },
    { 1024, 768 },
    { 1280, 980 },
    { 1400, 1050 },
    { 1600, 1200 },
    { 1920, 1440 },
    { 2048, 1536 },
    { 3200, 2400 },
};
static std::mutex s_gameResolutionLock;
static POINT s_lastLmbDown{ 0 };
static uint32_t s_flags{ 0 };

static MHpp_Hook<FRegisterClassExA>* s_registerClassHook = nullptr;
static MHpp_Hook<FCreateWindowExA>* s_createWindowHook = nullptr;
static MHpp_Hook<FGetWindowRect>* s_getWindowRectHook = nullptr;
static MHpp_Hook<FGetClientRect>* s_getClientRectHook = nullptr;
static MHpp_Hook<FOutputDebugStringA>* s_outputDebugStringHook = nullptr;
static MHpp_Hook<FGetCursorPos>* s_getCursorPosHook = nullptr;
static MHpp_Hook<FSetCursorPos>* s_setCursorPosHook = nullptr;
static MHpp_Hook<FClientToScreen>* s_clientToScreenHook = nullptr;
static MHpp_Hook<FScreenToClient>* s_screenToClientHook = nullptr;
static MHpp_Hook<FSetMenu>* s_setMenuHook = nullptr;
static MHpp_Hook<FLoadMenu>* s_loadMenuHook = nullptr;
static MHpp_Hook<FGetSystemMetrics>* s_getSystemMetricsHook = nullptr;
static MHpp_Hook<FPeekMessage>* s_peekMessageHook = nullptr;
static MHpp_Hook<FDialogBoxParam>* s_dialogBoxParamHook = nullptr;

// ================================================================================================

static void LegacyHandleLMB(HWND wnd, bool down, int x, int y)
{
    if (down) {
        if (!(s_flags & e_leftMouseDown)) {
            s_flags |= e_leftMouseDown;
            s_lastLmbDown = { x, y };
            MapWindowPoints(wnd, HWND_DESKTOP, &s_lastLmbDown, 1);
        } else if (!(s_flags & e_mouseDragging)) {
            if (DragDetect(wnd, s_lastLmbDown) != FALSE) {
                s_flags |= e_mouseDragging;
                RECT rect{ 0 };
                s_getClientRectHook->original()(wnd, &rect);
                MapWindowPoints(wnd, HWND_DESKTOP, (LPPOINT)& rect, 2);
                ClipCursor(&rect);
                SetCapture(wnd);
            }
        }
    } else {
        s_flags &= ~e_leftMouseDown;
        if (s_flags & e_mouseDragging) {
            s_flags &= ~e_mouseDragging;
            ReleaseCapture();
        }
    }
}

// ================================================================================================

static void LegacyResizeGame()
{
    // Resize game window for the requested game resolution + nonclient area
    RECT window_rect{ 0, 0, s_gameResolution.x, s_gameResolution.y };
    AdjustWindowRect(&window_rect, GetWindowLongA(s_legacyHWND, GWL_STYLE), TRUE);
    int nWidth = window_rect.right - window_rect.left;
    int nHeight = window_rect.bottom - window_rect.top;
    s_log << "CreateWindowExA: resizing Legacy window for... hMenu: " << std::hex
        << GetMenu(s_legacyHWND) << " nWidth: " << std::dec << nWidth << std::dec << " nHeight: "
        << nHeight << std::endl;
    SetWindowPos(s_legacyHWND, HWND_TOP, -1, -1, nWidth, nHeight, (SWP_NOCOPYBITS | SWP_NOMOVE));

    // Update resolution menu
    MENUITEMINFOA info{ 0 };
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STATE;
    for (size_t i = 0; i < std::size(s_gameResolutionOptions); ++i) {
        UINT item = IDM_RESOLUTION_START + i;
        if (GetMenuItemInfoA(s_hookMenu, item, FALSE, &info) == FALSE) {
            s_log << "LegacyResizeGame: ERROR! Failed to get menu item 0x" << std::hex << item << std::endl;
            continue;
        }
        if (s_gameResolution.x == s_gameResolutionOptions[i].x &&
            s_gameResolution.y == s_gameResolutionOptions[i].y) {
            info.fState |= MFS_CHECKED;
        } else {
            info.fState &= ~MFS_CHECKED;
        }
        if (SetMenuItemInfoA(s_hookMenu, item, FALSE, &info) == FALSE) {
            s_log << "LegacyResizeGame: ERROR! Failed to set menu item 0x" << std::hex << item << std::endl;
        }
    }

    // Redraw everything
    DrawMenuBar(s_legacyHWND);
    DDrawForceDirty();
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

        // Setup default flags
        s_flags |= e_overrideWindowRect;

        // Setup objects for the draw thread
        DDrawAcquireGdiObjects();

        return CallWindowProcA(BaseWndProc, wnd, msg, wParam, lParam);
    }

    // Legacy's WndProc prevents the Window from moving.
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
        return CallWindowProcA(DefWindowProcA, wnd, msg, wParam, lParam);

    // Ensure the cursor does not do weird stuff when we're panning
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        LegacyHandleLMB(wnd, (wParam & MK_LBUTTON), x, y);

        // Translate mouse coordinates to the new window size.
        x = (x * 640) / s_gameResolution.x;
        y = (y * 480) / s_gameResolution.y;
        LPARAM cursorPos = ((y & 0xFFFF) << 16 | (x & 0xFFFF));

        // WM_MOUSEMOVE is spammy, so no debug logging.
        return CallWindowProcA(s_legacyWndProc, wnd, msg, wParam, cursorPos);
    }
    case WM_CAPTURECHANGED: {
        ClipCursor(nullptr);
        return CallWindowProcA(BaseWndProc, wnd, msg, wParam, lParam);
    }

    // These undocumented messages don't handle menu highlighting properly when our
    // clientspace == screenspace hacks are active, unfortunately.
    case WM_UAHDESTROYWINDOW:
    case WM_UAHDRAWMENU:
    case WM_UAHDRAWMENUITEM:
    case WM_UAHINITMENU:
    case WM_UAHMEASUREMENUITEM:
    case WM_UAHNCPAINTMENUPOPUP: {
        s_flags &= ~e_overrideWindowRect;
        LRESULT result = CallWindowProcA(BaseWndProc, wnd, msg, wParam, lParam);
        s_flags |= e_overrideWindowRect;
        return result;
    }

    case WM_COMMAND: {
        if (HIWORD(wParam) == 0 && lParam == 0) {
            WORD menuid = LOWORD(wParam);
            if (menuid >= IDM_RESOLUTION_START &&
                menuid <= IDM_RESOLUTION_START + std::size(s_gameResolutionOptions)) {
                size_t idx = menuid - IDM_RESOLUTION_START;
                if (s_gameResolution.x != s_gameResolutionOptions[idx].x &&
                    s_gameResolution.y != s_gameResolutionOptions[idx].y) {
                    std::lock_guard<std::mutex> _(s_gameResolutionLock);
                    s_gameResolution = s_gameResolutionOptions[idx];
                    LegacyResizeGame();
                }
                return 0;
            } else if (menuid == IDM_SHOW_FPS || menuid == IDM_SHOW_FRAMETIME) {
                MENUITEMINFOA info{ 0 };
                info.cbSize = sizeof(info);
                info.fMask = MIIM_STATE;
                GetMenuItemInfoA(s_hookMenu, menuid, FALSE, &info);

                bool toggle = !(info.fState & MFS_CHECKED);
                if (toggle)
                    info.fState |= MFS_CHECKED;
                else
                    info.fState |= MFS_CHECKED;
                SetMenuItemInfoA(s_hookMenu, menuid, FALSE, &info);
                DrawMenuBar(wnd);

                if (menuid == IDM_SHOW_FPS)
                    DDrawShowFPS(toggle);
                else
                    DDrawShowFrameTime(toggle);
                return 0;
            }
        }
        return CallWindowProcA(BaseWndProc, wnd, msg, wParam, lParam);
    }

    case WM_DESTROY: {
        DDrawJoin();
        LRESULT result = CallWindowProcA(BaseWndProc, wnd, msg, wParam, lParam);
        DDrawReleaseGdiObjects();
        return result;
    }

#ifdef LEGACY_WM_DEBUG_LOG
    // These are called so frequently the log is useless.
    case WM_NCHITTEST:
    case WM_SETCURSOR:
    case WM_ENTERIDLE:
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

static void LegacyFixMenu(HMENU menu)
{
    // Legacy.exe defer-inits the Cinematics menu. It looks sloppy for it to only appear
    // once it's moused over, however...
    HMENU hmCinematics = CreateMenu();
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)hmCinematics, "Cinematics");

    // Menu for our settings
    s_hookMenu = CreateMenu();
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)s_hookMenu, "Settings");

    // Resolution menu
    HMENU hmResolution = CreateMenu();
    AppendMenuA(s_hookMenu, MF_POPUP, (UINT_PTR)hmResolution, "Resolution");

    int desktopX = s_getSystemMetricsHook->original()(SM_CXSCREEN);
    int desktopY = s_getSystemMetricsHook->original()(SM_CYSCREEN);
    for (size_t i = 0; i < std::size(s_gameResolutionOptions); ++i) {
        UINT state = 0;

        RECT window_rect{ 0, 0, s_gameResolutionOptions[i].x, s_gameResolutionOptions[i].y };
        AdjustWindowRect(&window_rect, GetWindowLongA(s_legacyHWND, GWL_STYLE), TRUE);
        if ((window_rect.right - window_rect.left) > desktopX ||
            (window_rect.bottom - window_rect.top) > desktopY) {
            state |= MFS_GRAYED;
        }

        char buf[128];
        sprintf_s(buf, "%ux%u", s_gameResolutionOptions[i].x, s_gameResolutionOptions[i].y);

        MENUITEMINFOA info{ 0 };
        info.cbSize = sizeof(info);
        info.fMask = (MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING);
        info.fType = (MFT_RADIOCHECK | MFT_STRING);
        info.fState = state;
        info.wID = IDM_RESOLUTION_START + i;
        info.dwTypeData = buf;
        InsertMenuItemA(hmResolution, i, TRUE, &info);
    }

    AppendMenuA(s_hookMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(s_hookMenu, MF_STRING, IDM_SHOW_FPS, "Show FPS");
    AppendMenuA(s_hookMenu, MF_STRING, IDM_SHOW_FRAMETIME, "Show Frame Time");
}

// ================================================================================================

static HWND WINAPI LegacyCreateWindow(_In_ DWORD dwExStyle, _In_opt_ LPCSTR lpClassName,
                                      _In_opt_ LPCSTR lpWindowName, _In_ DWORD dwStyle,
                                      _In_ int X, _In_ int Y, _In_ int nWidth, _In_ int nHeight,
                                      _In_opt_ HWND hWndParent, _In_opt_ HMENU hMenu,
                                      _In_opt_ HINSTANCE hInstance, _In_opt_ LPVOID lpParam)
{
    s_log << "CreateWindowExA: requested... dwExStyle: 0x" << std::hex << dwExStyle
          << " dwStyle: 0x" << std::hex << dwStyle << " hMenu: 0x" << hMenu << std::endl;

    if (!s_legacyHWND && lpWindowName && strcmp(lpWindowName, "Legacy of Time") == 0) {
        dwStyle |= WS_CAPTION;
        dwStyle &= ~WS_POPUP;

        // The game only requests a 640x480 window. Unfortunately, there is nonclient area, such as
        // title bars and menus to consider as well.
        RECT window_rect{ 0, 0, s_gameResolution.x, s_gameResolution.y };
        AdjustWindowRect(&window_rect, dwStyle, hMenu != nullptr);
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

        // Legacy loads its own menu but passes nullptr to us... Grrr...
        if (!hMenu) {
            HMENU menu = LoadMenuA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(101));
            LegacyFixMenu(menu);
            s_setMenuHook->original()(wnd, menu);
            LegacyResizeGame();
            DrawMenuBar(wnd);
        }

        // At this point, all game init is complete and we are at the main menu. Any DirectDraw
        // surfaces requested at this point are ephemeral and are used to render images to HWNDs.
        DDrawSignalInitComplete();

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
    if (s_flags & e_overrideWindowRect) {
        SetRect(lpRect, 0, 0, 640, 480);
        return TRUE;
    } else {
        return s_getWindowRectHook->original()(hWnd, lpRect);
    }
}

// ================================================================================================

static BOOL WINAPI LegacyGetClientRect(_In_ HWND hWnd, _Out_ LPRECT lpRect)
{
    // See above
    SetRect(lpRect, 0, 0, 640, 480);
    return TRUE;
}

// ================================================================================================

static BOOL WINAPI LegacyGetCursorPos(_Out_ LPPOINT lpPoint)
{
    POINT pos{ 0 };
    if (s_getCursorPosHook->original()(&pos) == FALSE)
        return FALSE;
    if (s_screenToClientHook->original()(s_legacyHWND, &pos) == FALSE)
        return FALSE;

    // Legacy.exe operates at 640x480, so we need to translate our coordinates to that...
    pos.x = (pos.x * 640) / s_gameResolution.x;
    pos.y = (pos.y * 480) / s_gameResolution.y;

    *lpPoint = pos;
    return TRUE;
}

// ================================================================================================

static BOOL WINAPI LegacySetCursorPos(_In_ int X, _In_ int Y)
{
    // Sorry Gage, but you can't move the cursor around.
    return TRUE;
}

// ================================================================================================

static BOOL WINAPI LegacyGetClientToScreen(_In_ HWND hWnd, _Inout_ LPPOINT lpPoint)
{
    return TRUE;
}

// ================================================================================================

static BOOL WINAPI LegacyGetScreenToClient(_In_ HWND hWnd, _Inout_ LPPOINT lpPoint)
{
    return TRUE;
}

// ================================================================================================

static BOOL WINAPI LegacySetMenu(_In_ HWND hWnd, _In_opt_ HMENU hMenu)
{
    return TRUE;
}

// ================================================================================================

static HMENU WINAPI LegacyLoadMenu(_In_opt_ HINSTANCE hInstance, _In_ LPCSTR lpMenuName)
{
    bool is_legacyMenu = hInstance == GetModuleHandleA(nullptr) && lpMenuName == MAKEINTRESOURCEA(101);
    if (s_legacyMenu && is_legacyMenu) {
        s_log << "LoadMenuA: servicing duplicate load request for menu " << (ULONG_PTR)lpMenuName << std::endl;
        return s_legacyMenu;
    } else {
        s_log << "LoadMenuA: loading menu " << (ULONG_PTR)lpMenuName << std::endl;
        HMENU result = s_loadMenuHook->original()(hInstance, lpMenuName);
        if (is_legacyMenu)
            s_legacyMenu = result;
        return result;
    }
}

// ================================================================================================

static int WINAPI LegacyGetSystemMetrics(_In_ int nIndex)
{
    switch (nIndex) {
    case SM_CXSCREEN:
        return 640;
    case SM_CYSCREEN:
        return 480;
    default:
        return s_getSystemMetricsHook->original()(nIndex);
    }
}

// ================================================================================================

static void PeekMessageDebug(LPMSG lpMsg, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    if (wRemoveMsg & PM_REMOVE)
        s_log << "PeekMessageA: removed";
    else
        s_log << "PeekMessageA: peeked";
    s_log << " wMsgFilterMin: 0x" << std::hex << wMsgFilterMin << " wMsgFilterMax: 0x"
          << wMsgFilterMax << " msg: ";

#define DECLARE_WM(x) \
    case x: \
        s_log << #x << " "; \
        break;

    switch (lpMsg->message) {
#include "WM.inl"
    default:
        s_log << "0x" << std::hex << lpMsg->message << " ";
        break;
    }
#undef DECLARE_WM

    s_log << std::hex << "wParam: 0x" << lpMsg->wParam << " lParam: 0x" << lpMsg->lParam << std::endl;
}

// ================================================================================================

static BOOL WINAPI LegacyPeekMessage(_Out_ LPMSG lpMsg, _In_opt_ HWND hWnd, _In_ UINT wMsgFilterMin,
                                     _In_ UINT wMsgFilterMax, _In_ UINT wRemoveMsg)
{
    BOOL result = s_peekMessageHook->original()(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    if (result == FALSE)
        return FALSE;

//#define LEGACY_PEEKMSG_LOG
//#define LEGACY_PEEKMSG_LOG_VERBOSE
#if defined(LEGACY_PEEKMSG_LOG) && defined(LEGACY_PEEKMSG_LOG_VERBOSE)
    PeekMessageDebug(lpMsg, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
#endif

    if (!(wRemoveMsg & PM_REMOVE))
        return result;

    switch (lpMsg->message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE: {
        bool down = lpMsg->wParam & MK_LBUTTON;
        LegacyHandleLMB(hWnd, down, GET_X_LPARAM(lpMsg->lParam), GET_Y_LPARAM(lpMsg->lParam));
        break;
    }

#if defined(LEGACY_PEEKMSG_LOG) && !defined(LEGACY_PEEKMSG_LOG_VERBOSE)
    default:
        PeekMessageDebug(lpMsg, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
        break;
#endif
    }

    return result;
}

// ================================================================================================

static BOOL WINAPI LegacyDialogProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    _DialogWndData* data = (_DialogWndData*)GetWindowLongA(wnd, GWL_USERDATA);

    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongA(wnd, GWL_USERDATA, lParam);
        data = (_DialogWndData*)lParam;
        BOOL result = data->m_dlgproc(wnd, msg, wParam, lParam);

        // Dialog was shown at a screenspace location that would be the center of a 640x480 desktop.
        // We can't assume anything, so we'll just redo the whole dealio.
        RECT dlg_rect{ };
        s_getWindowRectHook->original()(wnd, &dlg_rect);
        POINT size{ (dlg_rect.right - dlg_rect.left), (dlg_rect.bottom - dlg_rect.top) };
        POINT pos{ ((s_gameResolution.x / 2) - (size.x / 2)),
                   ((s_gameResolution.y / 2) - (size.y / 2)) };
        s_clientToScreenHook->original()(s_legacyHWND, &pos);
        SetWindowPos(wnd, nullptr, pos.x, pos.y, -1, -1, SWP_NOSIZE | SWP_NOZORDER);

        // When Legacy goes to BltFast the graphics, they are in screenspace... ugh.
        // Options Menu (132) has an issue where the graphics are blitted too high.???
        int offsetX = s_getSystemMetricsHook->original()(SM_CXDLGFRAME);
        int offsetY = 0;

        if (data->m_template == MAKEINTRESOURCEA(132)) {
            offsetY -= s_getSystemMetricsHook->original()(SM_CYDLGFRAME);
            offsetY -= s_getSystemMetricsHook->original()(SM_CYMENU);
        }
        DDrawSetBltOffset(dlg_rect.left + offsetX, dlg_rect.top + offsetY);
        data->m_previousBltTarget = DDrawBltToHWND(wnd);

        return result;
    }
    default:
        if (!data) {
            s_log << "LegacyDialogProc: Received WM 0x" << std::hex << msg
                  << " before GWL_USERDATA was set... Dropping." << std::endl;
            return FALSE;
        }
        return data->m_dlgproc(wnd, msg, wParam, lParam);
    }
}

// ================================================================================================

static INT_PTR WINAPI LegacyDialogBoxParam(_In_opt_ HINSTANCE hInstance,
                                           _In_ LPCSTR lpTemplateName,
                                           _In_opt_ HWND hWndParent,
                                           _In_opt_ DLGPROC lpDialogFunc,
                                           _In_ LPARAM dwInitParam)
{
    // We're just replacing the DlgProc with our own. Unfortunately, dialogs are slightly different
    // from other windows in how the procedures work. Hence the differences from CreateWindowExA.

    _DialogWndData userdata{ hInstance, lpTemplateName, lpDialogFunc, dwInitParam };
    INT_PTR result = s_dialogBoxParamHook->original()(hInstance, lpTemplateName, hWndParent,
                                                      LegacyDialogProc, (LPARAM)&userdata);
    DDrawBltToHWND(userdata.m_previousBltTarget);
    return result;
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

POINT Win32LockClientSize()
{
    s_gameResolutionLock.lock();
    return s_gameResolution;
}

// ================================================================================================

void Win32UnlockClientSize()
{
    s_gameResolutionLock.unlock();
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
    MAKE_HOOK(L"User32.dll", "GetCursorPos", LegacyGetCursorPos, s_getCursorPosHook);
    MAKE_HOOK(L"User32.dll", "SetCursorPos", LegacySetCursorPos, s_setCursorPosHook);
    MAKE_HOOK(L"User32.dll", "ClientToScreen", LegacyGetClientToScreen, s_clientToScreenHook);
    MAKE_HOOK(L"User32.dll", "ScreenToClient", LegacyGetScreenToClient, s_screenToClientHook);
    MAKE_HOOK(L"User32.dll", "SetMenu", LegacySetMenu, s_setMenuHook);
    MAKE_HOOK(L"User32.dll", "LoadMenuA", LegacyLoadMenu, s_loadMenuHook);
    MAKE_HOOK(L"User32.dll", "GetSystemMetrics", LegacyGetSystemMetrics, s_getSystemMetricsHook);
    MAKE_HOOK(L"User32.dll", "PeekMessageA", LegacyPeekMessage, s_peekMessageHook);
    MAKE_HOOK(L"User32.dll", "DialogBoxParamA", LegacyDialogBoxParam, s_dialogBoxParamHook);
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
    delete s_getCursorPosHook;
    delete s_setCursorPosHook;
    delete s_clientToScreenHook;
    delete s_screenToClientHook;
    delete s_setMenuHook;
    delete s_loadMenuHook;
    delete s_getSystemMetricsHook;
    delete s_peekMessageHook;
    delete s_dialogBoxParamHook;
    delete s_outputDebugStringHook;
}
